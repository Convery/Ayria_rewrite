/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-08-11
    License: MIT

    Quite OK Image Format (https://qoiformat.org)
	Modified to ignore the stream-termination token.
*/

#pragma once
#include <Utilities/Utilities.hpp>

namespace QOI
{
    #pragma pack (push, 1)
    struct Header_t
    {
        uint32_t Magic;
        uint32_t Width;
        uint32_t Height;
		uint8_t Channels;	// 3 = RGB, 4 = RGBA
        uint8_t Colorspace;	// 0 = sRGB, 1 = linear
    };
    #pragma pack (pop)

	constexpr size_t Decodesize(size_t Width, size_t Height, size_t Channels)
	{
		return Width * Height * Channels;
	}
	constexpr size_t Decodesize(const Header_t &Header)
	{
		return Decodesize(Header.Width, Header.Height, Header.Channels);
	}

	constexpr uint8_t OP_Mask = 0b11000000;
	enum OP_t : uint8_t
	{
		OP_INDEX = 0b00000000,
		OP_DELTA = 0b01000000,
		OP_LUMA  = 0b10000000,
		OP_RUN   = 0b11000000,
		OP_RGB   = 0b11111110,
		OP_RGBA  = 0b11111111
	};

	union Pixel_t
	{
		struct { uint8_t R, G, B, A; };
		uint32_t RAW;
	};
	constexpr uint8_t Hash(const Pixel_t &Pixel)
	{
		return ((Pixel.R * 3) + (Pixel.G * 5) + (Pixel.B * 7) + (Pixel.A * 11)) & 63;
	}

	// Compiletime decode.
	template <size_t Width, size_t Height, size_t Channels>
	constexpr cmp::Array_t<uint8_t, Decodesize(Width, Height, Channels)> Decode_fixed(Blob_view_t Data)
	{
		cmp::Array_t<uint8_t, Decodesize(Width, Height, Channels)> Result{};
		Pixel_t Current{ .A = 0xFF };
		Pixel_t Memory[64]{};
		uint8_t Runlength{};
		size_t Output{};

		// Skip the header if provided.
		if (Data[0] == 0x66 && Data[1] == 0x69 && Data[2] == 0x6F && Data[3] == 0x71)
			Data.remove_prefix(14);

		for (size_t Input = 0; Input < Data.size(); )
		{
			if (Runlength) --Runlength;
			else
			{
				const uint8_t ID = Data[Input++];

				if (ID == OP_RGB)
				{
					Current.R = Data[Input++];
					Current.G = Data[Input++];
					Current.B = Data[Input++];
				}
				else if (ID == OP_RGBA)
				{
					Current.R = Data[Input++];
					Current.G = Data[Input++];
					Current.B = Data[Input++];
					Current.A = Data[Input++];
				}
				else if ((ID & OP_Mask) == OP_INDEX)
				{
					Current = Memory[ID];
				}
				else if ((ID & OP_Mask) == OP_DELTA)
				{
					Current.R += ((ID >> 4) & 0x03) - 2;
					Current.G += ((ID >> 2) & 0x03) - 2;
					Current.B += (ID & 0x03) - 2;
				}
				else if ((ID & OP_Mask) == OP_LUMA)
				{
					const auto Delta = Data[Input++];
					const auto vg = (ID & 0x3f) - 32;
					Current.R += vg - 8 + ((Delta >> 4) & 0x0f);
					Current.G += vg;
					Current.B += vg - 8 + (Delta & 0x0f);
				}
				else if ((ID & OP_Mask) == OP_RUN)
				{
					Runlength = (ID & 0x3f);
				}

				Memory[Hash(Current)] = Current;
			}

			Result[Output++] = Current.R;
			Result[Output++] = Current.G;
			Result[Output++] = Current.B;
			if constexpr (Channels == 4)
			{
				Result[Output++] = Current.A;
			}
		}

		Result[Output++] = Current.R;
		Result[Output++] = Current.G;
		Result[Output++] = Current.B;
		if constexpr (Channels == 4)
		{
			Result[Output] = Current.A;
		}

		return Result;
	}

	// Runtime decode.
	inline Blob_t Decode(Blob_view_t Data, const Header_t &Header)
	{
		Pixel_t Current{ .A = 0xFF };
		Pixel_t Memory[64]{};
		uint8_t Runlength{};
		size_t Output{};

		assert(Header.Channels <= 4 && Header.Channels >= 3);
		assert(Header.Width && Header.Width);
		assert(Header.Magic == 'qoif');

		Blob_t Result(Decodesize(Header), {});

		for (size_t Input = 0; Input < Data.size(); )
		{
			if (Runlength) --Runlength;
			else
			{
				const uint8_t ID = Data[Input++];

				if (ID == OP_RGB)
				{
					Current.R = Data[Input++];
					Current.G = Data[Input++];
					Current.B = Data[Input++];
				}
				else if (ID == OP_RGBA)
				{
					Current.R = Data[Input++];
					Current.G = Data[Input++];
					Current.B = Data[Input++];
					Current.A = Data[Input++];
				}
				else if ((ID & OP_Mask) == OP_INDEX)
				{
					Current = Memory[ID];
				}
				else if ((ID & OP_Mask) == OP_DELTA)
				{
					Current.R += ((ID >> 4) & 0x03) - 2;
					Current.G += ((ID >> 2) & 0x03) - 2;
					Current.B += (ID & 0x03) - 2;
				}
				else if ((ID & OP_Mask) == OP_LUMA)
				{
					const auto Delta = Data[Input++];
					const auto vg = (ID & 0x3f) - 32;
					Current.R += vg - 8 + ((Delta >> 4) & 0x0f);
					Current.G += vg;
					Current.B += vg - 8 + (Delta & 0x0f);
				}
				else if ((ID & OP_Mask) == OP_RUN)
				{
					Runlength = (ID & 0x3f);
				}

				Memory[Hash(Current)] = Current;
			}

			Result[Output++] = Current.R;
			Result[Output++] = Current.G;
			Result[Output++] = Current.B;
			if (Header.Channels == 4)
			{
				Result[Output++] = Current.A;
			}
		}

		Result[Output++] = Current.R;
		Result[Output++] = Current.G;
		Result[Output++] = Current.B;
		if (Header.Channels == 4)
		{
			Result[Output] = Current.A;
		}

		return Result;
	}
	inline Blob_t Decode(Blob_view_t Data, Header_t *Description = nullptr)
	{
		const auto Header = *(Header_t *)Data.data();
		if (Description) *Description = Header;

		return Decode(Data.substr(16), Header);
	}

	// Runtime encode.
	inline Blob_t Encode(Blob_view_t Data, Header_t Description)
	{
		assert(Data.size() == (Description.Width * Description.Height * Description.Channels));
		assert(Description.Width && Description.Height);
		assert(Description.Colorspace <= 1);

		// Ensure that the description contains the identifier.
		Description.Magic = 'qoif';

		Blob_t Result((uint8_t *)&Description, sizeof(Header_t));
		Pixel_t Previous{ .A = 0xFF };
		Pixel_t Current{ .A = 0xFF };
		Pixel_t Memory[64]{};
		uint8_t Runlength{};

		// Reserve for the worst case.
		const auto Max = Description.Width * Description.Height * (Description.Channels + 1);
		Result.reserve(Result.size() + Max);

		for (size_t i = 0; i < Data.size(); i += Description.Channels)
		{
			Current.R = Data[i + 0];
			Current.G = Data[i + 1];
			Current.B = Data[i + 2];
			if (Description.Channels == 4)
			{
				Current.A = Data[i + 3];
			}

			if (Current.RAW == Previous.RAW)
			{
				Runlength++;
				if (Runlength == 62)
				{
					Result.push_back(OP_RUN | (Runlength - 1));
					Runlength = 0;
				}
			}
			else
			{
				if (Runlength)
				{
					Result.push_back(OP_RUN | (Runlength - 1));
					Runlength = 0;
				}

				const auto Index = Hash(Current);
				if (Memory[Index].RAW == Current.RAW)
				{
					Result.push_back(OP_INDEX | Index);
				}
				else
				{
					Memory[Index] = Current;

					if (Current.A == Previous.A)
					{
						const int8_t vr = Current.R - Previous.R;
						const int8_t vg = Current.G - Previous.G;
						const int8_t vb = Current.B - Previous.B;
						const int8_t vg_r = vr - vg;
						const int8_t vg_b = vb - vg;

						if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2)
						{
							Result.push_back(OP_DELTA | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2));
						}
						else if (vg_r > -9 && vg_r < 8 && vg > -33 && vg < 32 && vg_b > -9 && vg_b < 8)
						{
							Result.push_back(OP_LUMA | (vg + 32));
							Result.push_back((vg_r + 8) << 4 | (vg_b + 8));
						}
						else
						{
							Result.push_back(OP_RGB);
							Result.push_back(Current.R);
							Result.push_back(Current.G);
							Result.push_back(Current.B);
						}
					}
					else
					{
						Result.push_back(OP_RGBA);
						Result.push_back(Current.R);
						Result.push_back(Current.G);
						Result.push_back(Current.B);
						Result.push_back(Current.A);
					}
				}
			}

			Previous = Current;
		}

		if (Runlength) Result.push_back(OP_RUN | (Runlength - 1));
		return Result;
	}
}
