/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-27
    License: MIT

    Separate version of Stdinclude for libUtilities;
    so we don't have to rebuild it all the time.
*/

#pragma once

// Our configuration-, define-, macro-options.
#include <Configuration.hpp>

// Standalone utilities.
#include "Constexprhelpers.hpp"
#include "Datatypes.hpp"

// Ignore warnings from third-party code.
#pragma warning(push, 0)

// Feature-test macros.
#include <version>

// Standard-library includes for libUtilities.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <execution>
#include <filesystem>
#include <functional>
#include <format>
#include <io.h>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

// Third-party libraries.
#include <Thirdparty.hpp>

// Platform-specific libraries.
#if defined(_WIN32)
#include <intrin.h>
#include <Windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <windowsx.h>
#include <winternl.h>
#include <D3dkmthk.h>
#else
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>
#endif

// Restore warnings.
#pragma warning(pop)

// Extensions to the language.
using namespace std::literals;

// All utilities.
#include "Containers/Bytebuffer.hpp"
#include "Containers/Ringbuffer.hpp"

#include "Crypto/Checksums.hpp"
#include "Crypto/HWID.hpp"
#include "Crypto/OpenSSLWrapper.hpp"
#include "Crypto/qDSA.hpp"
#include "Crypto/SHA.hpp"
#include "Crypto/Tiger192.hpp"

#include "Encoding/Base58.hpp"
#include "Encoding/Base64.hpp"
#include "Encoding/Base85.hpp"
#include "Encoding/JSON.hpp"
#include "Encoding/UTF8.hpp"

#include "Hacking/Disassembly.hpp"
#include "Hacking/Hooking.hpp"
#include "Hacking/Memory.hpp"
#include "Hacking/Patternscan.hpp"

#include "Strings/Stringsplit.hpp"
#include "Strings/toHexstring.hpp"
#include "Strings/Variadicstring.hpp"

#include "Threading/Debugmutex.hpp"
#include "Threading/Spinlock.hpp"

#include "Wrappers/Databasewrapper.hpp"
#include "Wrappers/Filesystem.hpp"
#include "Wrappers/Logging.hpp"

// Small utilities that don't need their own module.
#pragma region Misc_utils

    // Python-esque constructs for loops.
    #pragma region Python

    // for (const auto &x : Slice({1, 2, 3, 4, 5}, 1, 3)) = { 2, 3, 4 }
    template <typename T, typename Container> constexpr auto Slice(const Container &Args, int Begin, int End)
    {
        const auto Range = std::span(Args);
        const auto First = std::next(Range.begin(), Begin);
        const auto Last = End ? std::next(Range.begin(), End) : std::prev(Range.end(), -End);

        return std::ranges::subrange(First, Last);
    }

    // for (const auto &x : Range(1, 100, 2)) = { 1, 3, 5 ... }
    template <typename T, typename Steptype = int> constexpr auto Range(T Start, T Stop, Steptype Stepsize = 1)
    {
        return std::views::iota(Start, Stop) | std::views::stride(Stepsize);
    }

    // for (const auto &[Index, Value] : Enumerate({ 1, 2, 3, 4 })) = { {0, 1}, {1, 2}, ... }
    template <typename T, typename Container> constexpr auto Enumerate(const Container &Args, size_t Start = 0)
    {
        const auto Indicies = std::views::iota(Start, Start + Args.size());
        return std::views::zip(Indicies, Args);
    }

    // MSVC deduction needs help..
    template <typename Range> requires std::ranges::range<Range> constexpr auto Enumerate(const Range &Args, size_t Start = 0)
    {
        const auto Indicies = std::views::iota(Start, Start + std::ranges::distance(Args));
        return std::views::zip(Indicies, Args);
    }

    // Need to extend the lifetime of the list.
    template <typename T> constexpr auto Enumerate(const std::initializer_list<T> &Args, size_t Start = 0)
    {
        std::vector<std::pair<size_t, T>> Vector;
        Vector.reserve(Args.size());

        for (const auto &[Index, Item] : Enumerate<T, std::initializer_list<T>>(Args, Start))
            Vector.emplace_back(Index, Item);

        return Vector;
    }
    template <typename T> constexpr auto Slice(const std::initializer_list<T> &Args,  int Begin, int End)
    {
        std::vector<T> Vector;
        Vector.reserve(Args.size());

        for (const auto &Item : Slice<T, std::initializer_list<T>>(Args, Begin, End))
            Vector.emplace_back(Item);

        return Vector;
    }

    #pragma endregion

    // Helper for debug-builds.
    #if defined (_WIN32)
    inline void setThreadname(std::string_view Name)
    {
        if constexpr (Build::isDebug)
        {
            #pragma pack(push, 8)
            using THREADNAME_INFO = struct { DWORD dwType; LPCSTR szName; DWORD dwThreadID; DWORD dwFlags; };
            #pragma pack(pop)

            __try
            {
                const THREADNAME_INFO Info{ 0x1000, Name.data(), GetCurrentThreadId(), 0 };
                RaiseException(0x406D1388, 0, sizeof(Info) / sizeof(ULONG_PTR), reinterpret_cast<const ULONG_PTR *>(&Info));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    #elif defined (__linux__)
    inline void setThreadname(std::string_view Name)
    {
        if constexpr (Build::isDebug)
        {
            prctl(PR_SET_NAME, Name.data(), 0, 0, 0);
        }
    }
    #else
    inline void setThreadname(std::string_view Name)
    {
        static_asseert(false, "Not implemented.");
    }
    #endif

    // Simple PRNG to avoid OS dependencies.
    namespace RNG
    {
        // Xoroshiro128+
        inline uint64_t Next()
        {
            static uint64_t Table[2]{ __rdtsc() ^ Hash::WW64(std::this_thread::get_id()), Hash::WW64(__rdtsc() ^ std::thread::hardware_concurrency()) };
            const uint64_t Result = Table[0] + Table[1];
            const uint64_t S1 = Table[0] ^ Table[1];

            Table[0] = std::rotl(Table[0], 24) ^ S1 ^ (S1 << 16);
            Table[1] = std::rotl(S1, 37);

            return Result;
        }
    };

#pragma endregion
