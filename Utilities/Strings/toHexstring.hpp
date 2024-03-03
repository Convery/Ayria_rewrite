/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-07-03
    License: MIT
*/

#pragma once
#include <Utilities/Utilities.hpp>

// U suffix for uppercase version.
namespace Encoding
{
    template <typename T, size_t N> constexpr std::string toHexstring(const std::array<T, N> &Input, bool Spaced = false)
    {
        constexpr std::array<char, 16> Mapping{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
        std::string Output{}; Output.reserve(sizeof(T) * Input.size() * (2 + (Spaced ? 1 : 0)));

        for (const auto &Item : Input)
        {
            const auto Bytes = std::bit_cast<std::array<uint8_t, sizeof(typename T::value_type)>>(Item);
            for (const auto Byte : Bytes)
            {
                Output += Mapping[(uint8_t(Byte) & 0xF0) >> 4];
                Output += Mapping[uint8_t(Byte) & 0x0F];
                if (Spaced) Output += ' ';
            }
        }

        while (Output.back() == ' ')
            Output.pop_back();

        return Output;
    }
    template <typename T, size_t N> constexpr std::string toHexstringU(const std::array<T, N> &Input, bool Spaced = false)
    {
        constexpr std::array<char, 16> Mapping{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
        std::string Output{}; Output.reserve(sizeof(T) * Input.size() * (2 + (Spaced ? 1 : 0)));

        for (const auto &Item : Input)
        {
            const auto Bytes = std::bit_cast<std::array<uint8_t, sizeof(typename T::value_type)>>(Item);
            for (const auto Byte : Bytes)
            {
                Output += Mapping[(uint8_t(Byte) & 0xF0) >> 4];
                Output += Mapping[uint8_t(Byte) & 0x0F];
                if (Spaced) Output += ' ';
            }
        }

        while (Output.back() == ' ')
            Output.pop_back();

        return Output;
    }

    template <cmp::Range_t T> requires (sizeof(typename T::value_type) != 1) constexpr std::string toHexstring(const T &Input, bool Spaced = false)
    {
        constexpr std::array<char, 16> Mapping{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
        std::string Output{}; Output.reserve(sizeof(typename T::value_type) * Input.size() * (2 + (Spaced ? 1 : 0)));

        for (const auto &Item : Input)
        {
            const auto Bytes = std::bit_cast<std::array<uint8_t, sizeof(typename T::value_type)>>(Item);
            for (const auto Byte : Bytes)
            {
                Output += Mapping[(uint8_t(Byte) & 0xF0) >> 4];
                Output += Mapping[uint8_t(Byte) & 0x0F];
                if (Spaced) Output += ' ';
            }
        }

        while (Output.back() == ' ')
            Output.pop_back();

        return Output;
    }
    template <cmp::Range_t T> requires (sizeof(typename T::value_type) != 1) constexpr std::string toHexstringU(const T &Input, bool Spaced = false)
    {
        constexpr std::array<char, 16> Mapping{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
        std::string Output{}; Output.reserve(sizeof(typename T::value_type) * Input.size() * (2 + (Spaced ? 1 : 0)));

        for (const auto &Item : Input)
        {
            const auto Bytes = std::bit_cast<std::array<uint8_t, sizeof(typename T::value_type)>>(Item);
            for (const auto Byte : Bytes)
            {
                Output += Mapping[(uint8_t(Byte) & 0xF0) >> 4];
                Output += Mapping[uint8_t(Byte) & 0x0F];
                if (Spaced) Output += ' ';
            }
        }

        while (Output.back() == ' ')
            Output.pop_back();

        return Output;
    }


    template <cmp::Sequential_t T> requires (sizeof(typename T::value_type) == 1) constexpr std::string toHexstring(const T &Input, bool Spaced = false)
    {
        constexpr std::array<char, 16> Mapping{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
        std::string Output{}; Output.reserve(Input.size() * (2 + (Spaced ? 1 : 0)));

        for (const auto Item : Input)
        {
            Output += Mapping[(uint8_t(Item) & 0xF0) >> 4];
            Output += Mapping[uint8_t(Item) & 0x0F];
            if (Spaced) Output += ' ';
        }

        while (Output.back() == ' ')
            Output.pop_back();

        return Output;
    }
    template <cmp::Sequential_t T> requires (sizeof(typename T::value_type) == 1) constexpr std::string toHexstringU(const T &Input, bool Spaced = false)
    {
        constexpr std::array<char, 16> Mapping{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
        std::string Output{}; Output.reserve(Input.size() * (2 + (Spaced ? 1 : 0)));

        for (const auto Item : Input)
        {
            Output += Mapping[(uint8_t(Item) & 0xF0) >> 4];
            Output += Mapping[uint8_t(Item) & 0x0F];
            if (Spaced) Output += ' ';
        }

        while (Output.back() == ' ')
            Output.pop_back();

        return Output;
    }
}
