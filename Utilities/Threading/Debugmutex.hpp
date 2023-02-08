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
    std::timed_mutex Internal{};

    // Unified error func.
    [[noreturn]] static void Break(const std::string &&Message) noexcept
    {
        (void)std::fprintf(stderr, Message.c_str());

        #if defined (_MSC_VER)
        __debugbreak();
        #elif defined (__clang__)
        __builtin_debugtrap();
        #elif defined (__GNUC__)
        __builtin_trap();
        #endif

        // Incase someone included this in runtime code.
        volatile size_t Intentional_nullderef = 0xDEAD;
        *(size_t *)Intentional_nullderef = 0xDEAD;
    }

    void lock() noexcept
    {
        if (Currentowner == std::this_thread::get_id())
        {
            Break(std::format("Debugmutex: Recursive lock by thread {:X}\n", *(uint32_t *)&Currentowner));
        }

        if (Internal.try_lock_for(std::chrono::seconds(10)))
        {
            Currentowner = std::this_thread::get_id();
        }
        else
        {
            Break(std::format("Debugmutex: Timeout, locked by thread {:X}\n", *(uint32_t *)&Currentowner));
        }
    }
    void unlock() noexcept
    {
        (void)Internal.try_lock();
        Currentowner = {};
        Internal.unlock();
    }
};
