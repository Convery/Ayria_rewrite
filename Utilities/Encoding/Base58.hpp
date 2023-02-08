/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-06-23
    License: MIT

    Using Bitcoins charset.
*/

#pragma once
#include <Utilities/Utilities.hpp>

namespace Base58
{
    constexpr size_t Decodesize(size_t N)
    {
        if (N == std::dynamic_extent)
            return std::dynamic_extent; return size_t(N * size_t{ 733 } / 1000.0f + 0.5f);
    }
    constexpr size_t Encodesize(size_t N)
    {
        if (N == std::dynamic_extent)
            return std::dynamic_extent; return size_t(N * size_t{ 137 } / 100.0f + 0.5f);
    }

    namespace Internal
    {
        static constexpr auto Table = std::to_array<uint8_t>({
            '1', '2', '3', '4', '5', '6', '7', '8', '9',
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J',
            'K', 'L', 'M', 'N', 'P', 'Q', 'R', 'S', 'T',
            'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c',
            'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'm',
            'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
            'w', 'x', 'y', 'z'
        });
        static constexpr auto Reversetable = std::to_array<uint8_t>({
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0xFF, 0x11, 0x12, 0x13, 0x14, 0x15, 0xFF,
            0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0xFF, 0x2C, 0x2D, 0x2E,
            0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        });

        // std::array at compiletime, std::basic_string at runtime.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N != std::dynamic_extent) constexpr auto Encode(std::span<const T, N> Input)
        {
            std::array<uint8_t, Encodesize(N)> Result{}, Buffer{};
            size_t Outputposition{ 1 };
            size_t Leadingzeros{};

            for (const auto &Item : Input)
            {
                // Count leading zeros.
                if (Item == T()) Leadingzeros++;
                auto Local = static_cast<uint32_t>(Item);

                for (size_t c = 0; c < Outputposition; c++)
                {
                    Local += static_cast<uint32_t>(uint8_t(Buffer[c]) << 8);
                    Buffer[c] = static_cast<uint8_t>(Local % 58);
                    Local /= 58;
                }

                while (Local)
                {
                    Buffer[Outputposition++] = static_cast<uint8_t>(Local % 58);
                    Local /= 58;
                }
            }

            // Count leading zeros.
            for (size_t i = 0; i < Leadingzeros; ++i)
                Buffer[i] = uint8_t('1');

            // Reverse emplace from the map.
            for (size_t i = 0; i < Outputposition; ++i)
            {
                Result[Leadingzeros + i] = Internal::Table[(size_t)Buffer[Outputposition - 1 - i]];
            }

            return Result;
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N == std::dynamic_extent) constexpr auto Encode(std::span<const T, N> Input)
        {
            std::basic_string<uint8_t> Result{}, Buffer((Encodesize(Input.size())), uint8_t{});
            size_t Outputposition{ 1 };
            size_t Leadingzeros{};

            for (const auto &Item : Input)
            {
                // Count leading zeros.
                if (Item == T()) Leadingzeros++;
                auto Local = static_cast<uint32_t>(Item);

                for (size_t c = 0; c < Outputposition; c++)
                {
                    Local += static_cast<uint32_t>(uint8_t(Buffer[c]) << 8);
                    Buffer[c] = static_cast<uint8_t>(Local % 58);
                    Local /= 58;
                }

                while (Local)
                {
                    Buffer[Outputposition++] = static_cast<uint8_t>(Local % 58);
                    Local /= 58;
                }
            }

            // Count leading zeros.
            for (size_t i = 0; i < Leadingzeros; ++i)
                Buffer[i] = uint8_t('1');

            // Reverse emplace from the map.
            Result.resize(Leadingzeros + Outputposition);
            for (size_t i = 0; i < Outputposition; ++i)
            {
                Result[Leadingzeros + i] = Internal::Table[(size_t)Buffer[Outputposition - 1 - i]];
            }

            return Result;
        }

        // std::array at compiletime, std::basic_string at runtime.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N != std::dynamic_extent) constexpr auto Decode(std::span<const T, N> Input)
        {
            std::array<uint8_t, Encodesize(N)> Buffer{};
            size_t Outputposition{ 1 };
            size_t Leadingzeros{};

            for (const auto &Item : Input)
            {
                // Count leading zeros.
                if (Item == T('1')) Leadingzeros++;
                auto Local = static_cast<uint32_t>(Internal::Reversetable[size_t(Item & uint8_t(0x7F))]);

                for (size_t c = 0; c < Outputposition; c++)
                {
                    Local += static_cast<uint32_t>(Buffer[c]) * 58;
                    Buffer[c] = static_cast<uint8_t>(Local & 0xFF);
                    Local >>= 8;
                }

                while (Local)
                {
                    Buffer[Outputposition++] = uint8_t(Local & 0xFF);
                    Local >>= 8;
                }
            }

            for (size_t i = 0; i < Leadingzeros; ++i)
                Buffer[Outputposition++] = uint8_t();

            std::ranges::reverse(Buffer.begin(), Buffer.begin() + Outputposition);
            return cmp::resize_array<uint8_t, Encodesize(N), Decodesize(N)>(Buffer);
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N == std::dynamic_extent) constexpr auto Decode(std::span<const T, N> Input)
        {
            std::basic_string<uint8_t> Buffer(Encodesize(Input.size()), uint8_t{});
            size_t Outputposition{ 1 };
            size_t Leadingzeros{};

            for (const auto &Item : Input)
            {
                // Count leading zeros.
                if (Item == T('1')) Leadingzeros++;
                auto Local = static_cast<uint32_t>(Internal::Reversetable[size_t(Item & uint8_t(0x7F))]);

                for (size_t c = 0; c < Outputposition; c++)
                {
                    Local += static_cast<uint32_t>(Buffer[c]) * 58;
                    Buffer[c] = static_cast<uint8_t>(Local & 0xFF);
                    Local >>= 8;
                }

                while (Local)
                {
                    Buffer[Outputposition++] = uint8_t(Local & 0xFF);
                    Local >>= 8;
                }
            }

            for (size_t i = 0; i < Leadingzeros; ++i)
                Buffer[i] = uint8_t();

            std::ranges::reverse(Buffer.begin(), Buffer.begin() + Outputposition);
            Buffer.resize(Decodesize(Input.size()));
            return Buffer;
        }
    }

    // Just verify the characters, not going to bother with length.
    template <cmp::Range_t T> constexpr bool isValid(const T &Input)
    {
        constexpr auto Charset = std::string_view("123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");
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
        return isValid(cmp::toArray(Input));
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
        return Encode(cmp::toArray(Input));
    }
    template <cmp::Char_t T, size_t N> constexpr auto Decode(const T(&Input)[N])
    {
        return Decode(cmp::toArray(Input));
    }
}
