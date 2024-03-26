/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-09
    License: MIT
*/

#pragma once
#include <Utilities/Utilities.hpp>

// Timed mutex that checks for cyclic locking and long tasks.
struct Debugmutex_t
{
    std::thread::id Currentowner{};
    std::timed_mutex Mutex{};

    // Unified error func.
    [[noreturn]] static void Break(const std::string &Message)
    {
        (void)std::fprintf(stderr, Message.c_str());

        #if defined (_MSC_VER)
        __debugbreak();
        #elif defined (__clang__)
        __builtin_debugtrap();
        #elif defined (__GNUC__)
        __builtin_trap();
        #endif

        // Incase someone included this in release mode.
        volatile size_t Intentional_nullderef = 0xDEAD;
        *(size_t *)Intentional_nullderef = 0xDEAD;
    }

    void lock()
    {
        if (Currentowner == std::this_thread::get_id())
        {
            Break(std::format("Debugmutex: Recursive lock by thread {:X}\n", std::bit_cast<uint32_t>(Currentowner)));
        }

        if (!Mutex.try_lock_for(std::chrono::seconds(10)))
        {
            Break(std::format("Debugmutex: Timeout, locked by thread {:X}\n", std::bit_cast<uint32_t>(Currentowner)));
        }

        Currentowner = std::this_thread::get_id();
    }
    void unlock()
    {
        if (Currentowner != std::this_thread::get_id())
        {
            Break(std::format("Debugmutex: Thread {:X} tried to unlock a mutex owned by {:X}\n", std::bit_cast<uint32_t>(std::this_thread::get_id()), std::bit_cast<uint32_t>(Currentowner)));
        }

        Currentowner = {};
        Mutex.unlock();
    }
};
