/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-09-26
    License: MIT
*/

#pragma once
#include <Ayria.hpp>

namespace Backend::Taskrunner
{
    using Taskcallback_t = void(__cdecl *)();
    struct Singleton_t
    {
        using Task_t = struct
        {
            uint32_t PeriodMS, LastMS;
            Taskcallback_t Callback;
        };

        std::vector<Taskcallback_t> Initialtasks;
        Inlinedvector<Task_t, 8> Recurringtasks;
        std::atomic<bool> doTerminate;
        Spinlock_t Threadsafe;
    };

    // Tasks are often set via ctor, so a singleton pattern is needed.
    static Singleton_t &getSingleton()
    {
        static Singleton_t Local{};
        return Local;
    }

    // Initial tasks should run from main(), and only once.
    inline void Enqueuetask(Taskcallback_t Callback, uint32_t PeriodMS = 1000)
    {
        std::scoped_lock Guard(getSingleton().Threadsafe);
        getSingleton().Recurringtasks.emplace_back(PeriodMS, 0, Callback);
    }
    inline void addStartup(Taskcallback_t Callback)
    {
        std::scoped_lock Guard(getSingleton().Threadsafe);
        getSingleton().Initialtasks.emplace_back(Callback);
    }

    // Called from usercode.
    inline void runStartup()
    {
        std::scoped_lock Guard(getSingleton().Threadsafe);

        for (const auto &Task : getSingleton().Initialtasks)
            Task();

        // Should only run once.
        getSingleton().Initialtasks.clear();
    }
    inline void Terminate()
    {
        getSingleton().doTerminate = true;
    }
}
