/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-27
    License: MIT
*/

#pragma once
#include <Stdinclude.hpp>

#include "Datatypes.hpp"
#include "Constexprhelpers.hpp"

#include "Containers/Bytebuffer.hpp"
#include "Containers/Ringbuffer.hpp"

#include "Crypto/Checksums.hpp"
#include "Crypto/qDSA.hpp"
#include "Crypto/SHA.hpp"
#include "Crypto/Tiger192.hpp"
#include "Crypto/OpenSSLWrapper.hpp"

#include "Encoding/Base58.hpp"
#include "Encoding/Base64.hpp"
#include "Encoding/Base85.hpp"
#include "Encoding/JSON.hpp"
#include "Encoding/UTF8.hpp"

#include "Strings/Stringsplit.hpp"
#include "Strings/toHexstring.hpp"
#include "Strings/Variadicstring.hpp"

#include "Threading/Debugmutex.hpp"
#include "Threading/Spinlock.hpp"

#include "Wrappers/Logging.hpp"
#include "Wrappers/Databasewrapper.hpp"
#include "Wrappers/Filesystem.hpp"

// Small utilities that don't need their own module.
#pragma region Misc_utils

    // Python-esque constructs for loops.
    #pragma region Python

    // for (const auto &[Index, Value] : Enumerate(std::vector{ 1, 2, 3, 4 })) = { {0, 1}, {1, 2}, ... }
    template <typename T, typename Iter = decltype(std::declval<T>().begin()), typename = decltype(std::declval<T>().size())>
    constexpr auto Enumerate(T &&Iteratable, size_t Start = 0)
    {
        // This should be optimized away..
        struct Countediterator_t
        {
            size_t Index, Size;
            Iter Iterator;

            bool operator !=(const std::default_sentinel_t &Right) const { return !!Size; }
            auto operator *() const { return std::tie(Index, *Iterator); }
            void operator ++() { ++Index; --Size; ++Iterator; }
        };
        struct Wrapper_t
        {
            size_t Start;
            T Iterable;

            auto begin() { return Countediterator_t{ Start, Iterable.size(), Iterable.begin() }; }
            static auto end() { return std::default_sentinel_t{}; }
        };

        return Wrapper_t{ Start, Iteratable };
    }

    // for (const auto &[Index, Value] : Enumerate({ 1, 2, 3, 4 })) = { {0, 1}, {1, 2}, ... }
    template <typename T, typename Iter = decltype(std::declval<std::initializer_list<T>>().begin())>
    constexpr auto Enumerate(std::initializer_list<T> &&Args, size_t Start = 0)
    {
        // This should be optimized away..
        struct Countediterator_t
        {
            size_t Index, Size;
            Iter Iterator;

            bool operator !=(const std::default_sentinel_t &Right) const { return !!Size; }
            auto operator *() const { return std::tie(Index, *Iterator); }
            void operator ++() { ++Index; --Size; ++Iterator; }
        };
        struct Wrapper_t
        {
            size_t Start;
            std::initializer_list<T> Iterable;

            auto begin() { return Countediterator_t{ Start, Iterable.size(), Iterable.begin() }; }
            static auto end() { return std::default_sentinel_t{}; }
        };

        return Wrapper_t(Start, Args);
    }

    // for (const auto &x : Range(1, 100, 2)) = { 1, 3, 5 ... }
    template <typename Valuetype, typename Steptype = int>
    constexpr auto Range(Valuetype Start, Valuetype Stop, Steptype Stepsize = 1)
    {
        // This should be optimized away..
        struct Iterator_t
        {
            Valuetype Current, Limit;
            Steptype Step;

            bool operator !=(const std::default_sentinel_t &Right) const { return Current != Limit; }
            auto operator *() const { return Current; }
            void operator ++()
            {
                if constexpr (std::is_arithmetic_v<Valuetype>) Current += Step;
                else std::advance(Current, Step);
            }
        };
        struct Wrapper_t
        {
            Valuetype Start, Stop;
            Steptype Stepsize;

            auto begin() { return Iterator_t{ Start, Stop, Stepsize }; }
            static auto end() { return std::default_sentinel_t{}; }
        };

        // Infinite loop..
        assert(Stepsize != 0);

        return Wrapper_t{ Start, Stop, Stepsize };
    }

    #pragma endregion

    // NOTE(tcn): Just until STL supports ranges::zip, a naive implementation returning references.
    // std::vector a{ 1, 2, 3 }, b{ 4, 5, 6 }, c{ 7, 8, 9 };
    // for (auto [A, B, C] : Zip(a, b, c)) { A = C; }
    constexpr auto Zip(auto &...Args)
    {
        struct Tupleiterator_t
        {
            size_t Size;
            std::tuple<decltype(Args.begin())...> Iterators;

            bool operator !=(const std::default_sentinel_t &Right) const { return !!Size; }
            void operator ++() { --Size; std::apply([](auto &...x) { (..., std::advance(x, 1)); }, Iterators); }

            auto operator *()
            {
                return[&]<size_t ...Index>(std::index_sequence<Index...>)
                {
                    return std::forward_as_tuple((*std::get<Index>(Iterators))...);
                }(std::make_index_sequence<sizeof...(Args)>());
            }
        };
        struct Wrapper_t
        {
            size_t Size;
            std::tuple<decltype(Args.begin())...> Iterators;

            auto begin() { return Tupleiterator_t{ Size, Iterators }; }
            static auto end() { return std::default_sentinel_t{}; }
        };

        auto Min = [](auto &&Self, auto a, auto b, auto... x)
        {
            if constexpr (sizeof...(x) == 0)
                return a < b ? a : b;
            else
                return Self(Self, Self(Self, a, b), x...);
        };

        return Wrapper_t{ Min(Min, (Args.size())...), { Args.begin()... } };
    }

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
    }
    #endif

    // Simple PRNG to avoid OS dependencies.
    namespace RNG
    {
        // Xoroshiro128+
        template <typename T = uint64_t> T Next()
        {
            static uint64_t Table[2]{ __rdtsc(), ~(__rdtsc()) };
            const uint64_t Result = Table[0] + Table[1];
            const uint64_t S1 = Table[0] ^ Table[1];

            Table[0] = std::rotl(Table[0], 24) ^ S1 ^ (S1 << 16);
            Table[1] = std::rotl(S1, 37);

            if constexpr (std::is_same_v<T, bool>) return Result & 1;
            if constexpr (sizeof(T) == sizeof(uint64_t)) return std::bit_cast<T>(Result);
            else
            {
                T Temp{};
                std::memcpy(&Temp, &Result, std::min(sizeof(T), sizeof(uint64_t)));

                return Temp;
            }
        }
    }
#pragma endregion
