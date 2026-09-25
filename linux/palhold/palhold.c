// palhold: hold world-save map objects of PalSchema buildings until PalSchema has registered them.
//
// A Linux dedicated server applies its world save about 5 s after launch; UE4SS and PalSchema cannot start before
// 30 s. Every saved map object whose MapObjectId comes from a PalSchema building mod fails its init process at 5 s,
// the init manager's ApplyWorldSaveData sequence fails with it, and the server never finishes starting (no logins,
// no autosave). A failed init handle is terminal, so this preloaded library prevents the failure instead: it
// detours the game's per-entry apply (UPalMapObjectManager, 0x73b04d0) from process start, and for entries whose id
// is listed in Mods/PalSchema/held-map-object-ids.txt it keeps the entry and its init handle and returns without
// applying. The handle stays pending, so the sequence waits instead of failing. Once PalSchema has registered its
// buildings it calls palhold_release() on the game thread, which runs the game's own apply for every held entry
// with its original handle; the sequence then completes normally.
//
// Addresses are for PalServer-Linux-Shipping v1.0.5.102999 (non-PIE, loaded with ASLR off); the prologue is checked
// byte for byte and on any mismatch nothing is patched.
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>

#define APPLY_ONE 0x73b04d0UL           // void(UPalMapObjectManager*, TSharedRef<Handle>*, FPalMapObjectSaveData*)
#define APPLY_LOOP 0x73b4e60UL          // UPalMapObjectManager::LateApplyWorldSaveDataAsync, 990 bytes
#define APPLY_LOOP_END (APPLY_LOOP + 990)
#define NAME_TO_STRING 0x7977420UL      // FString FName::ToString() const (sret rdi, this rsi)
#define FMEMORY_FREE 0x78118b0UL
#define PATCH_LEN 17
#define ENTRY_CHECK 0x20                // FName MapObjectId + FGuid MapObjectInstanceId + 8 more bytes

static const unsigned char kPrologue[PATCH_LEN] = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53,  // push rbp, r15, r14, r13, r12, rbx
    0x48, 0x81, 0xec, 0xb8, 0x01, 0x00, 0x00                      // sub rsp, 0x1b8
};

typedef struct { void* Object; void* Controller; } SharedRef;
typedef struct { uint16_t* Data; int32_t Num; int32_t Max; } FString;
typedef void (*ApplyOneFn)(void* Manager, SharedRef* Handle, void* Entry);

typedef struct {
    void* Manager;
    SharedRef Handle;
    void* Entry;
    unsigned char Check[ENTRY_CHECK];
} Held;

static ApplyOneFn g_original;           // trampoline: the 17 relocated prologue bytes, then a jump back
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char** g_ids;
static int g_idCount;
static Held* g_held;
static int g_heldCount, g_heldCap;
static int g_released;
// FName (8 bytes) -> held or not, so each distinct id is named once.
static struct { uint64_t Name; int Hold; }* g_cache;
static int g_cacheCount, g_cacheCap;

static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[palhold] ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

static int name_is_listed(const void* name)
{
    FString s = {0};
    ((void (*)(FString*, const void*))NAME_TO_STRING)(&s, name);
    int hit = 0;
    if (s.Data && s.Num > 1)
    {
        for (int i = 0; i < g_idCount && !hit; ++i)
        {
            const char* id = g_ids[i];
            int n = (int)strlen(id);
            if (n != s.Num - 1) continue;
            hit = 1;
            for (int k = 0; k < n; ++k) if (s.Data[k] != (unsigned char)id[k]) { hit = 0; break; }
        }
    }
    if (s.Data) ((void (*)(void*))FMEMORY_FREE)(s.Data);
    return hit;
}

static int should_hold(const void* entry)
{
    uint64_t name;
    memcpy(&name, entry, sizeof name);
    for (int i = 0; i < g_cacheCount; ++i) if (g_cache[i].Name == name) return g_cache[i].Hold;
    int hold = name_is_listed(entry);
    if (g_cacheCount == g_cacheCap)
    {
        g_cacheCap = g_cacheCap ? g_cacheCap * 2 : 256;
        g_cache = realloc(g_cache, (size_t)g_cacheCap * sizeof *g_cache);
    }
    g_cache[g_cacheCount].Name = name;
    g_cache[g_cacheCount].Hold = hold;
    ++g_cacheCount;
    return hold;
}

static void apply_one_hook(void* manager, SharedRef* handle, void* entry)
{
    uintptr_t from = (uintptr_t)__builtin_return_address(0);
    if (from >= APPLY_LOOP && from < APPLY_LOOP_END && handle && handle->Object && entry)
    {
        pthread_mutex_lock(&g_lock);
        if (!g_released && should_hold(entry))
        {
            if (g_heldCount == g_heldCap)
            {
                g_heldCap = g_heldCap ? g_heldCap * 2 : 256;
                g_held = realloc(g_held, (size_t)g_heldCap * sizeof *g_held);
            }
            Held* h = &g_held[g_heldCount++];
            h->Manager = manager;
            h->Handle = *handle;
            // Our copy of the TSharedRef takes a shared reference, exactly as the game's own copies do.
            if (h->Handle.Controller) __atomic_add_fetch((int32_t*)((char*)h->Handle.Controller + 8), 1, __ATOMIC_SEQ_CST);
            h->Entry = entry;
            memcpy(h->Check, entry, ENTRY_CHECK);
            pthread_mutex_unlock(&g_lock);
            return;
        }
        pthread_mutex_unlock(&g_lock);
    }
    g_original(manager, handle, entry);
}

static void release_ref(SharedRef* r)
{
    // TSharedRef release, as the game does it: controller +8 shared count, +0xc weak count,
    // vtable[0] destroys the object, vtable[2] destroys the controller.
    char* c = (char*)r->Controller;
    if (!c) return;
    if (__atomic_sub_fetch((int32_t*)(c + 8), 1, __ATOMIC_SEQ_CST) == 0)
    {
        void (**vt)(void*) = *(void (***)(void*))c;
        vt[0](c);
        if (__atomic_sub_fetch((int32_t*)(c + 0xc), 1, __ATOMIC_SEQ_CST) == 0) vt[2](c);
    }
    r->Controller = 0;
}

// Game thread only. Applies every held entry through the game's own function with its original init handle.
// Returns how many were applied; later calls pass straight through.
__attribute__((visibility("default"))) int palhold_release(void)
{
    pthread_mutex_lock(&g_lock);
    g_released = 1;
    Held* held = g_held;
    int count = g_heldCount;
    g_held = 0;
    g_heldCount = g_heldCap = 0;
    pthread_mutex_unlock(&g_lock);
    if (!g_original) return -1;
    int applied = 0, changed = 0;
    for (int i = 0; i < count; ++i)
    {
        Held* h = &held[i];
        if (memcmp(h->Check, h->Entry, ENTRY_CHECK) != 0)
        {
            // The save data moved or was freed; applying it would read garbage. Its handle stays pending and the
            // sequence will not complete, which is no worse than the failure this library exists to prevent.
            ++changed;
            continue;
        }
        g_original(h->Manager, &h->Handle, h->Entry);
        release_ref(&h->Handle);
        ++applied;
    }
    free(held);
    say("released %d held map object(s) to the game's apply%s\n", applied, changed ? "" : "; world save apply can complete");
    if (changed) say("WARNING: %d held entr%s changed in memory before release and were NOT applied; the init sequence will stay pending\n", changed, changed == 1 ? "y" : "ies");
    return applied;
}

__attribute__((visibility("default"))) int palhold_held_count(void)
{
    pthread_mutex_lock(&g_lock);
    int n = g_heldCount;
    pthread_mutex_unlock(&g_lock);
    return n;
}

__attribute__((visibility("default"))) int palhold_active(void) { return g_original != 0; }

static void load_ids(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f))
    {
        size_t n = strcspn(line, "\r\n");
        line[n] = 0;
        if (!n || line[0] == '#') continue;
        g_ids = realloc(g_ids, (size_t)(g_idCount + 1) * sizeof *g_ids);
        g_ids[g_idCount++] = strdup(line);
    }
    fclose(f);
}

__attribute__((constructor)) static void palhold_init(void)
{
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
    char* slash = strrchr(exe, '/');
    if (!slash || strcmp(slash + 1, "PalServer-Linux-Shipping") != 0) return;  // setarch and friends
    if (memcmp((const void*)APPLY_ONE, kPrologue, PATCH_LEN) != 0)
    {
        say("per-entry apply prologue does not match v1.0.5.102999; not installed\n");
        return;
    }
    *slash = 0;
    char path[4200];
    snprintf(path, sizeof path, "%s/Mods/PalSchema/held-map-object-ids.txt", exe);
    load_ids(path);
    if (!g_idCount)
    {
        say("no ids in %s; not installed\n", path);
        return;
    }

    unsigned char* tramp = mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED) { say("mmap failed; not installed\n"); return; }
    // jmp qword [rip+0]; dq target
    static const unsigned char kJmp[6] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
    memcpy(tramp, kPrologue, PATCH_LEN);
    memcpy(tramp + PATCH_LEN, kJmp, 6);
    uint64_t back = APPLY_ONE + PATCH_LEN;
    memcpy(tramp + PATCH_LEN + 6, &back, 8);

    unsigned char patch[PATCH_LEN];
    memcpy(patch, kJmp, 6);
    uint64_t hook = (uint64_t)(uintptr_t)&apply_one_hook;
    memcpy(patch + 6, &hook, 8);
    memset(patch + 14, 0x90, PATCH_LEN - 14);

    long page = sysconf(_SC_PAGESIZE);
    uintptr_t start = APPLY_ONE & ~(uintptr_t)(page - 1);
    size_t len = (APPLY_ONE + PATCH_LEN) - start;
    if (mprotect((void*)start, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) { say("mprotect failed; not installed\n"); return; }
    g_original = (ApplyOneFn)(void*)tramp;
    memcpy((void*)APPLY_ONE, patch, PATCH_LEN);
    mprotect((void*)start, len, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char*)APPLY_ONE, (char*)APPLY_ONE + PATCH_LEN);
    say("installed; holding %d map object id(s) from %s until PalSchema releases them\n", g_idCount, path);
}
