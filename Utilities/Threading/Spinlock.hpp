/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-07
    License: MIT
*/

#pragma once
#include <Utilities/Utilities.hpp>

// STL's atomic_flag uses a log because of ABI..
struct Spinlock_t
{    
    std::atomic<bool> Flag{};

    void unlock() noexcept { Flag.store(false, std::memory_order_release); }
    bool try_lock() noexcept { return !Flag.exchange(true, std::memory_order_acquire); }

    void lock() noexcept
    {
        // Modern Intel processors prefer exponential backoff for pauses.
        // TODO(tcn): We should benchmark on AMD and older Intel. 
        constexpr bool useExponentialbackoff = true;

        // Fancy modern version (0 - 64 mm_pause).
        if constexpr (useExponentialbackoff)
        {
            const int Max = 64;
            int Current = 1;

            while (!try_lock())
            {
                for (int Backoff = Current; Backoff != 0; --Backoff)
                {
                    _mm_pause();
                }

                Current = Current < Max ? Current << 1 : Max;
            }
        }

        // Legacy version (0 - 16 mm_pause + sleep fallback)
        else
        {
            while (!try_lock())
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
    }
};
