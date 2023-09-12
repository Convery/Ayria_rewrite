/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-06-30
    License: MIT

    quotient Digital Signature Algorithm
    arXiv:1709.03358

    NOTE(tcn):
    Due to the constexpr limitations on unions (__cpp_constexpr < 202002), a tuple-based implementation is needed.
    As such, constexpr evaluation is very slow and limited by /constexpr:steps<NUMBER>

    !! This has not been extensively tested / audited !!
*/

#pragma once
#include <Utilities/Utilities.hpp>
#include <Utilities/Crypto/SHA.hpp>

namespace qDSA
{
    // A "proper" crypto implementation should be constant time..
    constexpr bool Constanttime = false;

    // Fixed size containers for primitive types.
    using Generickey_t = std::array<uint8_t, 32>;
    using Privatekey_t = std::array<uint8_t, 32>;
    using Publickey_t  = std::array<uint8_t, 32>;
    using Sharedkey_t  = std::array<uint8_t, 32>;
    using Signature_t  = std::array<uint8_t, 64>;

    // Combine arrays, no idea why the STL doesn't provide this..
    template <typename T, size_t N, size_t M>
    constexpr std::array<T, N + M> operator+(const std::array<T, N> &Left, const std::array<T, M> &Right)
    {
        return[]<size_t ...LIndex, size_t ...RIndex>(const std::array<T, N> &Left, const std::array<T, M> &Right, std::index_sequence<LIndex...>, std::index_sequence<RIndex...>)
        {
            return std::array<T, N + M>{ { Left[LIndex]..., Right[RIndex]... } };
        }(Left, Right, std::make_index_sequence<N>(), std::make_index_sequence<M>());
    }
}

namespace qDSA::Datatypes
{
    // 128-bit Kummer point.
    struct K128_t final
    {
        static constexpr size_t Soragesize = 128 / 8;
        union
        {
            __m128i V; // Compiler hint.
            std::array<uint8_t, Soragesize> RAW{};
        };

        // Nothing special..
        constexpr K128_t() = default;
        constexpr K128_t(K128_t &&) = default;
        constexpr K128_t(const K128_t &) = default;
        constexpr K128_t &operator=(K128_t &&) = default;
        constexpr K128_t &operator=(const K128_t &) = default;

        // Generic initialization for the array.
        constexpr K128_t(uint64_t High, uint64_t Low)
        {
            RAW = std::bit_cast<std::array<uint8_t, 8>>(High) + std::bit_cast<std::array<uint8_t, 8>>(Low);
        }
        template <size_t N> constexpr K128_t(const std::span<uint8_t, N> Other)
        {
            std::ranges::copy_n(Other.begin(), std::min(Soragesize, Other.size()), RAW.data());
        }
        template <size_t N> constexpr K128_t(const std::array<uint8_t, N> &Other)
        {
            std::ranges::copy_n(Other.begin(), std::min(Soragesize, Other.size()), RAW.data());
        }

        // Simplify access to the raw storage.
        constexpr uint8_t &operator[](size_t i) { return RAW[i]; }
        constexpr uint8_t operator[](size_t i) const { return RAW[i]; }

        // Convert to components.
        constexpr std::tuple<uint64_t, uint64_t> asPair() const
        {
            const auto Pair = std::bit_cast<std::array<uint64_t, 2>>(RAW);
            return { Pair[0], Pair[1] };
        }
        constexpr std::array<uint8_t, Soragesize> asArray() const
        {
            return RAW;
        }
        template <typename T> requires std::constructible_from<T, decltype(RAW)> constexpr operator T() const
        {
            return T(RAW);
        }

        // Extracted due to needing expansion and reduction.
        // friend constexpr K128_t operator *=(K128_t &Left, const K128_t &Right);
        // friend constexpr K128_t operator *(const K128_t &Left, const K128_t &Right);

        // While the rest can be inlined.
        friend constexpr K128_t operator +(const K128_t &Left, const K128_t &Right)
        {
            K128_t Result{};

            uint8_t Carry = 0;
            for (size_t i = 0; i < 16; ++i)
            {
                const uint16_t Temp = uint16_t(Left[i]) + uint16_t(Right[i]) + Carry;
                Carry = (Temp >> 8) & 1;
                Result[i] = uint8_t(Temp);
            }

            Carry *= 2;
            for (size_t i = 0; i < 16; ++i)
            {
                const uint16_t Temp = uint16_t(Result[i]) + Carry;
                Carry = (Temp >> 8) & 1;
                Result[i] = uint8_t(Temp);
            }

            return Result;
        }
        friend constexpr K128_t operator -(const K128_t &Left, const K128_t &Right)
        {
            K128_t Result{};

            uint8_t Carry = 0;
            for (size_t i = 0; i < 16; ++i)
            {
                const uint16_t Temp = uint16_t(Left[i]) - (uint16_t(Right[i]) + Carry);
                Carry = (Temp >> 8) & 1;
                Result[i] = uint8_t(Temp);
            }

            Carry *= 2;
            for (size_t i = 0; i < 16; ++i)
            {
                const uint16_t Temp = uint16_t(Result[i]) - Carry;
                Carry = (Temp >> 8) & 1;
                Result[i] = uint8_t(Temp);
            }

            return Result;
        }
        constexpr K128_t &operator +=(const K128_t &Right) { *this = (*this + Right); return *this; }
        constexpr K128_t &operator -=(const K128_t &Right) { *this = (*this - Right); return *this; }

        // Freeze the point..
        constexpr K128_t &Freeze()
        {
            K128_t Result{};

            // Extract the top bit.
            uint16_t Carry = uint8_t(RAW[15] >> 7);
            Result[15] = RAW[15] & uint8_t(0x7F);

            for (size_t i = 0; i < 15; ++i)
            {
                const uint16_t Temp = uint16_t(RAW[i]) + Carry;
                Carry = (Temp >> 8) & 1;
                Result[i] = uint8_t(Temp);
            }

            Result[15] = uint8_t(uint8_t(Result[15]) + Carry);
            Result[0] = uint8_t(uint8_t(Result[0]) + uint8_t(Result[15] >> 7));
            Result[15] &= uint8_t(0x7F);

            RAW = Result;
            return *this;
        }
        static constexpr K128_t Freeze(const K128_t &Input)
        {
            auto Copy = Input;
            return Copy.Freeze();
        }
    };

    // 256 bit compressed Kummer surface.
    struct K256_t final
    {
        static constexpr size_t Soragesize = 256 / 8;
        union
        {
            __m256i V; // Compiler hint.
            std::array<uint8_t, Soragesize> RAW{};
        };

        // Nothing special..
        constexpr K256_t() = default;
        constexpr K256_t(K256_t &&) = default;
        constexpr K256_t(const K256_t &) = default;
        constexpr K256_t &operator=(K256_t &&) = default;
        constexpr K256_t &operator=(const K256_t &) = default;

        // Generic initialization for the array.
        constexpr K256_t(K128_t High, K128_t Low)
        {
            RAW = std::bit_cast<std::array<uint8_t, 16>>(High.RAW) + std::bit_cast<std::array<uint8_t, 16>>(Low.RAW);
        }
        constexpr K256_t(uint64_t A, uint64_t B, uint64_t C, uint64_t D)
        {
            RAW = std::bit_cast<std::array<uint8_t, 8>>(A) + std::bit_cast<std::array<uint8_t, 8>>(B) + std::bit_cast<std::array<uint8_t, 8>>(C) + std::bit_cast<std::array<uint8_t, 8>>(D);
        }
        template <size_t N> constexpr K256_t(const std::span<uint8_t, N> Other)
        {
            std::ranges::copy_n(Other.begin(), std::min(Soragesize, Other.size()), RAW.data());
        }
        template <size_t N> constexpr K256_t(const std::array<uint8_t, N> &Other)
        {
            std::ranges::copy_n(Other.begin(), std::min(Soragesize, Other.size()), RAW.data());
        }

        // Simplify access to the raw storage.
        constexpr uint8_t &operator[](size_t i) { return RAW[i]; }
        constexpr uint8_t operator[](size_t i) const { return RAW[i]; }

        // Convert to components.
        constexpr std::tuple<K128_t, K128_t> asPair() const
        {
            const auto Pair = std::bit_cast<std::array<std::array<uint8_t, 16>, 2>>(RAW);
            return { Pair[0], Pair[1] };
        }
        constexpr std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> asTuple() const
        {
            const auto Tuple = std::bit_cast<std::array<uint64_t, 4>>(RAW);
            return { Tuple[0], Tuple[1], Tuple[2], Tuple[3] };
        }
        constexpr std::array<uint8_t, Soragesize> asArray() const
        {
            return RAW;
        }
        template <typename T> requires std::constructible_from<T, decltype(RAW)> constexpr operator T() const
        {
            return T(RAW);
        }
    };

    // 512 bit uncompressed Kummer surface.
    struct K512_t final
    {
        static constexpr size_t Soragesize = 512 / 8;
        union
        {
            __m512i V; // Compiler hint.
            std::array<uint8_t, Soragesize> RAW{};
        };

        // Nothing special..
        constexpr K512_t() = default;
        constexpr K512_t(K512_t &&) = default;
        constexpr K512_t(const K512_t &) = default;
        constexpr K512_t &operator=(K512_t &&) = default;
        constexpr K512_t &operator=(const K512_t &) = default;

        // Generic initialization for the array.
        constexpr K512_t(K256_t High, K256_t Low)
        {
            RAW = std::bit_cast<std::array<uint8_t, 32>>(High.RAW) + std::bit_cast<std::array<uint8_t, 32>>(Low.RAW);
        }
        constexpr K512_t(K128_t X, K128_t Y, K128_t Z, K128_t W = {})
        {
            const auto High = std::bit_cast<std::array<uint8_t, 16>>(X.RAW) + std::bit_cast<std::array<uint8_t, 16>>(Y.RAW);
            const auto Low = std::bit_cast<std::array<uint8_t, 16>>(Z.RAW) + std::bit_cast<std::array<uint8_t, 16>>(W.RAW);
            RAW = High + Low;
        }
        template <size_t N> constexpr K512_t(const std::span<uint8_t, N> Other)
        {
            std::ranges::copy_n(Other.begin(), std::min(Soragesize, Other.size()), RAW.data());
        }
        template <size_t N> constexpr K512_t(const std::array<uint8_t, N> &Other)
        {
            std::ranges::copy_n(Other.begin(), std::min(Soragesize, Other.size()), RAW.data());
        }

        // Simplify access to the raw storage.
        constexpr uint8_t &operator[](size_t i) { return RAW[i]; }
        constexpr const uint8_t &operator[](size_t i) const { return RAW[i]; }

        // Convert to components.
        constexpr std::tuple<K256_t, K256_t> asPair() const
        {
            const auto Pair = std::bit_cast<std::array<std::array<uint8_t, 32>, 2>>(RAW);
            return { Pair[0], Pair[1] };
        }
        constexpr std::array<uint8_t, Soragesize> asArray() const
        {
            return RAW;
        }
        constexpr std::tuple<K128_t, K128_t, K128_t, K128_t> asTuple() const
        {
            const auto Tuple = std::bit_cast<std::array<std::array<uint8_t, 16>, 4>>(RAW);
            return { Tuple[0], Tuple[1], Tuple[2], Tuple[3] };
        }
        template <typename T> requires std::constructible_from<T, decltype(RAW)> constexpr operator T() const
        {
            return T(RAW);
        }
    };
}

namespace qDSA::Pointmath
{
    using namespace Datatypes;

    // Partial addition with an offset for sets.
    constexpr K512_t Addpartial(const K512_t &Left, const K256_t &Right, uint8_t Offset)
    {
        uint8_t Carry = 0;
        K512_t Result{ Left };

        for (size_t i = 0; i < 32; ++i)
        {
            const uint16_t Temp = uint16_t(Left[i + Offset]) + uint16_t(Right[i]) + Carry;
            Carry = (Temp >> 8) & 1;
            Result[i + Offset] = uint8_t(Temp);
        }

        for (size_t i = 32 + Offset; i < 64; ++i)
        {
            const uint16_t Temp = uint16_t(Left[i]) + Carry;
            Carry = (Temp >> 8) & 1;
            Result[i] = uint8_t(Temp);
        }

        return Result;
    }

    // Helpers for N-bit <-> N-bit conversion.
    constexpr K256_t Expand(const K128_t &X, const K128_t &Y)
    {
        std::array<uint16_t, 32> Buffer{};
        std::array<uint8_t, 32> Result{};

        for (size_t i = 0; i < 16; ++i)
        {
            for (size_t c = 0; c < 16; ++c)
            {
                const uint16_t Temp = uint16_t(X[i]) * uint16_t(Y[c]);
                Buffer[i + c + 1] += (Temp >> 8) & 0xFF;
                Buffer[i + c] += Temp & 0xFF;
            }
        }

        for (size_t i = 0; i < 31; ++i)
        {
            Buffer[i + 1] += Buffer[i] >> 8;
            Result[i] = uint8_t(Buffer[i]);
        }

        Result[31] = uint8_t(Buffer[31]);
        return { Result };
    }
    constexpr K512_t Expand(const K256_t &X, const K256_t &Y)
    {
        const auto [X0, X1] = X.asPair();
        const auto [Y0, Y1] = Y.asPair();

        K512_t Result{ Expand(X0, Y0), {} };
        Result = Addpartial(Result, Expand(X0, Y1), 16);
        Result = Addpartial(Result, Expand(X1, Y0), 16);
        Result = Addpartial(Result, Expand(X1, Y1), 32);

        return Result;
    }
    constexpr K128_t Reduce(const K256_t &Input)
    {
        std::array<uint8_t, 16> Result{};
        std::array<uint16_t, 16> Buffer{};

        for (size_t i = 0; i < 16; ++i)
        {
            Buffer[i] = uint16_t(Input[i]);
            Buffer[i] += 2 * uint8_t(Input[i + 16]);
        }

        // NOTE(tcn): After 4 million iterations, it looks good enough..
        // TODO(tcn): Verify if we need to do two iterations.
        for (size_t i = 0; i < 15; ++i)
        {
            Buffer[i + 1] += (Buffer[i] >> 8);
            Buffer[i] &= 0x00FF;
        }

        Buffer[0] += 2 * (Buffer[15] >> 8);
        Buffer[15] &= 0x00FF;

        for (size_t i = 0; i < 15; ++i)
        {
            Buffer[i + 1] += (Buffer[i] >> 8);
            Result[i] = uint8_t(Buffer[i]);
        }

        Result[15] = uint8_t(Buffer[15]);
        return { Result };
    }
    constexpr K256_t Reduce(const K512_t &Input)
    {
        constexpr K256_t L1 = std::to_array<uint8_t>(
        {
               0xbd, 0x05, 0x0c, 0x84, 0x4b, 0x0b, 0x73, 0x47,
               0xff, 0x54, 0xa1, 0xf9, 0xc9, 0x7f, 0xc2, 0xd2,
               0x94, 0x52, 0xc7, 0x20, 0x98, 0xd6, 0x34, 0x03
        });
        constexpr K256_t L6 = std::to_array<uint8_t>(
        {
                0x40, 0x6f, 0x01, 0x03, 0xe1, 0xd2, 0xc2, 0xdc,
                0xd1, 0x3f, 0x55, 0x68, 0x7e, 0xf2, 0x9f, 0xb0,
                0x34, 0xa5, 0xd4, 0x31, 0x08, 0xa6, 0x35, 0xcd
        });

        K512_t Buffer = Input;
        for (size_t i = 0; i < 4; ++i)
        {
            const auto [High, Low] = Buffer.asPair();

            const auto Temp = Expand(Low, L6);
            cmp::memcpy(&Buffer[32], &Temp[32], 32);

            Buffer = Addpartial(Buffer, Temp.RAW, 0);
        }

        Buffer[33] = (Buffer[32] & uint8_t(0x1c)) >> 2;
        Buffer[32] = Buffer[32] << 6;
        Buffer[32] |= (Buffer[31] & uint8_t(0xfc)) >> 2;
        Buffer[31] &= uint8_t(0x03);

        for (size_t i = 0; i < 1; ++i)
        {
            const auto [High, Low] = Buffer.asPair();

            const auto Temp = Expand(Low, L1);
            cmp::memcpy(&Buffer[32], &Temp[32], 32);
            Buffer = Addpartial(Buffer, Temp.RAW, 0);
        }

        Buffer[33] = uint8_t(0);
        Buffer[32] = (Buffer[31] & uint8_t(0x04)) >> 2;
        Buffer[31] &= uint8_t(0x03);

        for (size_t i = 0; i < 1; ++i)
        {
            const auto [High, Low] = Buffer.asPair();

            Buffer[32] = uint8_t(0);

            Buffer = Addpartial(Buffer, Expand(Low, L1).RAW, 0);
        }

        return Buffer.RAW;
    }

    // Extracted due to needing expansion and reduction.
    constexpr K128_t operator *(const K128_t &Left, const K128_t &Right)
    {
        return Reduce(Expand(Left, Right));
    }
    constexpr K128_t operator *=(K128_t &Left, const K128_t &Right) { Left = (Left * Right); return Left; }

    // For somewhat cleaner code later.
    constexpr bool isZero(const K128_t &Input)
    {
        if constexpr (Constanttime)
        {
            const auto One = K128_t{ std::array<uint8_t, 16> {1} };
            const auto Frozen = K128_t{ One + Input }.Freeze();

            uint8_t Check = Frozen[0] ^ 1;
            for (size_t i = 1; i < 16; ++i)
            {
                Check |= Frozen[i];
            }

            return Check != 0;
        }
        else
        {
            return Input.RAW == K128_t{}.RAW;
        }
    }
    constexpr void Swap(K128_t &A, K128_t &B, bool doSwap)
    {
        if constexpr (Constanttime)
        {
            const uint8_t Mask = 0 - static_cast<int>(doSwap);

            for (size_t i = 0; i < 16; ++i)
            {
                uint8_t Temp = A[i] ^ B[i];
                Temp &= Mask;
                A[i] ^= Temp;
                B[i] ^= Temp;
            }
        }
        else
        {
            if (doSwap) std::swap(A, B);
        }
    }
    constexpr void Swap(K512_t &Left, K512_t &Right, bool doSwap)
    {
        auto [lX, lY, lZ, lW] = Left.asTuple();
        auto [rX, rY, rZ, rW] = Right.asTuple();

        Swap(lX, rX, doSwap);
        Swap(lY, rY, doSwap);
        Swap(lZ, rZ, doSwap);
        Swap(lW, rW, doSwap);

        Left = { lX, lY, lZ, lW };
        Right = { rX, rY, rZ, rW };
    }

    // Surfaces need some extra maths.
    constexpr K128_t Negate(const K128_t &Input)
    {
        return K128_t{} - Input;
    }
    constexpr K256_t Negate(const K256_t &Input)
    {
        constexpr K256_t N = std::to_array<uint8_t>
        ({
                0x43, 0xFA, 0xF3, 0x7B, 0xB4, 0xF4, 0x8C, 0xB8,
                0x00, 0xAB, 0x5E, 0x6,  0x36, 0x80, 0x3D, 0x2D,
                0x6B, 0xAD, 0x38, 0xDF, 0x67, 0x29, 0xCB, 0xFC,
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x03
        });

        uint16_t Carry{};
        K256_t Result{};

        for (uint8_t i = 0; i < 32; ++i)
        {
            const auto Temp = uint16_t(N[i]) - (uint16_t(Input[i]) + Carry);
            Carry = (Temp >> 8) & 1;
            Result[i] = uint8_t(Temp);
        }

        return Result;
    }

    // Partial negation.
    constexpr K512_t &NegateX(K512_t &Input)
    {
        const auto [X, Y, Z, W] = Input.asTuple();
        Input = { Negate(X), Y, Z, W };
        return Input;
    }
    constexpr K512_t &NegateW(K512_t &Input)
    {
        const auto [X, Y, Z, W] = Input.asTuple();
        Input = { X, Y, Z, Negate(W) };
        return Input;
    }
    constexpr K512_t &NegateX(K512_t &&Input)
    {
        const auto [X, Y, Z, W] = Input.asTuple();
        Input = { Negate(X), Y, Z, W };
        return Input;
    }
    constexpr K512_t &NegateW(K512_t &&Input)
    {
        const auto [X, Y, Z, W] = Input.asTuple();
        Input = { X, Y, Z, Negate(W) };
        return Input;
    }

    // Internal helper for exponentials, should not be used anywhere else.
    namespace Internal
    {
        template <size_t Exponent> K128_t Exp(const K128_t &Input)
        {
            // NOTE(tcn): MSVC wants to instantiate all paths, even those never taken..
            if constexpr (Exponent == 0) return Input;

            if constexpr (Exponent == 1) return Input;
            if constexpr (Exponent == 2) return Input * Input;

            if constexpr (Exponent % 2 == 0)
            {
                const auto Squared = Input * Input;
                return Exp<Exponent / 2>(Squared);
            }
            else
            {
                const auto Squared = Input * Input;
                return Input * Exp<(Exponent - 1) / 2>(Squared);
            }
        };
    }

    // ret = Input ^ {-1/2}
    constexpr K128_t InvSQRT(const K128_t &Input)
    {
        /*
            NOTE(tcn):
            This is effectively Input ^ 127605887595351923798765477786913079294
            As such, there's a lot of room for optimization here.

            Provided are 3 implementations, need to benchmark on different systems.
            More work is needed by someone with more maths knowledge.

            Bench on MSVC 17.4.3, CPU E5-2660v2 (2013), 100K iterations, 10 runs:
            LambdaA:  72.05us (min),  79.18us (max)
            LambdaB:  68.86us (min),  75.69us (max)
            LambdaC: 117.79us (min), 135.96us (max)
        */

        [[maybe_unused]] const auto LambdaA = [](K128_t Input)
        {
            using namespace Internal;

            const auto A = Exp<2>(Input);
            const auto B = Exp<15>(Input);
            const auto C = Exp<31>(Input);

            const auto D = Exp<33>(C);
            const auto E = Exp<0x401>(D);
            const auto F = Exp<0x100001>(E);
            const auto G = Exp<0x10000000001>(F);
            const auto H = F * Exp<0x10000000000>(G);

            const auto I = Exp<16>(H);
            const auto J = Exp<2>(I * B);

            return J * Exp<2>(J * A);
        };
        [[maybe_unused]] const auto LambdaB = [](K128_t Input)
        {
            const auto Square = [](K128_t Input, size_t Count)
            {
                for (size_t i = 0; i < Count; ++i)
                    Input *= Input;
                return Input;
            };

            const auto A = Input * Input;
            const auto B = A * Input;
            const auto C = B * B;
            const auto D = C * C;
            const auto E = D * B;
            const auto F = E * E;
            const auto G = F * Input;

            const auto H = G * Square(G * G, 4);
            const auto I = H * Square(H * H, 9);
            const auto J = I * Square(I * I, 19);

            const auto K = J * Square(J * J, 39);
            const auto L = J * Square(K, 40);
            const auto M = Square(L, 4);
            const auto N = M * E;
            const auto O = N * N;

            const auto P = (O * A) * (O * A);

            return O * P;
        };
        [[maybe_unused]] const auto LambdaC = [](K128_t Input)
        {
            Input *= Input;
            auto Result = Input;

            // 01011111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111110
            for (size_t i = 2; i < 127; ++i)
            {
                Input *= Input;
                if (i != 125) Result *= Input;
            }

            return Result;
        };

        return LambdaB(Input);
    }
    constexpr K128_t Invert(const K128_t &Input)
    {
        auto Result = InvSQRT(Input * Input);
        const auto Temp = Result * Input;
        Result *= Temp;
        return Result;
    }

    // Returns a default constructed point on error.
    constexpr K128_t hasSQRT(const K128_t &Delta, bool Sigma)
    {
        K128_t Result = InvSQRT(Delta);
        Result *= Delta;

        // Invalid.
        if (!isZero((Result * Result) - Delta)) return {};

        Result.Freeze();

        if (((Result[0] & 1) ^ Sigma))
        {
            Result = Negate(Result);
        }

        return Result;
    }

    // Surface math.
    constexpr K512_t Square4(const K512_t &Input)
    {
        const auto [X, Y, Z, W] = Input.asTuple();
        return { X * X, Y * Y, Z * Z, W * W };
    }
    constexpr K512_t Hadamard(const K512_t &Input)
    {
        const auto [X, Y, Z, W] = Input.asTuple();
        const auto A = Y - X;
        const auto B = Z + W;
        const auto C = X + Y;
        const auto D = Z - W;

        return { A + B, A - B, D - C, C + D };
    }
    constexpr K512_t Multiply4(const K512_t &Left, const K512_t &Right)
    {
        const auto [lX, lY, lZ, lW] = Left.asTuple();
        const auto [rX, rY, rZ, rW] = Right.asTuple();
        return { lX * rX, lY * rY, lZ * rZ, lW * rW };
    }
    constexpr K128_t Dotproduct(const K512_t &Left, const K512_t &Right)
    {
        const auto [lX, lY, lZ, lW] = Left.asTuple();
        const auto [rX, rY, rZ, rW] = Right.asTuple();
        return (lX * rX) + (lY * rY) + (lZ * rZ) + (lW * rW);
    }
    constexpr K128_t negDotproduct(const K512_t &Left, const K512_t &Right)
    {
        const auto [lX, lY, lZ, lW] = Left.asTuple();
        const auto [rX, rY, rZ, rW] = Right.asTuple();
        return (lX * rX) - (lY * rY) - (lZ * rZ) + (lW * rW);
    }

    // Scalar groups.
    constexpr K256_t getPositive(const K256_t &Input)
    {
        if ((Input[0] & 1)) return Negate(Input);
        else return Input;
    }
    constexpr K256_t opsScalar(const K256_t &A, const K256_t &B, const K256_t &C)
    {
        const K512_t Temp(Negate(Reduce(Expand(B, C))), {});
        return Reduce(Addpartial(Temp, A, 0));
    }
    template <size_t N> constexpr K256_t getScalar(const std::array<uint8_t, N> &Input)
    {
        return Reduce(K512_t{ Input });
    }
}

namespace qDSA::Fieldmath
{
    using namespace Datatypes;
    using namespace Pointmath;

    // Evaluate the polynomials at (L1, L2, Tau).
    constexpr std::pair<K128_t, K128_t> getK2(const K128_t &L1, const K128_t &L2, bool Tau)
    {
        K128_t A, B;

        A = L1 * L2 * std::to_array<uint8_t>({ 0x11, 0x12 });

        if (Tau)
        {
            B = L1 * std::to_array<uint8_t>({ 0xF7, 0x0D });
            A += B;

            B = L2 * std::to_array<uint8_t>({ 0x99, 0x25 });
            A -= B;
        }

        A *= std::to_array<uint8_t>({ 0xE3, 0x2F });
        A += A;

        B = L1 * std::to_array<uint8_t>({ 0x33, 0x1D });
        B *= B;
        A = B - A;

        B = L2 * std::to_array<uint8_t>({ 0xE3, 0x2F });
        B *= B;
        A += B;;

        if (Tau)
        {
            B = std::to_array<uint8_t>({ 0x0B, 0x2C });
            B *= B;
            A += B;
        }

        return { A, B };
    }
    constexpr std::tuple<K128_t, K128_t, K128_t> getK3(const K128_t &L1, const K128_t &L2, bool Tau)
    {
        K128_t A, B, C;

        A = L1 * L1;
        B = L2 * L2;

        if (Tau)
        {
            C = std::to_array<uint8_t>({ 0x01, 0x00 });
            A += C;
            B += C;
            C = A + B;
        }

        A *= L2 * std::to_array<uint8_t>({ 0xF7, 0x0D });
        B *= L1 * std::to_array<uint8_t>({ 0x99, 0x25 });
        A -= B;

        if (Tau)
        {
            B = std::to_array<uint8_t>({ 0x01, 0x00 });
            C -= B;
            C -= B;
            C *= std::to_array<uint8_t>({ 0x11, 0x12 });
            A += C;
        }

        A *= std::to_array<uint8_t>({ 0xE3, 0x2F });

        if (Tau)
        {
            B = L1 * L2 * (std::to_array<uint8_t>({ 0x79, 0x17 }) * std::to_array<uint8_t>({ 0xD7, 0xAB }));
            A -= B;
        }

        return { A, B, C };
    }
    constexpr std::pair<K128_t, K128_t> getK4(const K128_t &L1, const K128_t &L2, bool Tau)
    {
        K128_t A, B;

        if (Tau)
        {
            A = L1 * std::to_array<uint8_t>({ 0x99, 0x25 });
            B = L2 * std::to_array<uint8_t>({ 0xF7, 0x0D });
            B -= A;

            B += std::to_array<uint8_t>({ 0x11, 0x12 });
            B *= L1 * L2 * std::to_array<uint8_t>({ 0xE3, 0x2F });
            B += B;

            A = L1 * std::to_array<uint8_t>({ 0xE3, 0x2F });
            A *= A;
            B = A - B;

            A = L2 * std::to_array<uint8_t>({ 0x33, 0x1D });
            A *= A;
            B += A;
        }

        A = L1 * L2 * std::to_array<uint8_t>({ 0x0B, 0x2C });
        A *= A;

        if (Tau) A += B;

        return { A, B };
    }

    // (Bjj * R1^2) - (2 * C * Bij * R1 * R2) + (Bii * R2^2) == 0
    constexpr bool isQuad(const K128_t &Bij, const K128_t &Bjj, const K128_t &Bii, const K128_t &R1, const K128_t &R2)
    {
        constexpr K128_t One = std::to_array<uint8_t>({ 1 });
        constexpr K128_t Const = std::to_array<uint8_t>
        ({
            0x43, 0xA8, 0xDD, 0xCD, 0xD8, 0xE3, 0xF7, 0x46,
            0xDD, 0xA2, 0x20, 0xA3, 0xEF, 0x0E, 0xF5, 0x40
        });

        const auto A = (Bjj * R1 * R1);
        const auto B = (Const * Bij * R1 * R2) + (Const * Bij * R1 * R2);
        const auto C = (Bii * R2 * R2);

        const auto Result = K128_t::Freeze(One + (A - B + C));
        return Result.RAW == One.RAW;
    }
}

namespace qDSA::Matrixmath
{
    using namespace Datatypes;
    using namespace Pointmath;
    using namespace Fieldmath;

    // Mult by k-hat and mu-hat.
    constexpr K128_t kRow(const K128_t &X1, const K128_t &X2, const K128_t &X3, const K128_t &X4)
    {
        return
            (X2 * std::to_array<uint8_t>({ 0x80, 0x00 })) +
            (X3 * std::to_array<uint8_t>({ 0x39, 0x02 })) +
            (X4 * std::to_array<uint8_t>({ 0x49, 0x04 })) -
            (X1 * std::to_array<uint8_t>({ 0xC1, 0x03 }));
    }
    constexpr K128_t mRow(const K128_t &X1, const K128_t &X2, const K128_t &X3, const K128_t &X4)
    {
        return
            ((X2 + X2) - X1) *
            (std::to_array<uint8_t>({ 0x0B, 0x00 })) +
            (X3 * std::to_array<uint8_t>({ 0x13, 0x00 })) +
            (X4 * std::to_array<uint8_t>({ 0x03, 0x00 }));
    }

    // Ret = ( B_{1,1}, B_{2,2}, B_{3,3}, B_{4,4} )
    constexpr K512_t biiValues(const K512_t &P, const K512_t &Q)
    {
        constexpr K512_t muHat{ std::to_array<uint8_t>({ 0x21, 0x00 }), std::to_array<uint8_t>({ 0x0B, 0x00 }), std::to_array<uint8_t>({ 0x11, 0x00 }), std::to_array<uint8_t>({ 0x31, 0x00 }) };
        constexpr K512_t eHat{ std::to_array<uint8_t>({ 0x41, 0x03 }), std::to_array<uint8_t>({ 0xC3, 0x09 }), std::to_array<uint8_t>({ 0x51, 0x06 }), std::to_array<uint8_t>({ 0x31, 0x02 }) };
        constexpr K512_t k{ std::to_array<uint8_t>({ 0x59, 0x12 }), std::to_array<uint8_t>({ 0x3F, 0x17 }), std::to_array<uint8_t>({ 0x79, 0x16 }), std::to_array<uint8_t>({ 0xC7, 0x07 }) };

        const auto T1 = [&]() -> K512_t
        {
            const auto [lX, lY, lZ, lW] = NegateX(Multiply4(Square4(P), eHat)).asTuple();
            const auto [rX, rY, rZ, rW] = NegateX(Multiply4(Square4(Q), eHat)).asTuple();

            return {
                Dotproduct({ lX, lY, lZ, lW }, { rX, rY, rZ, rW }),
                Dotproduct({ lX, lY, lZ, lW }, { rY, rX, rW, rZ }),
                Dotproduct({ lX, lZ, lY, lW }, { rZ, rX, rW, rY }),
                Dotproduct({ lX, lW, lY, lZ }, { rW, rX, rZ, rY })
            };
        }();
        const auto T2 = [&]() -> K512_t
        {
            const auto [X, Y, Z, W] = T1.asTuple();

            return {
                negDotproduct({ X, Y, Z, W }, k),
                negDotproduct({ Y, X, W, Z }, k),
                negDotproduct({ Z, W, X, Y }, k),
                negDotproduct({ W, Z, Y, X }, k)
            };

        }();

        return NegateX(Multiply4(T2, muHat));
    }

    // Ret = B_{ij}
    constexpr K128_t bijValues(const K512_t &P, const K512_t &Q, const K512_t &C)
    {
        const auto [P1, P2, P3, P4] = P.asTuple();
        const auto [Q1, Q2, Q3, Q4] = Q.asTuple();
        const auto [C1, C2, C3, C4] = C.asTuple();
        K128_t TempX, TempY, TempZ;
        K128_t Result;

        Result = P1 * P2;
        TempX = Q1 * Q2;
        TempY = P3 * P4;

        Result = Result - TempY;
        TempZ = Q3 * Q4;
        TempX = TempX - TempZ;
        Result = Result * TempX;
        TempX = TempY * TempZ;

        Result = Result * C3;
        Result = Result * C4;

        TempY = (C3 * C4) + (C1 * C2);
        TempX = TempX * TempY;
        Result = TempX - Result;

        Result = Result * C1;
        Result = Result * C2;

        TempY = (C2 * C4) + (C1 * C3);
        Result = Result * TempY;

        TempY = (C2 * C3) + (C1 * C4);
        Result = Result * TempY;

        return Result;
    }
}

namespace qDSA::Internal
{
    using namespace Datatypes;
    using namespace Pointmath;
    using namespace Fieldmath;
    using namespace Matrixmath;

    // Pre-computing inverted Kummer point coordinates.
    constexpr K512_t Wrap(const K512_t &Input)
    {
        const auto [X, Y, Z, W] = Input.asTuple();
        const auto A = Invert((Y * Z) * W) * X;
        const auto B = A * W;

        return { {}, { B * Z }, { B * Y }, { (Y * Z) * A} };
    }
    constexpr K512_t Unwrap(const K512_t &Input)
    {
        const auto [X, Y, Z, W] = Input.asTuple();
        return { (Y * Z) * W, Z * W, Y * W, (Y * Z) };
    }

    // Scalar pseudo-multiplication using a Montgomery ladder.
    constexpr std::pair<K512_t, K512_t> xDBLADD(K512_t Xp, K512_t Xq, const K512_t &Xd)
    {
        constexpr K512_t eHat{ std::to_array<uint8_t>({ 0x41, 0x03 }), std::to_array<uint8_t>({ 0xC3, 0x09 }), std::to_array<uint8_t>({ 0x51, 0x06 }), std::to_array<uint8_t>({ 0x31, 0x02 }) };
        constexpr K512_t e{ std::to_array<uint8_t>({ 0x72, 0x00 }), std::to_array<uint8_t>({ 0x39, 0x00 }), std::to_array<uint8_t>({ 0x42, 0x00 }), std::to_array<uint8_t>({ 0xA2, 0x01 }) };

        Xq = Hadamard(Xq);
        Xp = Hadamard(Xp);

        Xq = Multiply4(Xq, Xp);
        Xp = Square4(Xp);

        Xq = Multiply4(Xq, eHat);
        Xp = Multiply4(Xp, eHat);

        Xq = Hadamard(Xq);
        Xp = Hadamard(Xp);

        Xq = Square4(Xq);
        Xp = Square4(Xp);

        const auto [lX, lY, lZ, lW] = Xq.asTuple();
        const auto [rX, rY, rZ, rW] = Xd.asTuple();

        return { Multiply4(Xp, e), {lX, lY * rY, lZ * rZ, lW * rW} };
    }
    constexpr K512_t Ladder(K512_t Xq, const K512_t &Xd, const K256_t &Scalars)
    {
        K512_t Xp{ std::to_array<uint8_t>({ 0x0B, 0x00 }), std::to_array<uint8_t>({ 0x16, 0x00 }), std::to_array<uint8_t>({ 0x13, 0x00 }), std::to_array<uint8_t>({ 0x03, 0x00 }) };
        uint8_t Previous{};

        for (int i = 250; i >= 0; i--)
        {
            const auto Bit = (Scalars[i >> 3] >> (i & 0x07)) & 1;
            const auto doSwap = Bit ^ Previous;
            Previous = Bit;
            NegateX(Xq);

            Swap(Xp, Xq, doSwap);
            std::tie(Xp, Xq) = xDBLADD(Xp, Xq, Xd);
        }

        NegateX(Xp);

        Swap(Xp, Xq, Previous);
        return Xp;
    }
    constexpr K512_t Ladder(const K256_t &Scalars)
    {
        // Wrapped Kummer base-point.
        constexpr K512_t WBP
        {
            std::to_array<uint8_t>({0x00}),
            std::to_array<uint8_t>({ 0x48, 0x1A, 0x93, 0x4E, 0xA6, 0x51, 0xB3, 0xAE, 0xE7, 0xC2, 0x49, 0x20, 0xDC, 0xC3, 0xE0, 0x1B }),
            std::to_array<uint8_t>({ 0xDF, 0x36, 0x7E, 0xE0, 0x18, 0x98, 0x65, 0x64, 0x30, 0xA6, 0xAB, 0x8E, 0xCD, 0x16, 0xB4, 0x23 }),
            std::to_array<uint8_t>({ 0x1E, 0x44, 0x15, 0x72, 0x05, 0x3D, 0xAE, 0xC7, 0x4D, 0xA2, 0x47, 0x44, 0x38, 0x5C, 0xB3, 0x5D })
        };

        return Ladder(Unwrap(WBP), WBP, Scalars);
    }

    // So we can transfer the points in a more manageable form.
    constexpr std::tuple<K128_t, K128_t> Compress(const K512_t &Input)
    {
        const auto [X, Y, Z, W] = Input.asTuple();

        K128_t L1, L2, L3, L4;

        // Matrix multiplication by kHat.
        const std::array Premult = { kRow(W, Z, Y, X), kRow(Z, W, X, Y),
                                     kRow(Y, X, W, Z), kRow(X, Y, Z, W) };

        // Select a nice point.
        L2 = [&]() {
            if (!isZero(Premult[2])) return Invert(Premult[2]);
            if (!isZero(Premult[1])) return Invert(Premult[1]);
            if (!isZero(Premult[0])) return Invert(Premult[0]);
            return Invert(Premult[3]);
        }();

        // Normalize.
        L4 = Premult[3] * L2;
        L1 = Premult[0] * L2;
        L2 = Premult[1] * L2;

        // Tuples.
        const auto Tau = !isZero(Premult[2]);
        const auto K2 = getK2(L1, L2, Tau);
        const auto K3 = getK3(L1, L2, Tau);

        // K_2 * L4 - K_3
        L3 = (K2.first * L4) - std::get<0>(K3);

        L1.Freeze();
        L2.Freeze();
        L3.Freeze();

        L1[15] |= uint8_t(uint8_t(Tau) << 7);
        L2[15] |= uint8_t((L3[0] & uint8_t(1)) << 7);

        return { L1, L2 };
    }
    constexpr K512_t Decompress(const K256_t &Input)
    {
        K128_t T0, T1, T2, T3, T4, T5;
        auto [L1, L2] = Input.asPair();

        const auto Sigma = ((L2[15] & uint8_t(0x80)) >> 7) == uint8_t(1);
        const auto Tau = ((L1[15] & uint8_t(0x80)) >> 7) == uint8_t(1);
        L1[15] &= uint8_t(0x7F);
        L2[15] &= uint8_t(0x7F);

        std::tie(T1, T4) = getK2(L1, L2, Tau);
        std::tie(T3, T4) = getK4(L1, L2, Tau);
        std::tie(T2, T4, T5) = getK3(L1, L2, Tau);

        // K2_X = 0
        if (isZero(T1))
        {
            T2.Freeze();

            // K3_Z = 0
            if (isZero(T2))
            {
                // Invalid compression.
                if (!isZero(L1) || !isZero(L2) || Tau || Sigma) return {};
                else
                {
                    T0 = T1 = T2 = T3 = {};
                    T3[0] = uint8_t(1);
                }
            }

            //
            else if ((uint8_t(Sigma) ^ T2[0]) != uint8_t(1))
            {
                T0 = T2 * L1;
                T0 += T0;
                T1 = T2 * L2;
                T1 += T1;

                if (Tau) T2 += T2;
                else T2 = {};
            }

            // Invalid compression.
            else
            {
                return {};
            }
        }

        else
        {
            T4 = T2 * T2;
            T5 = T1 * T3;
            T4 = T4 - T5;

            // Invalid compression.
            const auto Root = hasSQRT(T4, Sigma);
            if (isZero(Root)) return {};

            T3 = T2 + Root;

            if (Tau) T2 = T1;
            else T2 = {};

            T0 = T1 * L1;
            T1 = T1 * L2;
        }

        // Matrix multiplication by mu (inverse).
        return { mRow(T3, T2, T1, T0), mRow(T2, T3, T0, T1),
                 mRow(T1, T0, T3, T2), mRow(T0, T1, T2, T3) };
    }

    // Verification.
    constexpr bool Check(K512_t P, K512_t Q, const K256_t &Rcomp)
    {
        constexpr K512_t muHat{ std::to_array<uint8_t>({ 0x21, 0x00 }), std::to_array<uint8_t>({ 0x0B, 0x00 }), std::to_array<uint8_t>({ 0x11, 0x00 }), std::to_array<uint8_t>({ 0x31, 0x00 }) };

        // Invalid compression.
        auto R = Decompress(Rcomp);
        if (isZero(R)) return false;

        P = NegateW(Hadamard(NegateX(P)));
        Q = NegateW(Hadamard(NegateX(Q)));
        R = NegateW(Hadamard(NegateX(R)));

        const auto [PX, PY, PZ, PW] = P.asTuple();
        const auto [QX, QY, QZ, QW] = Q.asTuple();
        const auto [RX, RY, RZ, RW] = R.asTuple();
        const auto [muX, muY, muZ, muW] = muHat.asTuple();
        const auto [BiiX, BiiY, BiiZ, BiiW] = biiValues(P, Q).asTuple();

        // B_{1,2}
        auto Bij = bijValues(P, Q, muHat);
        auto Invalid = !isQuad(Bij, BiiY, BiiX, RX, RY);
        if constexpr (!Constanttime) { if (Invalid) return false; }

        // B_{1,3}
        Bij = bijValues({ PX, PZ, PY, PW }, { QX, QZ, QY, QW }, { muX, muZ, muY, muW });
        Invalid |= !isQuad(Bij, BiiZ, BiiX, RX, RZ);
        if constexpr (!Constanttime) { if (Invalid) return false; }

        // B_{1,4}
        Bij = bijValues({ PX, PW, PY, PZ }, { QX, QW, QY, QZ }, { muX, muW, muY, muZ });
        Invalid |= !isQuad(Bij, BiiW, BiiX, RX, RW);
        if constexpr (!Constanttime) { if (Invalid) return false; }

        // B_{2,3}
        Bij = bijValues({ PY, PZ, PX, PW }, { QY, QZ, QX, QW }, { muY, muZ, muX, muW });
        Bij = Negate(Bij);
        Invalid |= !isQuad(Bij, BiiZ, BiiY, RY, RZ);
        if constexpr (!Constanttime) { if (Invalid) return false; }

        // B_{2,4}
        Bij = bijValues({ PY, PW, PX, PZ }, { QY, QW, QX, QZ }, { muY, muW, muX, muZ });
        Bij = Negate(Bij);
        Invalid |= !isQuad(Bij, BiiW, BiiY, RY, RW);
        if constexpr (!Constanttime) { if (Invalid) return false; }

        // B_{3,4}
        Bij = bijValues({ PZ, PW, PX, PY }, { QZ, QW, QX, QY }, { muZ, muW, muX, muY });
        Bij = Negate(Bij);
        Invalid |= !isQuad(Bij, BiiW, BiiZ, RZ, RW);

        return !Invalid;
    }
}

namespace qDSA
{
    // Create a keypair from a random seed.
    constexpr Publickey_t getPublickey(const Privatekey_t &Privatekey)
    {
        using namespace qDSA::Internal;

        const auto Scalar = getScalar(Privatekey);
        const auto Ladders = Ladder(Scalar);
        const auto [X, Y] = Compress(Ladders);
        return K256_t{ X, Y }.RAW;
    }
    template <typename T> constexpr std::pair<Publickey_t, Privatekey_t> Createkeypair(const T &Seed)
    {
        auto Privatekey = Hash::SHA256(Seed);

        // Make the private-key compatible with RFC 8032 in-case someone want's to re-use it.
        Privatekey[0] &= uint8_t(0xF8);
        Privatekey[31] &= uint8_t(0x7F);
        Privatekey[31] |= uint8_t(0x40);

        return { getPublickey(Privatekey), Privatekey };
    }

    // Compute a shared secret between two keypairs (A.PK, B.SK) == (B.PK, A.SK)
    constexpr Sharedkey_t Generatesecret(const Publickey_t &Publickey, const Privatekey_t &Privatekey)
    {
        using namespace qDSA::Internal;

        const auto PK = Decompress(K256_t{ Publickey });
        const auto PKW = Wrap(PK);

        const auto Secret = Ladder(PK, PKW, getScalar(Privatekey));
        const auto [A, B] = Compress(Secret);
        return A.RAW + B.RAW;
    }

    // Create a signature for the provided message, somewhat hardened against hackery.
    template <cmp::Range_t C> constexpr Signature_t Sign(const Publickey_t &Publickey, const Privatekey_t &Privatekey, const C &Message)
    {
        using namespace qDSA::Internal;
        const auto Buffer = cmp::getBytes(Message);

        // Get the first point.
        const auto P = [&]()
        {
            // 32 bytes of deterministic 'randomness'.
            Blob_t Local; Local.reserve(Buffer.size() + 32);
            Local.append(Hash::SHA512(Privatekey), 8, 32);
            Local.append(Buffer.data(), Buffer.size());

            return getScalar(Hash::SHA512(Local));
        }();
        const auto [PZ, PW] = Compress(Ladder(P));

        // Get the second point.
        const auto Q = [&]()
        {
            Blob_t Local; Local.reserve(16 + 16 + 32 + Buffer.size());
            Local.append(PZ.RAW.data(), PZ.RAW.size());
            Local.append(PW.RAW.data(), PW.RAW.size());
            Local.append(Publickey.data(), Publickey.size());
            Local.append(Buffer.data(), Buffer.size());

            return getScalar(Hash::SHA512(Local));
        }();

        // Get the third point.
        const auto W = opsScalar(P, getPositive(Q), getScalar(Privatekey));

        // Share with the world..
        return PZ.RAW + PW.RAW + W.RAW;
    }

    // Verify that the message was signed by the owner of the public key.
    template <cmp::Range_t C> constexpr bool Verify(const Publickey_t &Publickey, const Signature_t &Signature, const C &Message)
    {
        using namespace qDSA::Internal;
        const auto Buffer = cmp::getBytes(Message);

        // Validate compression of the first point.
        const auto P = Decompress(K256_t(Publickey));
        if (isZero(P)) [[unlikely]] return false;

        // Second point.
        const auto Q = [&]()
        {
            Blob_t Local; Local.reserve(64 + Buffer.size());
            Local.append(Signature.data(), Signature.size() / 2);
            Local.append(Publickey.data(), Publickey.size());
            Local.append(Buffer.data(), Buffer.size());

            return getScalar(Hash::SHA512(Local));
        }();

        // Third point.
        const auto [High, Low] = K512_t(Signature).asPair();
        const auto W = Ladder(P, Wrap(P), Q);

        return Check(Ladder(getScalar(Low.RAW)), W, High);
    }
}
