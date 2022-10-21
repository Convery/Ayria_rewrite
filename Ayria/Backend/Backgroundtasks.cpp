/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-09-26
    License: MIT
*/

#pragma once
#include <Ayria.hpp>

using namespace Backend::Taskrunner;

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
            std::scoped_lock Guard(getSingleton().Threadsafe);

            for (auto &Task : getSingleton().Recurringtasks)
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

    // Unreachable.
    return 1;
}

// Export functionality to the plugins.
extern "C" EXPORT_ATTR void __cdecl Createperiodictask(void(__cdecl * Callback)(), unsigned int PeriodMS)
{
    assert(PeriodMS && Callback);

    if (PeriodMS && Callback) [[likely]]
        Enqueuetask(Callback, PeriodMS);
}

// An example of a startup task.
const struct Startup_t
{
    Startup_t()
    {
        addStartup([]
        {
            // Per platform thread initialization, TODO(tcn): add pthread.
            #if defined (_WIN32)
            _beginthreadex(NULL, NULL, Backgroundthread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
            #else
            std::thread(Backgroundthread, nullptr).detach();
            #endif

            // Register for termination if not already provided.
            std::atexit([]() { Terminate(); });
        });

    }
} Startup{};
