/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-06-30
    License: MIT

    quotient Digital Signature Algorithm
    arXiv:1709.03358

    NOTE(tcn):
    This has not been extensively tested / audited.
    This implements some bad crypto practices, e.g. no constant-time operations.
    This should not be used for more important authentication / signing situations.

    Also; while one can make this proper constexpr, it takes forever for the compiler to evaluate.
    /constexpr:steps<NUMBER> where the default of 1048576 is nowhere close to enough..

    While it would be preferable (and easier to read) to have more union members, MSVC does not currently support it.
    It needs __cpp_constexpr >= 202002L, so might be something to rewrite in the future.
*/

#pragma once
#include "../Constexprhelpers.hpp"
#include "SHA.hpp"

namespace qDSA
{
    // Fixed size containers for primitive types.
    using Privatekey_t = cmp::Array_t<uint8_t, 32>;
    using Publickey_t = cmp::Array_t<uint8_t, 32>;
    using Sharedkey_t = cmp::Array_t<uint8_t, 32>;
    using Signature_t = cmp::Array_t<uint8_t, 64>;

    namespace Internal
    {
        #pragma region uKummer

        // 128-bit Kummer point.
        struct FE128_t final
        {
            union
            {
                __m128i V; // Compiler hint.
                std::array<uint8_t, 128 / 8> Raw{};
            };

            constexpr FE128_t() = default;
            constexpr FE128_t(FE128_t &&Input) = default;
            constexpr FE128_t(const FE128_t &Input) = default;
            constexpr FE128_t(uint64_t High, uint64_t Low)
            {
                cmp::memcpy(&Raw[0], High);
                cmp::memcpy(&Raw[8], Low);
            }
            constexpr FE128_t(std::span<const std::byte> Input)
            {
                cmp::memcpy(Raw.data(), Input.data(), std::min(Input.size(), Raw.size()));
            }
            constexpr FE128_t(std::span<const uint8_t> Input)
            {
                cmp::memcpy(Raw.data(), Input.data(), std::min(Input.size(), Raw.size()));
            }

            // No need for anything fancy..
            constexpr FE128_t &operator=(FE128_t &&Right) = default;
            constexpr FE128_t &operator=(const FE128_t &Right) = default;

            // Simplify access to Raw.
            constexpr operator std::span<uint8_t>() { return Raw; }
            constexpr uint8_t &operator[](size_t i) { return Raw[i]; }
            constexpr uint8_t operator[](size_t i) const { return Raw[i]; }
            constexpr operator std::span<const uint8_t>() const { return Raw; }

            // Extracted due to needing expansion and reduction.
            friend constexpr FE128_t operator +(const FE128_t &Left, const FE128_t &Right);
            friend constexpr FE128_t operator -(const FE128_t &Left, const FE128_t &Right);
            friend constexpr FE128_t operator *(const FE128_t &Left, const FE128_t &Right);
            constexpr FE128_t &operator +=(const FE128_t &Right) { *this = (*this + Right); return *this; }
            constexpr FE128_t &operator -=(const FE128_t &Right) { *this = (*this - Right); return *this; }
            constexpr FE128_t &operator *=(const FE128_t &Right) { *this = (*this * Right); return *this; }
        };

        // 256 bit compressed Kummer surface.
        struct FE256_t final
        {
            union
            {
                __m256i V; // Compiler hint.
                std::array<uint8_t, 256 / 8> Raw{};
            };

            constexpr FE256_t() = default;
            constexpr FE256_t(FE256_t &&Input) = default;
            constexpr FE256_t(const FE256_t &Input) = default;
            constexpr FE256_t(std::span<const std::byte> Input)
            {
                cmp::memcpy(Raw.data(), Input.data(), std::min(Input.size(), Raw.size()));
            }
            constexpr FE256_t(std::span<const uint8_t> Input)
            {
                cmp::memcpy(Raw.data(), Input.data(), std::min(Input.size(), Raw.size()));
            }
            constexpr FE256_t(const FE128_t &X, const FE128_t &Y)
            {
                cmp::memcpy(Raw.data() + 0, X.Raw.data(), X.Raw.size());
                cmp::memcpy(Raw.data() + 16, Y.Raw.data(), Y.Raw.size());
            }
            constexpr FE256_t(const std::pair<FE128_t, FE128_t> &Input)
            {
                const auto &[X, Y] = Input;
                cmp::memcpy(Raw.data() + 0, X.Raw.data(), X.Raw.size());
                cmp::memcpy(Raw.data() + 16, Y.Raw.data(), Y.Raw.size());
            }

            // No need for anything fancy..
            constexpr FE256_t &operator=(FE256_t &&Right) = default;
            constexpr FE256_t &operator=(const FE256_t &Right) = default;

            // Simplify access to Raw.
            constexpr operator std::span<uint8_t>() { return Raw; }
            constexpr uint8_t &operator[](size_t i) { return Raw[i]; }
            constexpr uint8_t operator[](size_t i) const { return Raw[i]; }
            constexpr operator std::span<const uint8_t>() const { return Raw; }

            // Component access.
            constexpr std::pair<FE128_t, FE128_t> as128() const
            {
                // msvc does not like bitcast to SIMD-derived types.
                const auto Temp = std::bit_cast<std::array<uint64_t, 4>>(Raw);
                return { {Temp[0], Temp[1]}, {Temp[2], Temp[3]} };
            }
        };

        // 512 bit uncompressed Kummer surface.
        struct FE512_t final
        {
            union
            {
                __m512i V; // Compiler hint.
                std::array<uint8_t, 512 / 8> Raw{};
            };

            constexpr FE512_t() = default;
            constexpr FE512_t(FE512_t &&Input) = default;
            constexpr FE512_t(const FE512_t &Input) = default;
            constexpr FE512_t(std::span<const uint8_t> Input)
            {
                cmp::memcpy(Raw.data(), Input.data(), std::min(Input.size(), Raw.size()));
            }
            constexpr FE512_t(std::span<const std::byte> Input)
            {
                cmp::memcpy(Raw.data(), Input.data(), std::min(Input.size(), Raw.size()));
            }
            constexpr FE512_t(const FE256_t &High, const FE256_t &Low)
            {
                cmp::memcpy(Raw.data() + 0, High.Raw.data(), High.Raw.size());
                cmp::memcpy(Raw.data() + 32, Low.Raw.data(), Low.Raw.size());
            }
            constexpr FE512_t(const std::pair<FE256_t, FE256_t> &Input)
            {
                const auto &[High, Low] = Input;
                cmp::memcpy(Raw.data() + 0, High.Raw.data(), High.Raw.size());
                cmp::memcpy(Raw.data() + 32, Low.Raw.data(), Low.Raw.size());
            }
            constexpr FE512_t(const std::tuple<FE128_t, FE128_t, FE128_t> &Input)
            {
                const auto &[X, Y, Z] = Input;

                cmp::memcpy(Raw.data() + 0, X.Raw.data(), X.Raw.size());
                cmp::memcpy(Raw.data() + 16, Y.Raw.data(), Y.Raw.size());
                cmp::memcpy(Raw.data() + 32, Z.Raw.data(), Z.Raw.size());
            }
            constexpr FE512_t(const std::tuple<FE128_t, FE128_t, FE128_t, FE128_t> &Input)
            {
                const auto &[X, Y, Z, W] = Input;

                cmp::memcpy(Raw.data() + 0, X.Raw.data(), X.Raw.size());
                cmp::memcpy(Raw.data() + 16, Y.Raw.data(), Y.Raw.size());
                cmp::memcpy(Raw.data() + 32, Z.Raw.data(), Z.Raw.size());
                cmp::memcpy(Raw.data() + 48, W.Raw.data(), W.Raw.size());
            }
            constexpr FE512_t(const FE128_t &X, const FE128_t &Y, const FE128_t &Z, const FE128_t &W)
            {
                cmp::memcpy(Raw.data() + 0, X.Raw.data(), X.Raw.size());
                cmp::memcpy(Raw.data() + 16, Y.Raw.data(), Y.Raw.size());
                cmp::memcpy(Raw.data() + 32, Z.Raw.data(), Z.Raw.size());
                cmp::memcpy(Raw.data() + 48, W.Raw.data(), W.Raw.size());
            }

            // No need for anything fancy..
            constexpr FE512_t &operator=(FE512_t &&Right) = default;
            constexpr FE512_t &operator=(const FE512_t &Right) = default;

            // Simplify access to Raw.
            constexpr operator std::span<uint8_t>() { return Raw; }
            constexpr uint8_t &operator[](size_t i) { return Raw[i]; }
            constexpr uint8_t operator[](size_t i) const { return Raw[i]; }
            constexpr operator std::span<const uint8_t>() const { return Raw; }

            // Component access.
            constexpr std::tuple<FE128_t, FE128_t, FE128_t, FE128_t> as128() const
            {
                // msvc does not like bitcast to SIMD-derived types.
                const auto Temp = std::bit_cast<std::array<uint64_t, 8>>(Raw);
                return { {Temp[0], Temp[1]}, {Temp[2], Temp[3]}, {Temp[4], Temp[5]}, {Temp[6], Temp[7]} };
            }
            constexpr std::pair<FE256_t, FE256_t> as256() const
            {
                // msvc does not like bitcast to SIMD-derived types.
                const auto Temp = std::bit_cast<std::array<uint64_t, 8>>(Raw);
                return { {{Temp[0], Temp[1]}, {Temp[2], Temp[3]}}, {{Temp[4], Temp[5]}, {Temp[6], Temp[7]} } };
            }
        };
        #pragma endregion

        #pragma region Maths

        // Partial addition with an offset for sets.
        constexpr FE512_t Addpartial(const FE512_t &Left, const FE256_t &Right, uint8_t Offset)
        {
            uint8_t Carry = 0;
            FE512_t Result{ Left };

            for (size_t i = 0; i < 32; ++i)
            {
                const uint16_t Temp = uint16_t(Left[i + Offset]) + uint16_t(Right[i]) + Carry;
                Carry = (Temp >> 8) & 1;
                Result[i + Offset] = uint8_t(Temp);
            }

            for (size_t i = size_t{ 32 } + Offset; i < 64; ++i)
            {
                const uint16_t Temp = uint16_t(Left[i]) + Carry;
                Carry = (Temp >> 8) & 1;
                Result[i] = uint8_t(Temp);
            }

            return Result;
        }

        // Helpers for N-bit <-> N-bit conversion.
        constexpr FE256_t Expand(const FE128_t &X, const FE128_t &Y)
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
        constexpr FE512_t Expand(const FE256_t &X, const FE256_t &Y)
        {
            const auto [X0, X1] = X.as128();
            const auto [Y0, Y1] = Y.as128();

            FE512_t Result{ Expand(X0, Y0), {} };
            Result = Addpartial(Result, Expand(X0, Y1), 16);
            Result = Addpartial(Result, Expand(X1, Y0), 16);
            Result = Addpartial(Result, Expand(X1, Y1), 32);

            return Result;
        }
        constexpr FE128_t Reduce(const FE256_t &Input)
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
        constexpr FE256_t Reduce(const FE512_t &Input)
        {
            constexpr FE256_t L1 = std::span<const uint8_t>
            { {
                    0xbd, 0x05, 0x0c, 0x84, 0x4b, 0x0b, 0x73, 0x47,
                    0xff, 0x54, 0xa1, 0xf9, 0xc9, 0x7f, 0xc2, 0xd2,
                    0x94, 0x52, 0xc7, 0x20, 0x98, 0xd6, 0x34, 0x03
                } };
            constexpr FE256_t L6 = std::span<const uint8_t>
            { {
                    0x40, 0x6f, 0x01, 0x03, 0xe1, 0xd2, 0xc2, 0xdc,
                    0xd1, 0x3f, 0x55, 0x68, 0x7e, 0xf2, 0x9f, 0xb0,
                    0x34, 0xa5, 0xd4, 0x31, 0x08, 0xa6, 0x35, 0xcd
                } };

            FE512_t Buffer = Input;
            for (size_t i = 0; i < 4; ++i)
            {
                const auto Temp = Expand(Buffer.as256().second, L6);
                for (size_t c = 32; c < 64; ++c) Buffer[c] = Temp[c];
                Buffer = Addpartial(Buffer, Temp.as256().first, 0);
            }

            Buffer[33] = (Buffer[32] & uint8_t(0x1c)) >> 2;
            Buffer[32] = Buffer[32] << 6;
            Buffer[32] |= (Buffer[31] & uint8_t(0xfc)) >> 2;
            Buffer[31] &= uint8_t(0x03);

            {
                const auto Temp = Expand(Buffer.as256().second, L1);
                for (size_t c = 32; c < 64; ++c) Buffer[c] = Temp[c];
                Buffer = Addpartial(Buffer, Temp.as256().first, 0);
            }

            Buffer[33] = uint8_t(0);
            Buffer[32] = (Buffer[31] & uint8_t(0x04)) >> 2;
            Buffer[31] &= uint8_t(0x03);

            const auto Temp = Expand(Buffer.as256().second, L1);
            Buffer[32] = uint8_t(0);
            Buffer = Addpartial(Buffer, Temp.as256().first, 0);

            return Buffer.as256().first;
        }

        // Extracted due to needing expansion and reduction.
        constexpr FE128_t operator +(const FE128_t &Left, const FE128_t &Right)
        {
            FE128_t Result{};

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
        constexpr FE128_t operator -(const FE128_t &Left, const FE128_t &Right)
        {
            FE128_t Result{};

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
        constexpr FE128_t operator *(const FE128_t &Left, const FE128_t &Right)
        {
            return Reduce(Expand(Left, Right));
        }

        // Point helpers.
        constexpr bool isZero(const FE128_t &Input)
        {
            return Input.Raw == FE128_t{}.Raw;
        }
        constexpr bool isZero(const FE256_t &Input)
        {
            return Input.Raw == FE256_t{}.Raw;
        }
        constexpr bool isZero(const FE512_t &Input)
        {
            return Input.Raw == FE512_t{}.Raw;
        }
        constexpr FE256_t Negate(const FE256_t &Input)
        {
            constexpr FE256_t N = std::span<const uint8_t>
            { {
                    0x43, 0xFA, 0xF3, 0x7B, 0xB4, 0xF4, 0x8C, 0xB8,
                    0x00, 0xAB, 0x5E, 0x6,  0x36, 0x80, 0x3D, 0x2D,
                    0x6B, 0xAD, 0x38, 0xDF, 0x67, 0x29, 0xCB, 0xFC,
                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x03
            } };

            uint16_t Carry{};
            FE256_t Result{};

            for (uint8_t i = 0; i < 32; ++i)
            {
                const auto Temp = uint16_t(N[i]) - (uint16_t(Input[i]) + Carry);
                Carry = (Temp >> 8) & 1;
                Result[i] = uint8_t(Temp);
            }

            return Result;
        }
        constexpr FE128_t Negate(const FE128_t &Input)
        {
            return FE128_t{} - Input;
        }
        constexpr FE128_t Freeze(const FE128_t &Input)
        {
            FE128_t Result{};
            uint16_t Carry = uint8_t(Input[15] >> 7);
            Result[15] = Input[15] & uint8_t(0x7F);

            for (size_t i = 0; i < 15; ++i)
            {
                const uint16_t Temp = uint16_t(Input[i]) + Carry;
                Carry = (Temp >> 8) & 1;
                Result[i] = uint8_t(Temp);
            }

            Result[15] = uint8_t(uint8_t(Result[15]) + Carry);
            Result[0] = uint8_t(uint8_t(Result[0]) + uint8_t(Result[15] >> 7));
            Result[15] &= uint8_t(0x7F);

            return Result;
        }

        // Silly stuff..
        constexpr void NegX(FE512_t *A)
        {
            const auto [X, Y, Z, W] = A->as128();
            *A = { Negate(X), Y, Z, W };
        }
        constexpr void NegW(FE512_t *A)
        {
            const auto [X, Y, Z, W] = A->as128();
            *A = { X, Y, Z, Negate(W) };
        }

        // Point math.
        constexpr FE128_t InvSQRT(const FE128_t &Input)
        {
            FE128_t X2, X3, X6;
            FE128_t Result;

            X2 = Input * Input;
            X3 = X2 * Input;
            X6 = X3 * X3;
            X6 = X6 * X6;
            X3 = X6 * X3;
            X6 = X3 * X3;
            X6 = X6 * Input;

            const auto Square = [](FE128_t Input, size_t Count)
            {
                for (size_t i = 0; i < Count; ++i)
                    Input *= Input;
                return Input;
            };

            X6 *= Square(X6 * X6, 4);
            X6 *= Square(X6 * X6, 9);
            X6 *= Square(X6 * X6, 19);
            Result = X6 * Square(X6 * X6, 39);
            Result = X6 * Square(Result, 40);
            Result = Square(Result, 4);

            Result *= X3;
            Result *= Result;
            X6 = Result * X2;
            X6 *= X6;
            Result *= X6;

            return Result;
        }
        constexpr FE128_t Invert(const FE128_t &Input)
        {
            auto Result = InvSQRT(Input * Input);
            const auto Temp = Result * Input;
            Result *= Temp;
            return Result;
        }
        constexpr FE128_t SQRT(const FE128_t &Delta, bool Sigma)
        {
            FE128_t Result = InvSQRT(Delta);
            Result *= Delta;

            // Invalid.
            if (!isZero((Result * Result) - Delta)) return {};

            Result = Freeze(Result);

            if (((Result[0] & uint8_t(1)) ^ uint8_t(Sigma)) == uint8_t(1))
            {
                Result = Negate(Result);
            }

            return Result;
        }

        // Surface math.
        constexpr FE512_t Square4(const FE512_t &Input)
        {
            const auto [X, Y, Z, W] = Input.as128();
            return { X * X, Y * Y, Z * Z, W * W };
        }
        constexpr FE512_t Hadamard(const FE512_t &Input)
        {
            const auto [X, Y, Z, W] = Input.as128();
            const auto A = Y - X;
            const auto B = Z + W;
            const auto C = X + Y;
            const auto D = Z - W;

            return { A + B, A - B, D - C, C + D };
        }
        constexpr FE512_t Multiply4(const FE512_t &Left, const FE512_t &Right)
        {
            const auto [lX, lY, lZ, lW] = Left.as128();
            const auto [rX, rY, rZ, rW] = Right.as128();
            return { lX * rX, lY * rY, lZ * rZ, lW * rW };
        }
        constexpr FE128_t Dotproduct(const FE512_t &Left, const FE512_t &Right)
        {
            const auto [lX, lY, lZ, lW] = Left.as128();
            const auto [rX, rY, rZ, rW] = Right.as128();
            return (lX * rX) + (lY * rY) + (lZ * rZ) + (lW * rW);
        }
        constexpr FE128_t negDotproduct(const FE512_t &Left, const FE512_t &Right)
        {
            const auto [lX, lY, lZ, lW] = Left.as128();
            const auto [rX, rY, rZ, rW] = Right.as128();
            return (lX * rX) - (lY * rY) - (lZ * rZ) + (lW * rW);
        }

        // Scalar groups.
        template <size_t N> constexpr FE256_t getScalar(const cmp::Array_t<uint8_t, N> &Input)
        {
            FE512_t Temp{};
            for (uint8_t i = 0; i < N; ++i)
                Temp[i] = Input[i];
            return Reduce(Temp);
        }
        template <size_t N> constexpr FE256_t getScalar(const std::array<uint8_t, N> &Input)
        {
            FE512_t Temp{};
            for (uint8_t i = 0; i < N; ++i)
                Temp[i] = Input[i];
            return Reduce(Temp);
        }
        constexpr FE256_t getPositive(const FE256_t &Input)
        {
            if ((Input[0] & uint8_t(1)) == uint8_t(1)) return Negate(Input);
            else return Input;
        }
        constexpr FE256_t opsScalar(const FE256_t &A, const FE256_t &B, const FE256_t &C)
        {
            const FE512_t Temp(Negate(Reduce(Expand(B, C))), {});
            return Reduce(Addpartial(Temp, A, 0));
        }

        // Pre-computing inverted Kummer point coordinates.
        constexpr FE512_t Wrap(const FE512_t &Input)
        {
            const auto [X, Y, Z, W] = Input.as128();
            const auto A = Invert((Y * Z) * W) * X;
            const auto B = A * W;

            return { {}, { B * Z }, { B * Y }, { (Y * Z) * A} };
        }
        constexpr FE512_t Unwrap(const FE512_t &Input)
        {
            const auto [X, Y, Z, W] = Input.as128();
            return { (Y * Z) * W, Z * W, Y * W, (Y * Z) };
        }

        // Scalar pseudo-multiplication using a Montgomery ladder.
        constexpr std::pair<FE512_t, FE512_t> xDBLADD(FE512_t Xp, FE512_t Xq, const FE512_t &Xd)
        {
            constexpr FE512_t eHat{ std::span<const uint8_t>{{ 0x41, 0x03 }}, std::span<const uint8_t>{{ 0xC3, 0x09 }}, std::span<const uint8_t>{{ 0x51, 0x06 }}, std::span<const uint8_t>{{ 0x31, 0x02 }} };
            constexpr FE512_t e{ std::span<const uint8_t>{{ 0x72, 0x00 }}, std::span<const uint8_t>{{ 0x39, 0x00 }}, std::span<const uint8_t>{{ 0x42, 0x00 }}, std::span<const uint8_t>{{ 0xA2, 0x01 }} };

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

            const auto [lX, lY, lZ, lW] = Xq.as128();
            const auto [rX, rY, rZ, rW] = Xd.as128();

            return { Multiply4(Xp, e), {lX, lY * rY, lZ * rZ, lW * rW} };
        }
        constexpr FE512_t Ladder(FE512_t Xq, const FE512_t &Xd, const FE256_t &Scalars)
        {
            FE512_t Xp{ std::span<const uint8_t>{{ 0x0B, 0x00 }}, std::span<const uint8_t>{{ 0x16, 0x00 }}, std::span<const uint8_t>{{ 0x13, 0x00 }}, std::span<const uint8_t>{{ 0x03, 0x00 }} };
            uint8_t Previous{};

            for (int i = 250; i >= 0; i--)
            {
                const auto Bit = (Scalars[i >> 3] >> (i & 0x07)) & uint8_t(1);
                const auto Swap = Bit ^ Previous;
                Previous = Bit;
                NegX(&Xq);

                if (Swap == uint8_t(1)) std::swap(Xp, Xq);
                std::tie(Xp, Xq) = xDBLADD(Xp, Xq, Xd);
            }

            NegX(&Xp);

            if (Previous == uint8_t(1)) std::swap(Xp, Xq);
            return Xp;
        }
        constexpr FE512_t Ladder(const FE256_t &Scalars)
        {
            // Wrapped Kummer base-point.
            constexpr FE512_t WBP
            {
                std::span<const uint8_t>{},
                std::span<const uint8_t>{{ 0x48, 0x1A, 0x93, 0x4E, 0xA6, 0x51, 0xB3, 0xAE, 0xE7, 0xC2, 0x49, 0x20, 0xDC, 0xC3, 0xE0, 0x1B }},
                std::span<const uint8_t>{{ 0xDF, 0x36, 0x7E, 0xE0, 0x18, 0x98, 0x65, 0x64, 0x30, 0xA6, 0xAB, 0x8E, 0xCD, 0x16, 0xB4, 0x23 }},
                std::span<const uint8_t>{{ 0x1E, 0x44, 0x15, 0x72, 0x05, 0x3D, 0xAE, 0xC7, 0x4D, 0xA2, 0x47, 0x44, 0x38, 0x5C, 0xB3, 0x5D }}
            };

            return Ladder(Unwrap(WBP), WBP, Scalars);
        }
        #pragma endregion

        #pragma region Fields

        // Evaluate the polynomials at (L1, L2, Tau).
        constexpr std::pair<FE128_t, FE128_t> getK2(const FE128_t &L1, const FE128_t &L2, bool Tau)
        {
            FE128_t A, B;

            A = L1 * L2 * std::span<const uint8_t>{ { 0x11, 0x12 } };

            if (Tau)
            {
                B = L1 * std::span<const uint8_t>{ { 0xF7, 0x0D } };
                A += B;

                B = L2 * std::span<const uint8_t>{ { 0x99, 0x25 } };
                A -= B;
            }

            A *= std::span<const uint8_t>{ { 0xE3, 0x2F } };
            A += A;

            B = L1 * std::span<const uint8_t>{ { 0x33, 0x1D } };
            B *= B;
            A = B - A;

            B = L2 * std::span<const uint8_t>{ { 0xE3, 0x2F } };
            B *= B;
            A += B;;

            if (Tau)
            {
                B = std::span<const uint8_t>{ { 0x0B, 0x2C } };
                B *= B;
                A += B;
            }

            return { A, B };
        }
        constexpr std::tuple<FE128_t, FE128_t, FE128_t> getK3(const FE128_t &L1, const FE128_t &L2, bool Tau)
        {
            FE128_t A, B, C;

            A = L1 * L1;
            B = L2 * L2;

            if (Tau)
            {
                C = std::span<const uint8_t>{ { 0x01, 0x00 } };
                A += C;
                B += C;
                C = A + B;
            }

            A *= L2 * std::span<const uint8_t>{ { 0xF7, 0x0D } };
            B *= L1 * std::span<const uint8_t>{ { 0x99, 0x25 } };
            A -= B;

            if (Tau)
            {
                B = std::span<const uint8_t>{ { 0x01, 0x00 } };
                C -= B;
                C -= B;
                C *= std::span<const uint8_t>{ { 0x11, 0x12 } };
                A += C;
            }

            A *= std::span<const uint8_t>{ { 0xE3, 0x2F } };

            if (Tau)
            {
                B = L1 * L2 * (std::span<const uint8_t>{ { 0x79, 0x17 } } * std::span<const uint8_t>{ { 0xD7, 0xAB } });
                A -= B;
            }

            return { A, B, C };
        }
        constexpr std::pair<FE128_t, FE128_t> getK4(const FE128_t &L1, const FE128_t &L2, bool Tau)
        {
            FE128_t A, B;

            if (Tau)
            {
                A = L1 * std::span<const uint8_t>{ { 0x99, 0x25 } };
                B = L2 * std::span<const uint8_t>{ { 0xF7, 0x0D } };
                B -= A;

                B += std::span<const uint8_t>{ { 0x11, 0x12 } };
                B *= L1 * L2 * std::span<const uint8_t>{ { 0xE3, 0x2F } };
                B += B;

                A = L1 * std::span<const uint8_t>{ { 0xE3, 0x2F } };
                A *= A;
                B = A - B;

                A = L2 * std::span<const uint8_t>{ { 0x33, 0x1D } };
                A *= A;
                B += A;
            }

            A = L1 * L2 * std::span<const uint8_t>{ { 0x0B, 0x2C } };
            A *= A;

            if (Tau) A += B;

            return { A, B };
        }

        // (Bjj * R1^2) - (2 * C * Bij * R1 * R2) + (Bii * R2^2) == 0
        constexpr bool isQuad(const FE128_t &Bij, const FE128_t &Bjj, const FE128_t &Bii, const FE128_t &R1, const FE128_t &R2)
        {
            constexpr FE128_t One = std::span<const uint8_t>{ {1} };
            constexpr FE128_t Const = std::span<const uint8_t>
            {{
                    0x43, 0xA8, 0xDD, 0xCD,
                    0xD8, 0xE3, 0xF7, 0x46,
                    0xDD, 0xA2, 0x20, 0xA3,
                    0xEF, 0x0E, 0xF5, 0x40
            }};

            const auto A = (Bjj * R1 * R1);
            const auto B = (Const * Bij * R1 * R2) + (Const * Bij * R1 * R2);
            const auto C = (Bii * R2 * R2);

            const auto Result = Freeze(One + (A - B + C));
            return Result.Raw == One.Raw;
        }
        #pragma endregion

        #pragma region Matrises

        // Mult by k-hat and mu-hat.
        constexpr FE128_t kRow(const FE128_t &X1, const FE128_t &X2, const FE128_t &X3, const FE128_t &X4)
        {
            return
                (X2 * std::span<const uint8_t>{ { 0x80, 0x00 }}) +
                (X3 * std::span<const uint8_t>{ { 0x39, 0x02 }}) +
                (X4 * std::span<const uint8_t>{ { 0x49, 0x04 }}) -
                (X1 * std::span<const uint8_t>{ { 0xC1, 0x03 }});
        }
        constexpr FE128_t mRow(const FE128_t &X1, const FE128_t &X2, const FE128_t &X3, const FE128_t &X4)
        {
            return
                ((X2 + X2) - X1) *
                (     std::span<const uint8_t>{ { 0x0B, 0x00 }}) +
                (X3 * std::span<const uint8_t>{ { 0x13, 0x00 }}) +
                (X4 * std::span<const uint8_t>{ { 0x03, 0x00 }});
        }

        // Bi-quadratic form.
        constexpr FE512_t biiValues(const FE512_t &P, const FE512_t &Q)
        {
            constexpr FE512_t muHat{ std::span<const uint8_t>{{ 0x21, 0x00 }}, std::span<const uint8_t>{{ 0x0B, 0x00 }}, std::span<const uint8_t>{{ 0x11, 0x00 }}, std::span<const uint8_t>{{ 0x31, 0x00 }} };
            constexpr FE512_t eHat{ std::span<const uint8_t>{{ 0x41, 0x03 }}, std::span<const uint8_t>{{ 0xC3, 0x09 }}, std::span<const uint8_t>{{ 0x51, 0x06 }}, std::span<const uint8_t>{{ 0x31, 0x02 }} };
            constexpr FE512_t k{ std::span<const uint8_t>{{ 0x59, 0x12 }}, std::span<const uint8_t>{{ 0x3F, 0x17 }}, std::span<const uint8_t>{{ 0x79, 0x16 }}, std::span<const uint8_t>{{ 0xC7, 0x07 }} };

            auto T0 = Multiply4(Square4(P), eHat); NegX(&T0);
            auto T1 = Multiply4(Square4(Q), eHat); NegX(&T1);

            const auto T2 = [&]() -> FE512_t
            {
                const auto [lX, lY, lZ, lW] = T0.as128();
                const auto [rX, rY, rZ, rW] = T1.as128();

                return {
                    Dotproduct({ lX, lY, lZ, lW }, { rX, rY, rZ, rW }),
                    Dotproduct({ lX, lY, lZ, lW }, { rY, rX, rW, rZ }),
                    Dotproduct({ lX, lZ, lY, lW }, { rZ, rX, rW, rY }),
                    Dotproduct({ lX, lW, lY, lZ }, { rW, rX, rZ, rY })
                };
            }();
            const auto T3 = [&]() -> FE512_t
            {
                const auto [X, Y, Z, W] = T2.as128();

                return {
                    negDotproduct({ X, Y, Z, W }, k),
                    negDotproduct({ Y, X, W, Z }, k),
                    negDotproduct({ Z, W, X, Y }, k),
                    negDotproduct({ W, Z, Y, X }, k)
                };

            }();

            auto Result = Multiply4(T3, muHat);
            NegX(&Result);
            return Result;
        }
        constexpr FE128_t bijValues(const FE512_t &P, const FE512_t &Q, const FE512_t &C)
        {
            const auto [P1, P2, P3, P4] = P.as128();
            const auto [Q1, Q2, Q3, Q4] = Q.as128();
            const auto [C1, C2, C3, C4] = C.as128();
            FE128_t TempX, TempY, TempZ;
            FE128_t Result;

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
        #pragma endregion

        // So we can transfer the points in a more manageable form.
        constexpr std::tuple<FE128_t, FE128_t> Compress(const FE512_t &Input)
        {
            const auto [X, Y, Z, W] = Input.as128();

            FE128_t L1, L2, L3, L4;

            // Matrix multiplication by kHat.
            const std::array<FE128_t, 4> Premult = { kRow(W, Z, Y, X), kRow(Z, W, X, Y),
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

            L1 = Freeze(L1);
            L2 = Freeze(L2);
            L3 = Freeze(L3);

            L1[15] |= uint8_t(uint8_t(Tau) << 7);
            L2[15] |= uint8_t((L3[0] & uint8_t(1)) << 7);

            return { L1, L2 };
        }
        constexpr FE512_t Decompress(const FE256_t &Input)
        {
            FE128_t T0, T1, T2, T3, T4, T5;
            auto [L1, L2] = Input.as128();

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
                T2 = Freeze(T2);

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
                const auto Root = SQRT(T4, Sigma);
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
        constexpr bool Check(FE512_t &P, FE512_t &Q, const FE256_t &Rcomp)
        {
            constexpr FE512_t muHat{ std::span<const uint8_t>{{ 0x21, 0x00 }}, std::span<const uint8_t>{{ 0x0B, 0x00 }}, std::span<const uint8_t>{{ 0x11, 0x00 }}, std::span<const uint8_t>{{ 0x31, 0x00 }} };

            // Invalid compression.
            auto R = Decompress(Rcomp);
            if (isZero(R)) return false;

            NegX(&P); P = Hadamard(P); NegW(&P);
            NegX(&Q); Q = Hadamard(Q); NegW(&Q);
            NegX(&R); R = Hadamard(R); NegW(&R);

            const auto [PX, PY, PZ, PW] = P.as128();
            const auto [QX, QY, QZ, QW] = Q.as128();
            const auto [RX, RY, RZ, RW] = R.as128();
            const auto [muX, muY, muZ, muW] = muHat.as128();
            const auto [BiiX, BiiY, BiiZ, BiiW] = biiValues(P, Q).as128();

            // B_1,2
            auto Bij = bijValues(P, Q, muHat);
            if (!isQuad(Bij, BiiY, BiiX, RX, RY)) return false;

            // B_1,3
            Bij = bijValues({ PX, PZ, PY, PW }, { QX, QZ, QY, QW }, { muX, muZ, muY, muW });
            if (!isQuad(Bij, BiiZ, BiiX, RX, RZ)) return false;

            // B_1,4
            Bij = bijValues({ PX, PW, PY, PZ }, { QX, QW, QY, QZ }, { muX, muW, muY, muZ });
            if (!isQuad(Bij, BiiW, BiiX, RX, RW)) return false;

            // B_2,3
            Bij = bijValues({ PY, PZ, PX, PW }, { QY, QZ, QX, QW }, { muY, muZ, muX, muW });
            Bij = Negate(Bij);
            if (!isQuad(Bij, BiiZ, BiiY, RY, RZ)) return false;

            // B_2,4
            Bij = bijValues({ PY, PW, PX, PZ }, { QY, QW, QX, QZ }, { muY, muW, muX, muZ });
            Bij = Negate(Bij);
            if (!isQuad(Bij, BiiW, BiiY, RY, RW)) return false;

            // B_3,4
            Bij = bijValues({ PZ, PW, PX, PY }, { QZ, QW, QX, QY }, { muZ, muW, muX, muY });
            Bij = Negate(Bij);
            if (!isQuad(Bij, BiiW, BiiZ, RZ, RW)) return false;

            return true;
        }
    }

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

    // Create a keypair from a random seed.
    constexpr Publickey_t getPublickey(const Privatekey_t &Privatekey)
    {
        using namespace Internal;

        auto Scalar = getScalar(Privatekey);
        auto Ladders = Ladder(Scalar);
        const auto [X, Y] = Compress(Ladders);
        return FE256_t{ X, Y }.Raw;
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
        using namespace Internal;

        auto PK = Decompress(std::span(Publickey));
        const auto PKW = Wrap(PK);

        const auto Secret = Ladder(PK, PKW, getScalar(Privatekey));
        const auto [A, B] = Compress(Secret);
        return A.Raw + B.Raw;
    }

    // Create a signature for the provided message, somewhat hardened against hackery.
    template <cmp::Range_t C> constexpr Signature_t Sign(const Publickey_t &Publickey, const Privatekey_t &Privatekey, const C &Message)
    {
        using namespace Internal;
        const auto Buffer = cmp::getBytes(Message);

        // Get the first point.
        const auto P = [&]()
        {
            // 32 bytes of deterministic 'randomness'.
            Blob_t Local; Local.reserve(Buffer.size() + 32);
            Local.append(Hash::SHA512(Privatekey), 8, 32);
            Local.append(Buffer);

            return getScalar(Hash::SHA512(Local));
        }();
        const auto [PZ, PW] = Compress(Ladder(P));

        // Get the second point.
        const auto Q = [&]()
        {
            Blob_t Local; Local.reserve(16 + 16 + 32 + Buffer.size());
            Local.append(PZ.Raw.data(), PZ.Raw.size());
            Local.append(PW.Raw.data(), PW.Raw.size());
            Local.append(Publickey);
            Local.append(Buffer);

            return getScalar(Hash::SHA512(Local));
        }();

        // Get the third point.
        const auto W = opsScalar(P, getPositive(Q), getScalar(Privatekey));

        // Share with the world..
        return PZ.Raw + PW.Raw + W.Raw;
    }

    // Verify that the message was signed by the owner of the public key.
    template <cmp::Range_t C> constexpr bool Verify(const Publickey_t &Publickey, const Signature_t &Signature, const C &Message)
    {
        using namespace Internal;
        const auto Buffer = cmp::getBytes(Message);

        // Validate compression of the first point.
        auto P = Decompress(FE256_t{ Publickey });
        if (isZero(P)) [[unlikely]] return false;

        // Second point.
        const auto Q = [&]()
        {
            Blob_t Local; Local.reserve(64 + Buffer.size());
            Local.append(Signature, 0, 32);
            Local.append(Publickey);
            Local.append(Buffer);

            return getScalar(Hash::SHA512(Local));
        }();

        // Third point.
        const auto [High, Low] = FE512_t(Signature).as256();
        auto W = Ladder(P, Wrap(P), Q);
        P = Ladder(getScalar(Low.Raw));

        return Check(P, W, High);
    }
}
