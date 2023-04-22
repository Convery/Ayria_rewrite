/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-06-23
    License: MIT

    Helpers to get around constexpr limitations / tricking the compiler.
*/

#pragma once
#include <array>
#include <ranges>
#include <cstdint>
#include <concepts>
#include <algorithm>

#include "Datatypes.hpp"

// Lookups do not work properly in msvc, copy this into each module that needs it.
#pragma region Copy_me

// Combine arrays, no idea why the STL doesn't provide this..
template <typename T, size_t N, size_t M>
constexpr std::array<T, N + M> operator+(const std::array<T, N> &Left, const std::array<T, M> &Right)
{
    return[]<size_t ...LIndex, size_t ...RIndex>(const std::array<T, N> &Left, const std::array<T, M> &Right,
        std::index_sequence<LIndex...>, std::index_sequence<RIndex...>)
    {
        return std::array<T, N + M>{ { Left[LIndex]..., Right[RIndex]... } };
    }(Left, Right, std::make_index_sequence<N>(), std::make_index_sequence<M>());
}
#pragma endregion

// Compile-time
namespace cmp
{
    #pragma region Concepts
    template <typename T> concept Byte_t = sizeof(T) == 1;
    template <typename T> concept Range_t = requires (T t) { t.begin(); t.end(); typename T::value_type; };
    template <typename T> concept Sequential_t = requires (T t) { t.data(); t.size(); typename T::value_type; };
    template <typename T> concept Char_t = std::is_same_v<std::remove_const_t<T>, char> || std::is_same_v<std::remove_const_t<T>, char8_t>;
    template <typename T> concept WChar_t =  std::is_same_v<std::remove_const_t<T>, wchar_t>  || std::is_same_v<std::remove_const_t<T>, char16_t>;

    // Fully typed instantiation.
    template <typename Type, template <typename ...> class Template> constexpr bool isDerived = false;
    template <template <typename ...> class Template, typename... Types> constexpr bool isDerived<Template<Types...>, Template> = true;

    // Partially typed instantiation.
    template <typename Type, template <typename, auto> class Template> constexpr bool isDerivedEx = false;
    template <template <typename, auto> class Template, typename T, auto A> constexpr bool isDerivedEx<Template<T, A>, Template> = true;

    // For metaprogramming / P2593
    template <typename ...> constexpr bool always_true = true;
    template <typename ...> constexpr bool always_false = false;
    #pragma endregion

    #pragma region Typeselection
    // Helper to avoid nested conditionals in templates.
    template <bool Conditional, typename T> struct Case_t : std::bool_constant<Conditional> { using type = T; };
    using Defaultcase_t = Case_t<false, void>;

    // Get the smallest type that can hold our value.
    template <uint64_t Maxvalue> using minInt_t = typename std::disjunction<
        Case_t<(std::bit_width(Maxvalue) > 32), uint64_t>,
        Case_t<(std::bit_width(Maxvalue) > 16), uint32_t>,
        Case_t<(std::bit_width(Maxvalue) >  8), uint16_t>,
        Case_t<(std::bit_width(Maxvalue) <= 8), uint8_t> >::type;
    #pragma endregion

    #pragma region Array
    // Sometimes we just need a bit more room..
    template <typename T, size_t Old, size_t New>
    constexpr std::array<T, New> resize_array(const std::array<T, Old> &Input)
    {
        constexpr auto Min = std::min(Old, New);

        return[]<size_t ...Index>(const std::array<T, Old> &Input, std::index_sequence<Index...>)
        {
            return std::array<T, New>{ { Input[Index]... }};
        }(Input, std::make_index_sequence<Min>());
    }

    // All this just to convert a string literal to an array without the null byte.
    template <typename T, size_t N> constexpr auto toArray(const T(&Input)[N])
    {
        return[]<size_t ...Index>(const T(&Input)[N], std::index_sequence<Index...>)
        {
            return std::array<T, N - 1>{ { Input[Index]... }};
        }(Input, std::make_index_sequence<N - 1>());
    }
    #pragma endregion

    #pragma region Memory
    // Since we can't cast (char *) <-> (void *) in constexpr.
    template <Byte_t T, Byte_t U> constexpr bool memcmp(const T *A, const U *B, size_t Size)
    {
        if (!std::is_constant_evaluated())
        {
            return 0 == std::memcmp(A, B, Size);
        }
        else
        {
            while (Size--)
            {
                if ((U(*A++) ^ T(*B++)) != T{}) [[unlikely]]
                    return false;
            }

            return true;
        }
    }

    // Memcpy accepts the size in bytes in-case we want partial copies.
    template <typename T, typename U> constexpr void memcpy(T *Dst, const U *Src, size_t Sizebytes)
    {
        if (!std::is_constant_evaluated())
        {
            std::memcpy(Dst, Src, Sizebytes);
        }
        else
        {
            if constexpr (sizeof(T) == sizeof(U))
            {
                while (Sizebytes)
                {
                    *Dst++ = std::bit_cast<T>(*Src++);
                    Sizebytes -= sizeof(T);
                }
            }
            else if constexpr (sizeof(T) == 1)
            {
                const auto Temp = std::bit_cast<std::array<uint8_t, sizeof(U)>>(*Src);
                const auto Step = std::min(sizeof(U), Sizebytes);

                for (size_t i = 0; i < Step; ++i)
                    *Dst++ = Temp[i];

                if (Sizebytes -= Step) return memcpy(Dst, ++Src, Sizebytes);
            }
            else if constexpr (sizeof(U) == 1)
            {
                const auto Step = std::min(sizeof(T), Sizebytes);
                std::array<uint8_t, sizeof(T)> Temp{};

                for (size_t i = 0; i < Step; ++i)
                    Temp[i] = uint8_t(*Src++);

                *Dst++ = std::bit_cast<T>(Temp);
                if (Sizebytes -= Step) return memcpy(Dst, Src, Sizebytes);
            }
            else
            {
                static_assert(always_false<T>, "memcpy needs the types to be the same size, or one being 1 byte aligned");
            }
        }
    }
    template <typename T, typename U> constexpr void memcpy(T *Dst, const U &Src)
    {
        if constexpr (sizeof(T) == sizeof(U))
        {
            *Dst = std::bit_cast<T>(Src);
        }
        else if constexpr (sizeof(T) == 1 || sizeof(U) == 1)
        {
            return memcpy(Dst, &Src, sizeof(U) >= sizeof(T) ? sizeof(U) : sizeof(T));
        }
        else
        {
            const auto A = std::bit_cast<std::array<uint8_t, sizeof(U)>>(Src);
            auto B = std::bit_cast<std::array<uint8_t, sizeof(T)>>(*Dst);

            const auto Min = sizeof(U) <= sizeof(T) ? sizeof(U) : sizeof(T);
            for (size_t i = 0; i < Min; ++i) B[i] = A[i];

            *Dst = std::bit_cast<T>(B);
        }


    }
    #pragma endregion

    #pragma region Typewrapper
    // Generic container convertible to std::array or std::basic_string depending on usage.
    template <typename T, size_t N> using IntelisenseHack_t = std::array<uint8_t, N * sizeof(T)>;
    template <typename T, size_t N> struct Container_t : IntelisenseHack_t<T, N>
    {
        using Basetype = std::array<uint8_t, N * sizeof(T)>;

        using const_reverse_iterator = typename Basetype::const_reverse_iterator;
        using reverse_iterator = typename Basetype::reverse_iterator;
        using difference_type = typename Basetype::difference_type;
        using const_reference = typename Basetype::const_reference;
        using const_iterator = typename Basetype::const_iterator;
        using const_pointer = typename Basetype::const_pointer;
        using value_type = typename Basetype::value_type;
        using size_type = typename Basetype::size_type;
        using reference = typename Basetype::reference;
        using iterator = typename Basetype::iterator;
        using pointer = typename Basetype::pointer;

        using Basetype::operator=;
        using Basetype::cbegin;
        using Basetype::begin;
        using Basetype::size;
        using Basetype::cend;
        using Basetype::data;
        using Basetype::end;

        // Need to overload the members for constexpr.
        constexpr void resize(size_t Elements) {}
        constexpr bool empty() const noexcept
        {
            return std::ranges::all_of(*this, [](const uint8_t &Item) { return Item == uint8_t{}; });
        }

        // Need explicit casts for anything other than std::array
        template <typename U> constexpr operator std::span<U, std::dynamic_extent>() const
        {
            return { (U *)data(), size() / sizeof(U)};
        }
        template <typename U> requires (U(data(), size())) constexpr operator U() const
        {
            return { data(), size() };
        }
        template <typename U> requires (U(begin(), end())) constexpr operator U() const
        {
            return { begin(), end() };
        }
        template <typename U> constexpr operator std::array<U, N / sizeof(U)>() const
        {
            return std::bit_cast<std::array<U, N / sizeof(U)>>(*this);
        }
        template <typename U> constexpr operator std::basic_string<U>() const
        {
            // Typesafety..
            if (std::is_constant_evaluated())
            {
                const auto Temp = std::bit_cast<std::array<U, N / sizeof(U)>>(*this);
                auto This = std::basic_string<U>{ Temp.data(), Temp.size() };
                while (This.back() == U{}) This.pop_back();
                return This;
            }
            else
            {
                auto This = std::basic_string<U>{ (U *)data(), size() / sizeof(U) };
                while (This.back() == U{}) This.pop_back();
                return This;
            }
        }
        constexpr operator std::span<uint8_t, N * sizeof(T)>()
        {
            return std::span<uint8_t, N * sizeof(T)>(Basetype::data(), Basetype::size());
        }
        constexpr const_reference operator[](size_t Index) const
        {
            if (std::is_constant_evaluated())
            {
                return data()[Index];
            }
            else
                return Basetype::operator[](Index);
        }
        constexpr reference operator[](size_t Index)
        {
            if (std::is_constant_evaluated())
            {
                return data()[Index];
            }
            else
                return Basetype::operator[](Index);
        }

        using Basetype::array;
        constexpr Container_t() : Basetype::array() {}
        constexpr Container_t(Container_t &&) = default;
        constexpr Container_t(const Container_t &) = default;
        constexpr Container_t(const std::array<uint8_t, N * sizeof(T)> &Array) : Container_t()
        {
            cmp::memcpy(data(), Array.data(), N * sizeof(T));
        }

        constexpr Container_t(Blob_t &&Blob) : Container_t()
        {
            cmp::memcpy(data(), Blob.data(), std::min(size(), std::size(Blob)));
        }
        constexpr Container_t(const Blob_t &Blob) : Container_t()
        {
            cmp::memcpy(data(), Blob.data(), std::min(size(), std::size(Blob)));
        }
        template <cmp::Range_t U> constexpr Container_t(const U &Range) : Container_t()
        {
            cmp::memcpy(data(), Range.data(), std::min(size(), std::size(Range)));
        }
        template <cmp::Byte_t U> constexpr Container_t(const U *Buffer, size_t Length) : Container_t()
        {
            cmp::memcpy(data(), Buffer, std::min(size(), Length));
        }
    };
    template <typename T> struct Container_t<T, std::dynamic_extent> : std::basic_string<uint8_t>
    {
        using Basetype = std::basic_string<uint8_t>;

        using const_reverse_iterator = Basetype::const_reverse_iterator;
        using reverse_iterator = Basetype::reverse_iterator;
        using difference_type = Basetype::difference_type;
        using const_reference = Basetype::const_reference;
        using const_iterator = Basetype::const_iterator;
        using const_pointer = Basetype::const_pointer;
        using value_type = Basetype::value_type;
        using size_type = Basetype::size_type;
        using reference = Basetype::reference;
        using iterator = Basetype::iterator;
        using pointer = Basetype::pointer;

        //using Basetype::operator[];
        using Basetype::operator+=;
        using Basetype::cbegin;
        using Basetype::resize;
        using Basetype::substr;
        using Basetype::begin;
        using Basetype::c_str;
        using Basetype::empty;
        using Basetype::cend;
        using Basetype::data;
        using Basetype::size;
        using Basetype::end;

        // Need explicit casts for anything other than std::basic_string
        template <typename U> constexpr operator std::span<U, std::dynamic_extent>() const
        {
            return { (U *)data(), size() / sizeof(U)};
        }
        template <typename U> requires (U(data(), size())) constexpr operator U() const
        {
            return { data(), size() };
        }
        template <typename U> requires (U(begin(), end())) constexpr operator U() const
        {
            return { begin(), end() };
        }
        template <cmp::Byte_t U> constexpr operator std::basic_string<U>() const
        {
            return { begin(), end() };
        }
        constexpr const_reference operator[](size_t Index) const
        {
            if (std::is_constant_evaluated())
            {
                return data()[Index];
            }
            else
                return Basetype::operator[](Index);
        }
        constexpr reference operator[](size_t Index)
        {
            if (std::is_constant_evaluated())
            {
                return data()[Index];
            }
            else
                return Basetype::operator[](Index);
        }

        using Basetype::basic_string;
        constexpr Container_t() : Basetype::basic_string() {}
        constexpr Container_t(Container_t &&) = default;
        constexpr Container_t(const Container_t &) = default;

        constexpr Container_t(size_t Size) : Container_t()
        {
            Basetype::resize(Size);
        }
        constexpr Container_t(Blob_t &&Blob) : Container_t()
        {
            Basetype::resize(Blob.size());
            cmp::memcpy(data(), Blob.data(), std::min(size(), std::size(Blob)));
        }
        constexpr Container_t(const Blob_t &Blob) : Container_t()
        {
            Basetype::resize(Blob.size());
            cmp::memcpy(data(), Blob.data(), std::min(size(), std::size(Blob)));
        }
        template <cmp::Range_t U> constexpr Container_t(const U &Range) : Container_t()
        {
            const auto Wantedsize = sizeof(typename U::value_type) * std::size(Range);

            Basetype::resize(Wantedsize);
            cmp::memcpy(data(), Range.data(), std::size(Range));
        }
        template <cmp::Byte_t U> constexpr Container_t(const U *Buffer, size_t Length) : Container_t()
        {
            const auto Wantedsize = sizeof(U) * Length;

            Basetype::resize(Wantedsize);
            cmp::memcpy(data(), Buffer, Length);
        }
    };

    // Can't really do friend operators..
    template <typename T, size_t N, size_t M>
    constexpr bool operator==(const Container_t<T, N> &Left, const Container_t<T, M> &Right) noexcept
    {
        if (Left.size() != Right.size()) return false;
        return cmp::memcmp(Left.data(), Right.data(), Left.size());
    }
    template <typename T, size_t N, size_t M> requires ((N > 0 && N != std::dynamic_extent) && (M > 0 && M != std::dynamic_extent))
    constexpr Container_t<T, N + M> operator+(const Container_t<T, N> &Left, const Container_t<T, M> &Right)
    {
        return[]<size_t ...LIndex, size_t ...RIndex>(const Container_t<T, N> &Left, const Container_t<T, M> &Right,
            std::index_sequence<LIndex...>, std::index_sequence<RIndex...>)
        {
            return Container_t<T, N + M>{ { Left[LIndex]..., Right[RIndex]... } };
        }(Left, Right, std::make_index_sequence<N>(), std::make_index_sequence<M>());
    }

    // Some helpful naming conventions.
    template <typename T, size_t N> using Array_t = Container_t<T, N>;
    template <typename T, size_t N = std::dynamic_extent> using Vector_t = Container_t<T, N>;
    #pragma endregion

    #pragma region Bytearray

    // Get the underlying bytes of an object / range in constexpr mode.
    template <typename T> requires(!Range_t<T>) constexpr auto getBytes(const T &Value)
    {
        return std::bit_cast<std::array<uint8_t, sizeof(T)>>(Value);
    }
    template <Byte_t T, size_t N> constexpr auto getBytes(const std::array<T, N> &Value)
    {
        return[]<size_t ...Index>(const std::array<T, N> &Input, std::index_sequence<Index...>)
        {
            return Array_t<uint8_t, N>{ { uint8_t(Input[Index])... }};
        }(Value, std::make_index_sequence<N>());
    }
    template <size_t N = std::dynamic_extent> constexpr auto getBytes(std::span<const uint8_t, N> Input) { return Input; }
    template <Range_t T> requires(std::extent<T>() == 0 && std::is_constant_evaluated()) constexpr auto getBytes(const T &Range)
    {
        Blob_t Buffer{}; Buffer.reserve(Range.size() * sizeof(typename T::value_type));

        for (const auto &Item : Range)
        {
            const auto Local = std::bit_cast<std::array<uint8_t, sizeof(typename T::value_type)>>(Item);
            Buffer.append(Local.data(), sizeof(typename T::value_type));
        }

        return Buffer;
    }
    template <Range_t T> requires(std::extent<T>() == 0 && !std::is_constant_evaluated()) constexpr auto getBytes(const T &Range)
    {
        return { reinterpret_cast<const uint8_t *>(Range.data()), std::extent<T>() * sizeof(typename T::value_type) };
    }
    template <Range_t T> requires(std::is_same_v<std::decay<typename T::value_type>, uint8_t>) constexpr auto getBytes(const T &Range)
    {
        return Range;
    }
    template <Range_t T> requires(std::extent<T>() != 0 && sizeof(typename T::value_type) != 1) constexpr auto getBytes(const T &Range)
    {
        std::array<uint8_t, std::extent<T>() * sizeof(typename T::value_type)> Buffer{};
        cmp::memcpy(Buffer.data(), Range.data(), std::extent<T>() * sizeof(typename T::value_type));

        return Buffer;
    }


    #pragma endregion

    #pragma region Endian
    // Convert to a different endian (half are NOP on each system).
    template <std::integral T> constexpr T toLittle(T Value)
    {
        if constexpr (std::endian::native == std::endian::big)
            return std::byteswap(Value);
        else
            return Value;
    }
    template <std::integral T> constexpr T toBig(T Value)
    {
        if constexpr (std::endian::native == std::endian::little)
            return std::byteswap(Value);
        else
            return Value;
    }
    template <std::integral T> constexpr T fromLittle(T Value)
    {
        if constexpr (std::endian::native == std::endian::big)
            return std::byteswap(Value);
        else
            return Value;
    }
    template <std::integral T> constexpr T fromBig(T Value)
    {
        if constexpr (std::endian::native == std::endian::little)
            return std::byteswap(Value);
        else
            return Value;
    }

    template <std::floating_point T> constexpr auto toInt(T Value)
    {
        if constexpr (sizeof(T) == 8) return std::bit_cast<uint64_t>(Value);
        if constexpr (sizeof(T) == 4) return std::bit_cast<uint32_t>(Value);

        // For when std::(b)float16_t is added.
        if constexpr (sizeof(T) == 2) return std::bit_cast<uint16_t>(Value);

        // All paths need to return something.
        return std::bit_cast<uint8_t>(Value);
    }
    template <std::floating_point T> constexpr T toLittle(T Value)
    {
        if constexpr (std::endian::native == std::endian::big)
            return std::bit_cast<T>(std::byteswap(toInt(Value)));
        else
            return Value;
    }
    template <std::floating_point T> constexpr T toBig(T Value)
    {
        if constexpr (std::endian::native == std::endian::little)
            return std::bit_cast<T>(std::byteswap(toInt(Value)));
        else
            return Value;
    }
    template <std::floating_point T> constexpr T fromLittle(T Value)
    {
        if constexpr (std::endian::native == std::endian::big)
            return std::bit_cast<T>(std::byteswap(toInt(Value)));
        else
            return Value;
    }
    template <std::floating_point T> constexpr T fromBig(T Value)
    {
        if constexpr (std::endian::native == std::endian::little)
            return std::bit_cast<T>(std::byteswap(toInt(Value)));
        else
            return Value;
    }
    #pragma endregion

    #pragma region Math

    // Opt for branchless versions.
    template <typename T> constexpr T abs(const T &Value)
    {
        if constexpr (std::signed_integral<T>)
        {
            const auto Mask = Value >> (sizeof(T) * 8 - 1);
            return (Value + Mask) ^ Mask;
        }
        else
        {
            return Value * ((Value > 0) - (Value < 0));
        }
    }
    template <typename T> constexpr T min(const T &A, const T &B)
    {
        if constexpr (std::signed_integral<T>)
        {
            return A + ((B - A) & ((B - A) >> (sizeof(T) * 8 - 1)));
        }
        else
        {
            return (A < B) * A + (B <= A) * B;
        }
    }
    template <typename T> constexpr T max(const T &A, const T &B)
    {
        if constexpr (std::signed_integral<T>)
        {
            return A - ((A - B) & ((A - B) >> (sizeof(T) * 8 - 1)));
        }
        else
        {
            return (A > B) * A + (B >= A) * B;
        }
    }
    template <typename T> constexpr T min(std::initializer_list<T> &&Items)
    {
        T Min = 0;
        for (const auto &Item : Items)
            Min = min(Min, Item);
        return Min;
    }
    template <typename T> constexpr T max(std::initializer_list<T> &&Items)
    {
        T Max = 0;
        for (const auto &Item : Items)
            Max = max(Max, Item);
        return Max;
    }
    template <typename T> constexpr T clamp(const T &Value, const T &Min, const T &Max)
    {
        return cmp::max(Min, cmp::min(Value, Max));
    }

    // Plumbing, recursion makes MSVC upset..
    template <typename T, std::integral U> constexpr T pow_int(T Base, U Exponent)
    {
        if (Exponent == 0) return 1;
        if (Exponent < 0) return pow_int(1 / Base, -Exponent);

        long double Result{ 1.0 };
        while (Exponent)
        {
            if (Exponent & 1) Result *= Base;
            Exponent >>= 1;
            Base *= Base;
        }

        return T(Result);
    }

    // As we are only doing this in constexpr, we can use a series.
    constexpr auto Taylorsteps = 512;
    template <std::floating_point T = double> constexpr T log(const T &Value)
    {
        // Prefer optimized implementations.
        if (!std::is_constant_evaluated())
        {
            return std::log(Value);
        }

        // Undefined for negative values.
        if (Value < T{ 0.0 }) return std::numeric_limits<T>::quiet_NaN();

        // Taylor series.
        long double Sum{}, Term{ (Value - 1) / (Value + 1) };
        const long double Squared{ Term * Term };
        for (int i = 0; i < Taylorsteps; ++i)
        {
            Sum += Term / (2 * i + 1);
            Term *= Squared;
        }

        return T(2 * Sum);
    }
    template <std::floating_point T = double> constexpr T exp(const T &Value)
    {
        // Prefer optimized implementations.
        if (!std::is_constant_evaluated())
        {
            return std::exp(Value);
        }

        // Check if an integer got promoted.
        if (Value == T(int64_t(Value)))
        {
            constexpr auto Euler = 2.7182818284590452353602874713527L;
            return pow_int(Euler, int64_t(Value));
        }

        // Taylor series.
        long double Sum{ 1.0 }, Term{ 1.0 };
        for (int i = 1; i < Taylorsteps; ++i)
        {
            Term *= Value / i;
            Sum += Term;
        }
        return T(Sum);
    }
    template <std::floating_point T = double> constexpr T pow(const T &Base, const double &Exponent)
    {
        // Prefer optimized implementations.
        if (!std::is_constant_evaluated())
        {
            return std::pow(Base, Exponent);
        }

        // Check if an integer got promoted.
        if (Exponent == T(int64_t(Exponent)))
        {
            return pow_int(Base, int64_t(Exponent));
        }
        else
        {
            return exp(Exponent * log(Base));
        }
    }

    #pragma endregion
}

// Additions for old STL versions.
#pragma region STL

// Will be added in MSVC 17.1, copied from MS-STL.
#if !defined (__cpp_lib_byteswap)
#define __cpp_lib_byteswap
namespace std
{
    [[nodiscard]] constexpr uint32_t _Byteswap_ulong(const uint32_t _Val) noexcept
    {
        if (std::is_constant_evaluated())
        {
            return (_Val << 24) | ((_Val << 8) & 0x00FF'0000) | ((_Val >> 8) & 0x0000'FF00) | (_Val >> 24);
        }
        else
        {
            #if defined (_MSC_VER)
            return _byteswap_ulong(_Val);
            #else
            return __builtin_bswap32(_Val);
            #endif
        }
    }
    [[nodiscard]] constexpr uint16_t _Byteswap_ushort(const uint16_t _Val) noexcept
    {
        if (std::is_constant_evaluated())
        {
            return static_cast<uint16_t>((_Val << 8) | (_Val >> 8));
        }
        else
        {
            #if defined (_MSC_VER)
            return _byteswap_ushort(_Val);
            #else
            return __builtin_bswap16(_Val);
            #endif
        }
    }
    [[nodiscard]] constexpr uint64_t _Byteswap_uint64(const uint64_t _Val) noexcept
    {
        if (std::is_constant_evaluated())
        {
            return (_Val << 56) | ((_Val << 40) & 0x00FF'0000'0000'0000) | ((_Val << 24) & 0x0000'FF00'0000'0000)
                | ((_Val << 8) & 0x0000'00FF'0000'0000) | ((_Val >> 8) & 0x0000'0000'FF00'0000)
                | ((_Val >> 24) & 0x0000'0000'00FF'0000) | ((_Val >> 40) & 0x0000'0000'0000'FF00) | (_Val >> 56);
        }
        else
        {
            #if defined (_MSC_VER)
            return _byteswap_uint64(_Val);
            #else
            return __builtin_bswap64(_Val);
            #endif
        }
    }
    template <std::integral _Ty> [[nodiscard]] constexpr _Ty byteswap(const _Ty _Val) noexcept
    {
        if constexpr (sizeof(_Ty) == 1) return _Val;
        else if constexpr (sizeof(_Ty) == 2) return static_cast<_Ty>(_Byteswap_ushort(static_cast<uint16_t>(_Val)));
        else if constexpr (sizeof(_Ty) == 4) return static_cast<_Ty>(_Byteswap_ulong(static_cast<uint32_t>(_Val)));
        else if constexpr (sizeof(_Ty) == 8) return static_cast<_Ty>(_Byteswap_uint64(static_cast<uint64_t>(_Val)));

        // 128-bit is not supported.
        else static_assert(cmp::always_false<_Ty>, "Unexpected integer size");
    }
}
#endif

#pragma endregion
