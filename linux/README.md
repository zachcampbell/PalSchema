# PalSchema on the native Linux dedicated server

This branch builds PalSchema as a C++ mod for the Linux UE4SS port
(BlackBookOfficial/ue4ss-linux-palworld, release commit 4bf136e plus the 20 patches on the
`palhook-gmalloc` branch of the companion fork). Verified 2026-09-20 on PalServer-Linux-Shipping
v1.0.5.102999 with a live world and a connected client: raw tables, enums, translations, items,
pals (including runtime-built ranch action classes), npcs, appearance, help guide, skins, resources,
buildings and custom spawners, plus a full content mod (Pal Variant Paldemonium) loading clean.

What is different from the Windows build:

- `linux/shim/` replaces the Windows-only pieces: safetyhook over funchook, an efsw stub, a
  SignatureManager with adapters and engine addresses located with Ghidra and guarded by prologue
  checks, operator new/delete forwarded to the game's exported allocator, and UE4SSProgram bound
  through dlsym. All addresses are for v1.0.5.102999; `linux/ghidra-signatures.md` says how each
  was found so they can be re-derived after a game update.
- Dedicated-server lifecycle: the core data tables and the game instance exist before UE4SS starts
  mods, so the registry is seeded from the live object array, the Serialize notification is replayed
  for boot-loaded tables, and the GameInstanceInit loaders are queued to the game thread through a
  ProcessEvent pre-callback drain (`src/SDK/Classes/Async.cpp`).
- Composite data tables: raw edits are also applied to the composite table itself, because its merged
  row map is cached at boot.
- Hardcoded Windows vtable indices are +1 here (Itanium double destructor slot) and hardcoded member
  offsets are resolved through reflection where they differed (UWorldPartitionRuntimeLevelStreamingCell).
- Actor spawning goes through a reflection-driven parameter block (`src/SDK/Helper/LinuxSpawn.cpp`).
- Not available on Linux: the item save-safety hooks (three of seven functions located, PalSchema
  installs them as a set) and FindComponentTemplateByName (stripped from the shipping binary).

Build: `linux/build-full.sh` (g++-13, C++23, static libstdc++/libgcc, `-Wl,-Bsymbolic`), against the
rebuilt libUE4SS with GUI and input disabled; the mod must be compiled with the same HAS_GUI setting as
the library. Install as `Mods/PalSchema/libs/main.so`. Server-side rules learned the hard way: run the
binary with ASLR off (`setarch x86_64 -R`), never let UE4SS calls that throw run, and a mod that adds
enum values requires the same mod on every client or joins time out.
