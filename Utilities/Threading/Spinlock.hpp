/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-07
    License: MIT
*/

#pragma once
#include <Utilities/Utilities.hpp>

// MS-STL atomic_flag uses a long for ABI.
struct Spinlock_t
{
    std::atomic_flag Flag{};

    // Modern Intel processors sleep for ~140 cycles rather than the older ~20-40.
    bool try_lock() noexcept { return !Flag.test_and_set(std::memory_order_acquire); }
    void unlock() noexcept { assert(Flag.test()); Flag.clear(std::memory_order_release); }

    void lock() noexcept
    {
        // Common case, no conflicts.
        if (try_lock()) [[likely]]
            return;

        // No sleep.
        for (size_t i = 0; i < 16; ++i)
        {
            if (try_lock()) return;
        }

        // Short sleep.
        for (size_t i = 0; i < 128; ++i)
        {
            // Intrinsic is always scheduled, regardless of if-statements.
            // So we have it first to avoid static-analysis warnings.
            _mm_pause();

            if (try_lock()) return;
        }

        while (!try_lock())
        {
            // Medium sleep.
            for (size_t i = 0; i < 512; ++i)
            {
                // Might be NOP on some CPUs, but 16 pauses should create uOP sleep.
                _mm_pause(); _mm_pause(); _mm_pause(); _mm_pause();
                _mm_pause(); _mm_pause(); _mm_pause(); _mm_pause();
                _mm_pause(); _mm_pause(); _mm_pause(); _mm_pause();
                _mm_pause(); _mm_pause(); _mm_pause(); _mm_pause();

                if (try_lock()) return;
            }

            // Long sleep.
            std::this_thread::yield();
        }
    }
};
