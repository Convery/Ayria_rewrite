/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-01-23
    License: MIT
*/

#include <Ayria.hpp>

namespace Backend::Backgroundtasks
{
    // As constinit is not available for MSVC debugbuilds, a singleton is needed.
    struct Singleton_t final
    {
        using Taskinfo_t = struct
        {
            uint32_t PeriodMS, LastMS;
            Callback_t Callback;
        };

        Inlinedvector<Taskinfo_t, 8> Recurringtasks;
        Hashset<Callback_t> Tasklist;

        std::vector<Callback_t> Initialtasks;
        std::atomic<bool> doTerminate;
        Spinlock_t Threadsafe;
    };
    static Singleton_t &getSingleton()
    {
        static Singleton_t Local{};
        return Local;
    }

    // Startup tasks are only ran once, at Initialize().
    void addPeriodictask(Callback_t Callback, uint32_t PeriodMS)
    {
        auto &Singleton = getSingleton();
        std::scoped_lock Guard(Singleton.Threadsafe);

        // Prevent duplicates.
        if (Singleton.Tasklist.insert(Callback).second)
        {
            Singleton.Recurringtasks.emplace_back(PeriodMS, 0, Callback);
        }
    }
    void addStartuptask(Callback_t Callback)
    {
        auto &Singleton = getSingleton();
        std::scoped_lock Guard(Singleton.Threadsafe);
        Singleton.Initialtasks.emplace_back(Callback);
    }

    // Called from usercode (in or after main).
    void Initialize()
    {
        auto &Singleton = getSingleton();
        std::scoped_lock Guard(Singleton.Threadsafe);

        // We can not ensure ordering of the tasks.
        for (const auto &Task : Singleton.Initialtasks) Task();

        // Should only run once, so let the runtime reclaim the memory.
        Singleton.Initialtasks.clear();
        Singleton.Initialtasks.shrink_to_fit();
    }
    void Terminate()
    {
        getSingleton().doTerminate = true;
    }

    // Normal-priority thread.
    static unsigned __stdcall Backgroundthread(void *)
    {
        // Name this thread for easier debugging.
        setThreadname("Ayria_Backgroundthread");

        // Set SSE to behave properly, MSVC seems to be missing a flag.
        _mm_setcsr(_mm_getcsr() | 0x8040); // _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON

        // Runs until the application terminates or DLL unloads.
        while (true)
        {
            // Rolls over every 48 days.
            const auto Currenttime = GetTickCount();

            // Run the tasks in a lambda to make the compiler happy.
            [=]() -> void
            {
                auto &Singleton = getSingleton();
                std::scoped_lock Guard(Singleton.Threadsafe);

                for (auto &Task : Singleton.Recurringtasks)
                {
                    // Take advantage of unsigned overflows.
                    if ((Currenttime - Task.LastMS) > Task.PeriodMS) [[unlikely]]
                    {
                        // Accurate enough.
                        Task.LastMS = Currenttime;
                        Task.Callback();
                    }
                }
            }();

            // The user has requested us to terminate.
            if (getSingleton().doTerminate) [[unlikely]]
            {
                Infoprint("App termination requested by the user.");
                return 0;
            }

            // Most tasks run with periods in seconds.
            const auto Delta = GetTickCount() - Currenttime;
            const auto Remaining = std::clamp(int(50 - Delta), 0, 50);
            std::this_thread::sleep_for(std::chrono::milliseconds(Remaining));
        }

        // Unreachable but some static-analysis get upset.
        std::unreachable();
    }

    // Set up the background-thread via Initialize()
    const struct Startup_t
    {
        Startup_t()
        {
            addStartuptask([]
            {
                // Per platform thread initialization, TODO(tcn): add pthread.
                #if defined (_WIN32)
                    _beginthreadex(NULL, NULL, Backgroundthread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
                #else
                    std::thread(Backgroundthread, nullptr).detach();
                #endif

                // Register for termination if not already provided.
                (void)std::atexit([]() { Terminate(); });
            });
        }
    } Startup{};
}
