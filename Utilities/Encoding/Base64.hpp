/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-06-23
    License: MIT
*/

#pragma once
#include <Utilities/Utilities.hpp>

namespace Base64
{
    // As we can't really calculate padding when decoding, so we allocate for the worst case.
    constexpr size_t Encodesize(size_t N)
    {
        if (N == std::dynamic_extent)
            return std::dynamic_extent;
        return ((N + 2) / 3) << 2;
    }
    constexpr size_t Decodesize(size_t N)
    {
        if (N == std::dynamic_extent)
            return std::dynamic_extent;
        return ((N * 3) >> 2);
    }

    namespace Internal
    {
        static constexpr auto Table =  std::to_array<uint8_t>({
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K',
            'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
            'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
            'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
            's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2',
            '3', '4', '5', '6', '7', '8', '9', '+', '/'
        });
        static constexpr auto Reversetable = std::to_array<uint8_t>({
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
            64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
            64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
        });

        // Array_t at compiletime, Vector_t at runtime.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N != std::dynamic_extent) constexpr auto Encode(std::span<const T, N> Input)
        {
            cmp::Array_t<uint8_t, Encodesize(N)> Result{};
            size_t Outputposition{};
            uint32_t Accumulator{};
            uint8_t Bits{};

            for (const auto &Item : Input)
            {
                Accumulator = (Accumulator << 8) | uint32_t(Item);

                Bits += 8;
                while (Bits >= 6)
                {
                    Bits -= 6;
                    Result[Outputposition++] = Internal::Table[Accumulator >> Bits & 0x3F];
                }
            }

            if (Bits)
            {
                Accumulator <<= 6 - Bits;
                Result[Outputposition++] = Internal::Table[Accumulator & 0x3F];
            }

            while (Outputposition < Encodesize(N))
                Result[Outputposition++] = uint8_t('=');

            return Result;
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N == std::dynamic_extent) constexpr auto Encode(std::span<const T, N> Input)
        {
            cmp::Vector_t<uint8_t> Result(Encodesize(Input.size()));
            size_t Outputposition{};
            uint32_t Accumulator{};
            uint8_t Bits{};

            for (const auto &Item : Input)
            {
                Accumulator = (Accumulator << 8) | uint32_t(Item);

                Bits += 8;
                while (Bits >= 6)
                {
                    Bits -= 6;
                    Result[Outputposition++] = Internal::Table[Accumulator >> Bits & 0x3F];
                }
            }

            if (Bits)
            {
                Accumulator <<= 6 - Bits;
                Result[Outputposition++] = Internal::Table[Accumulator & 0x3F];
            }

            while (Outputposition < Encodesize(N))
                Result[Outputposition++] = uint8_t('=');

            return Result;
        }

        // Array_t at compiletime, Vector_t at runtime.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N != std::dynamic_extent) constexpr auto Decode(std::span<const T, N> Input)
        {
            cmp::Array_t<uint8_t, Decodesize(N)> Result{};
            size_t Outputposition{};
            uint32_t Accumulator{};
            uint8_t Bits{};

            for (const auto &Item : Input)
            {
                if (Item == T('=')) [[unlikely]] continue;

                Accumulator = (Accumulator << 6) | uint32_t(Internal::Reversetable[uint8_t(Item & T(0x7F))]);
                Bits += 6;

                if (Bits >= 8)
                {
                    Bits -= 8;
                    Result[Outputposition++] = uint8_t((Accumulator >> Bits) & 0xFF);
                }
            }

            return Result;
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N == std::dynamic_extent) constexpr auto Decode(std::span<const T, N> Input)
        {
            cmp::Vector_t<uint8_t> Result(Decodesize(Input.size()));
            size_t Outputposition{};
            uint32_t Accumulator{};
            uint8_t Bits{};

            for (const auto &Item : Input)
            {
                if (Item == T('=')) [[unlikely]] continue;

                Accumulator = (Accumulator << 6) | uint32_t(Internal::Reversetable[uint8_t(Item & T(0x7F))]);
                Bits += 6;

                if (Bits >= 8)
                {
                    Bits -= 8;
                    Result[Outputposition++] = uint8_t((Accumulator >> Bits) & 0xFF);
                }
            }

            while (Result.back() == uint8_t{}) Result.pop_back();
            return Result;
        }
    }

    // Just verify the characters, not going to bother with length.
    template <cmp::Range_t T> constexpr bool isValid(const T &Input)
    {
        constexpr auto Charset = std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=");
        return std::ranges::all_of(Input, [&](auto Char)
        {
            #if defined(__cpp_lib_string_contains)
            return Charset.contains(char(Char));
            #else
            return Charset.find(char(Char)) != Charset.end();
            #endif
        });
    }
    template <cmp::Char_t T, size_t N> constexpr bool isValid(const T(&Input)[N])
    {
        return isValid(cmp::stripNullchar(Input));
    }

    // Dynamically selects the proper storage type.
    template <cmp::Byte_t T, size_t N = std::dynamic_extent> constexpr auto Encode(std::span<const T, N> Input)
    {
        return cmp::Container_t<uint8_t, Encodesize(N)>(Internal::Encode(Input));
    }
    template <cmp::Byte_t T, size_t N = std::dynamic_extent> constexpr auto Decode(std::span<const T, N> Input)
    {
        return cmp::Container_t<uint8_t, Decodesize(N)>(Internal::Decode(Input));
    }

    // Generic encoding / decoding.
    template <cmp::Range_t T> requires(1 == sizeof(typename T::value_type)) constexpr auto Encode(const T &Input)
    {
        return Encode(std::span(Input));
    }
    template <cmp::Range_t T> requires(1 == sizeof(typename T::value_type)) constexpr auto Decode(const T &Input)
    {
        return Decode(std::span(Input));
    }

    // String literal helper.
    template <cmp::Char_t T, size_t N> constexpr auto Encode(const T(&Input)[N])
    {
        return Encode(cmp::stripNullchar(Input));
    }
    template <cmp::Char_t T, size_t N> constexpr auto Decode(const T(&Input)[N])
    {
        return Decode(cmp::stripNullchar(Input));
    }

    // RFC7515, URL compatible charset.
    [[nodiscard]] constexpr std::string toURL(std::string_view Input)
    {
        while (Input.back() == '=')
            Input.remove_suffix(1);

        std::string Result(Input);
        for (auto &Item : Result)
        {
            if (Item == '+') Item = '-';
            if (Item == '/') Item = '_';
        }

        return Result;
    }
    [[nodiscard]] constexpr std::string fromURL(std::string_view Input)
    {
        std::string Result(Input);

        for (auto &Item : Result)
        {
            if (Item == '-') Item = '+';
            if (Item == '_') Item = '/';
        }

        switch (Result.size() & 3)
        {
            case 1: Result += "==="; break;
            case 2: Result += "=="; break;
            case 3: Result += "="; break;
            default: break;
        }

        return Result;
    }
}
