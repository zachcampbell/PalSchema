#pragma once

#include "Unreal/Core/Templates/Function.hpp"

namespace UECustom {
    enum class ENamedThreads : RC::Unreal::int64 {
        MainQueue = 0,
        NormalTaskPriority = 0,
        NormalThreadPriority = 0,
        RHIThread = 0,
        AudioThread = 1,
        GameThread = 2,
        NumQueues = 2,
        NumTaskPriorities = 2,
        ActualRenderingThread = 3,
        NumThreadPriorities = 3,
        QueueIndexShift = 8,
        TaskPriorityShift = 9,
        ThreadPriorityShift = 10,
        AnyNormalThreadNormalTask = 255,
        AnyThread = 255,
        ThreadIndexMask = 255,
        LocalQueue = 256,
        QueueIndexMask = 256,
        GameThread_Local = 258,
        ActualRenderingThread_Local = 259,
        HighTaskPriority = 512,
        TaskPriorityMask = 512,
        AnyNormalThreadHiPriTask = 767,
        HighThreadPriority = 1024,
        AnyHiPriThreadNormalTask = 1279,
        AnyHiPriThreadHiPriTask = 1791,
        BackgroundThreadPriority = 2048,
        AnyBackgroundThreadNormalTask = 2303,
        AnyBackgroundHiPriTask = 2815,
        ThreadPriorityMask = 3072,
        UnusedAnchor = 4294967295
    };

    void AsyncTask(ENamedThreads Thread, const RC::Unreal::TUniqueFunction<void()>& Function);
#ifdef __linux__
    void EnsureGameThreadDrain();
#endif
}