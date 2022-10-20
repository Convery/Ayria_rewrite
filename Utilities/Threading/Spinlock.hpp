/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-07
    License: MIT
*/

#pragma once
#include <thread>
#include <atomic>
#include <intrin.h>

// STL's atomic_flag uses a log because of ABI..
struct Spinlock_t
{
    std::atomic<bool> Flag{};

    void unlock() noexcept { Flag.store(false, std::memory_order_release); }
    bool try_lock() noexcept { return !Flag.exchange(true, std::memory_order_acquire); }

    void lock() noexcept
    {
        while (true)
        {
            // No sleep.
            for (size_t i = 0; i < 16; ++i)
            {
                if (try_lock()) return;
            }

            // Short sleep.
            for (size_t i = 0; i < 128; ++i)
            {
                // Intrinsic is always executed, regardless of if-statements.
                // So we have it first to avoid static-analysis warnings.
                _mm_pause();

                if (try_lock()) return;
            }

            // Medium sleep.
            for (size_t i = 0; i < 1024; ++i)
            {
                // Might be NOP on some CPUs..
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();

                if (try_lock()) return;
            }

            // Long sleep.
            std::this_thread::yield();
        }
    }
};
