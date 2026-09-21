#ifdef __linux__
#include "Utility/Logging.h" // fmt before the Unreal headers: AssertionMacros defines a `check` macro that breaks fmt/ranges.h
#endif
#include "SDK/Classes/Async.h"
#include "SDK/PalSignatures.h"
#ifdef __linux__
// palhook: the engine's AsyncTask is not resolved on the Linux port. Game-thread tasks are queued and drained
// from a UE4SS ProcessEvent pre-callback when it runs on the game thread (the dedicated server's main thread,
// whose tid equals the pid). A task queued from the game thread runs inline. Anything other than GameThread
// also runs inline, which is what the callers expect (they never ask for another thread).
#include "Utility/Logging.h"
#include <Unreal/Hooks.hpp>
#include <deque>
#include <chrono>
#include <mutex>
#include <exception>
#include <unistd.h>
#include <sys/syscall.h>
namespace UECustom {
    namespace {
        std::mutex g_queue_mutex;
        struct Queued { RC::Unreal::TUniqueFunction<void()> fn; std::chrono::steady_clock::time_point queued_at; };
        std::deque<Queued> g_game_thread_queue;
        bool g_drain_registered = false;
        bool g_draining = false; // game-thread only: a task that reaches ProcessEvent re-enters drain(); the outer loop keeps the queue
        bool on_game_thread() { return static_cast<pid_t>(syscall(SYS_gettid)) == getpid(); }
        void drain(RC::Unreal::UObject*, RC::Unreal::UFunction*, void*)
        {
            if (!on_game_thread()) return;
            static bool announced = false;
            if (!announced) { announced = true; PS::Log<RC::LogLevel::Normal>(STR("Game-thread task drain is live (tid {}).\n"), static_cast<long>(syscall(SYS_gettid))); }
            if (g_draining) return; // re-entered from inside a task: exactly-once is kept by the outer loop
            g_draining = true;
            for (;;)
            {
                Queued task;
                {
                    std::lock_guard<std::mutex> lock(g_queue_mutex);
                    if (g_game_thread_queue.empty()) break;
                    task = std::move(g_game_thread_queue.front());
                    g_game_thread_queue.pop_front();
                }
                auto started = std::chrono::steady_clock::now();
                try { task.fn(); }
                catch (const std::exception& e) { PS::Log<RC::LogLevel::Error>(STR("Game-thread task failed: {}\n"), RC::to_generic_string(e.what())); }
                auto done = std::chrono::steady_clock::now();
                PS::Log<RC::LogLevel::Normal>(STR("Game-thread task ran: waited {} ms in the queue, took {} ms.\n"),
                    std::chrono::duration_cast<std::chrono::milliseconds>(started - task.queued_at).count(),
                    std::chrono::duration_cast<std::chrono::milliseconds>(done - started).count());
            }
            g_draining = false;
        }
    }
    void AsyncTask(ENamedThreads Thread, const RC::Unreal::TUniqueFunction<void()>& Function)
    {
        // Callers pass temporaries; take ownership by moving out of the (dead after this call) argument.
        auto& Owned = const_cast<RC::Unreal::TUniqueFunction<void()>&>(Function);
        if (Thread != ENamedThreads::GameThread || on_game_thread()) { Owned(); return; }
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        if (!g_drain_registered) PS::Log<RC::LogLevel::Warning>(STR("Game-thread task queued before the drain was registered; it runs once EnsureGameThreadDrain() has been called.\n"));
        g_game_thread_queue.push_back(Queued{std::move(Owned), std::chrono::steady_clock::now()});
    }
    // Registers the drain from a UE4SS-owned thread (on_unreal_init), the path the probe mod proved;
    // registering from the loading thread mid-flight did not take (shadow run 60).
    void EnsureGameThreadDrain()
    {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        if (!g_drain_registered) { RC::Unreal::Hook::RegisterProcessEventPreCallback(&drain); g_drain_registered = true; PS::Log<RC::LogLevel::Normal>(STR("Game-thread task drain registered.\n")); }
    }
}
#else
namespace UECustom {
    void AsyncTask(ENamedThreads Thread, const RC::Unreal::TUniqueFunction<void()>& Function)
    {
        using AsyncTaskSignature = void(*)(ENamedThreads, const RC::Unreal::TUniqueFunction<void()>&);
        static void* FunctionPtr = nullptr;
        if (!FunctionPtr)
        {
            FunctionPtr = Palworld::SignatureManager::GetSignature("AsyncTask");
        }
        if (!FunctionPtr)
        {
            return;
        }
        reinterpret_cast<AsyncTaskSignature>(FunctionPtr)(Thread, Function);
    }
}
#endif
