#include "Core.h"
#include "UnCore.h"
#include "BC7PrepDecoder.h"

#if WUTHERING_WAVES

#define MODE_COUNT 10
#define FLAG_SPLIT0 1
#define FLAG_SWITCH_COLORSPACE (1 << 16)

static const int ModeSizes[MODE_COUNT] = { 16, 16, 16, 16, 16, 16, 16, 16, 16, 4 };
static const int SplitPoint[MODE_COUNT] = { 8, 8, 12, 12, 6, 8, 8, 12, 0, 0 };

static FORCEINLINE uint64 BitExtract(uint64 val, int start, int width)
{
	if (width == 0) return 0;
	uint64 mask = (1ULL << (width & 63)) - 1;
	return (val >> start) & mask;
}

static FORCEINLINE uint64 PackedAdd(uint64 a, uint64 b, uint64 msbMask, uint64 nonMsbMask)
{
	uint64 low = (a & nonMsbMask) + (b & nonMsbMask);
	return low ^ ((a ^ b) & msbMask);
}

static FORCEINLINE uint64 PackedAdd(uint64 a, uint64 b, uint64 msbMask)
{
	return PackedAdd(a, b, msbMask, ~msbMask);
}

static FORCEINLINE void DecorrToRgbPacked(uint64& r, uint64& g, uint64& b, uint64 msbMask, uint64 nonMsbMask)
{
	uint64 y = r; uint64 cr = g; uint64 cb = b;
	r = PackedAdd(y, cr, msbMask, nonMsbMask);
	g = y;
	b = PackedAdd(y, cb, msbMask, nonMsbMask);
}

static FORCEINLINE void DecorrToRgbPacked(uint64& r, uint64& g, uint64& b, uint64 msbMask)
{
	DecorrToRgbPacked(r, g, b, msbMask, ~msbMask);
}

static FORCEINLINE void DecorrToRgbScalar(int& y, int& cr, int& cb, int mask)
{
	int r = (y + cr) & mask;
	int g = y;
	int b = (y + cb) & mask;
	y = r; cr = g; cb = b;
}

static FORCEINLINE uint64 Mode4DecorrFast(uint64 rgba)
{
	uint64 ybroad = ((uint32)rgba & 0x3ffULL) * (1ULL + (1ULL << 10) + (1ULL << 20));
	uint64 crcba = ((rgba >> 10) & 0x3ff) | (rgba & 0xfffff00000ULL);
	return PackedAdd(ybroad, crcba, 0x8421084210ULL);
}

static FORCEINLINE uint32 Compact32To7_2x(uint64 x)
{
	return ((uint32)x & 0x7f) | ((uint32)(x >> 25) & 0x3f80);
}

static FORCEINLINE uint32 Compact24To7_3x(uint64 x)
{
	return ((uint32)x & 0x7f) | ((uint32)(x >> 17) & 0x3f80) | ((uint32)(x >> 34) & 0x1fc000);
}

static FORCEINLINE uint32 Compact24To7_2x(uint64 x)
{
	return ((uint32)x & 0x7f) | ((uint32)(x >> 17) & 0x3f80);
}

static FORCEINLINE uint32 Compact16To1_4x(uint64 x)
{
	x &= 0x0001000100010001ULL;
	x *= 0x0001000200040008ULL;
	return (uint32)(x >> 48);
}

static FORCEINLINE uint32 Compact16To1_2x(uint32 x)
{
	return (x & 1) | ((x >> 15) & 2);
}

static FORCEINLINE uint64 Compact16To5_4x(uint64 x)
{
	x &= 0x001f001f001f001fULL;
	x = ((x >> 11) | x) & 0x000003ff000003ffULL;
	x = ((x >> 22) | x) & 0xfffff;
	return x;
}

static FORCEINLINE uint32 Compact16To5_2x(uint32 x)
{
	return (x & 0x1f) | ((x >> 11) & 0x3e0);
}

static FORCEINLINE uint64 Compact16To6_4x(uint64 x)
{
	x = ((x >> 10) | x) & 0x00000fff00000fffULL;
	x = ((x >> 20) | x) & 0xffffffULL;
	return x;
}

static FORCEINLINE uint64 Compact8To7_8x(uint64 x)
{
	const uint64 stay1 = 0x007f007f007f007fULL;
	const uint64 move1 = stay1 << 8;
	x = ((x & move1) >> 1) | (x & stay1);
	const uint64 stay2 = 0x00003fff00003fffULL;
	x = ((x & ~stay2) >> 2) | (x & stay2);
	const uint64 stay4 = 0x000000000fffffffULL;
	x = ((x & ~stay4) >> 4) | (x & stay4);
	return x;
}

static FORCEINLINE uint64 Compact8To7_6x(uint64 x)
{
	const uint64 stay1 = 0x007f007f007fULL;
	const uint64 move1 = stay1 << 8;
	x = ((x & move1) >> 1) | (x & stay1);
	x = ((x >> 0) & 0x0000000ffffULL)
	  | ((x >> 2) & 0x0000fffc000ULL)
	  | ((x >> 4) & 0x3fff0000000ULL);
	return x;
}

static FORCEINLINE uint32 Compact8To1_8x(uint64 x)
{
	x &= 0x0101010101010101ULL;
	x *= 0x0102040810204080ULL;
	return (uint32)(x >> 56);
}

static FORCEINLINE uint64 Expand4To5_12x(uint64 x)
{
	x = ((x & 0x0000ffff00000000ULL) << 8)
	  | ((x & 0x00000000ffff0000ULL) << 4)
	  | (x & 0x000000000000ffffULL);
	const uint64 stay2 = 0x0000ff000ff000ffULL;
	x = ((x & ~stay2) << 2) | (x & stay2);
	x += x & 0x03c0f03c0f03c0f0ULL;
	return x;
}

static FORCEINLINE uint64 Expand4To5_4x(uint64 x)
{
	x = ((x & 0x0ff00) << 2) | (x & 0x000ff);
	x += x & 0x3c0f0;
	return x;
}

static FORCEINLINE uint32 Expand2To6_4x(uint32 x)
{
	x = ((x << 8) | x) & 0x0f00f;
	x = ((x << 4) | x) & 0xc30c3;
	return x;
}

static FORCEINLINE uint64 Expand1To5_12x(uint64 x)
{
	x &= 0xfff;
	x = (x * 0x100010001ULL) & 0xf0000f0000fULL;
	x = (x * 0x1111) & 0x084210842108421ULL;
	return x;
}

static FORCEINLINE uint64 Expand1To5_4x(uint64 x)
{
	return ((uint32)(x & 0xf) * 0x1111u) & 0x8421;
}

static FORCEINLINE void WriteBlock(byte* output, int idx, uint64 lo, uint64 hi)
{
	int destOffset = idx * 16;
	memcpy(output + destOffset, &lo, 8);
	memcpy(output + destOffset + 8, &hi, 8);
}

static FORCEINLINE uint64 Get64(const byte* buf, int offset)
{
	uint64 v;
	memcpy(&v, buf + offset, 8);
	return v;
}

static FORCEINLINE uint32 Get32(const byte* buf, int offset)
{
	uint32 v;
	memcpy(&v, buf + offset, 4);
	return v;
}

static FORCEINLINE uint16 Get16(const byte* buf, int offset)
{
	uint16 v;
	memcpy(&v, buf + offset, 2);
	return v;
}

static void UnMungeMode0(byte* output, const byte* payload, int firstOffset, int secondOffset,
	int stride0, int stride1, const TArray<int>& indices, bool switchColorspace)
{
	for (int i = 0; i < indices.Num(); i++)
	{
		uint64 inLo = Get64(payload, firstOffset + i * stride0);
		uint64 inHi = Get64(payload, secondOffset + i * stride1);

		uint64 rbits = BitExtract(inLo, 0, 24);
		uint64 gbits = BitExtract(inLo, 24, 24);
		uint64 bbits = BitExtract(inLo, 48, 16) | (BitExtract(inHi, 0, 8) << 16);
		uint64 partbits = BitExtract(inHi, 8, 4);

		if (switchColorspace)
			DecorrToRgbPacked(rbits, gbits, bbits, 0x888888ULL);

		uint64 lo = 1ULL;
		lo |= partbits << 1;
		lo |= rbits << 5;
		lo |= gbits << 29;
		lo |= bbits << 53;
		uint64 hi = bbits >> 11;
		hi |= inHi & ~0x1fffULL;

		WriteBlock(output, indices[i], lo, hi);
	}
}

static void UnMungeMode1(byte* output, const byte* payload, int firstOffset, int secondOffset,
	int stride0, int stride1, const TArray<int>& indices, bool switchColorspace)
{
	for (int i = 0; i < indices.Num(); i++)
	{
		uint64 rgbs = Get64(payload, firstOffset + i * stride0);
		uint64 extra = Get64(payload, secondOffset + i * stride1);

		uint64 rbits = Compact16To6_4x((rgbs >> 0) & 0x003f003f003f003fULL);
		uint64 gbits = Compact16To6_4x((rgbs >> 5) & 0x003e003e003e003eULL);
		uint64 bbits = Compact16To6_4x((rgbs >> 10) & 0x003e003e003e003eULL);

		uint32 expandedGb = Expand2To6_4x((uint32)(extra & 0xff));
		gbits |= (uint64)((expandedGb >> 0) & 0x41041);
		bbits |= (uint64)((expandedGb >> 1) & 0x41041);

		if (switchColorspace)
			DecorrToRgbPacked(rbits, gbits, bbits, 0x820820ULL);

		uint64 lo = 0x2ULL;
		lo |= (extra >> 6) & 0xfc;
		lo |= rbits << 8;
		lo |= gbits << 32;
		lo |= bbits << 56;
		uint64 hi = bbits >> 8;
		hi |= extra & ~0xffffULL;

		WriteBlock(output, indices[i], lo, hi);
	}
}

static void UnMungeMode2(byte* output, const byte* payload, int firstOffset, int secondOffset,
	int stride0, int stride1, const TArray<int>& indices, bool switchColorspace)
{
	for (int i = 0; i < indices.Num(); i++)
	{
		uint64 endpt0 = Get64(payload, firstOffset + i * stride0);
		uint32 endpt1 = Get32(payload, firstOffset + i * stride0 + 8);
		uint32 index = Get32(payload, secondOffset + i * stride1);

		uint64 rbits = Compact16To5_4x(endpt0 >> 1) | ((uint64)Compact16To5_2x(endpt1 >> 1) << 20);
		uint64 gbits = Compact16To5_4x(endpt0 >> 6) | ((uint64)Compact16To5_2x(endpt1 >> 6) << 20);
		uint64 bbits = Compact16To5_4x(endpt0 >> 11) | ((uint64)Compact16To5_2x(endpt1 >> 11) << 20);
		uint64 partbits = Compact16To1_4x(endpt0) | ((uint64)Compact16To1_2x(endpt1) << 4);

		if (switchColorspace)
			DecorrToRgbPacked(rbits, gbits, bbits, 0x21084210ULL);

		uint64 lo = 0x4ULL;
		lo |= partbits << 3;
		lo |= rbits << 9;
		lo |= gbits << 39;
		uint64 hi = gbits >> 25;
		hi |= bbits << 5;
		hi |= (uint64)(index & ~7u) << 32;

		WriteBlock(output, indices[i], lo, hi);
	}
}

static void UnMungeMode3(byte* output, const byte* payload, int firstOffset, int secondOffset,
	int stride0, int stride1, const TArray<int>& indices, bool switchColorspace)
{
	for (int i = 0; i < indices.Num(); i++)
	{
		uint64 endpt0 = Get64(payload, firstOffset + i * stride0);
		uint32 endpt1 = Get32(payload, firstOffset + i * stride0 + 8);
		uint32 index = Get32(payload, secondOffset + i * stride1);

		uint64 rbits = Compact24To7_3x(endpt0 >> 0) | (((uint64)endpt1 & 0x007f00ULL) << (21 - 8));
		uint64 gbits = Compact24To7_3x(endpt0 >> 8) | (((uint64)endpt1 & 0x7f0000ULL) << (21 - 16));
		uint64 bbits = Compact24To7_2x(endpt0 >> 16) | ((uint64)Compact24To7_2x(endpt1) << 14);
		uint64 partbits = Compact8To1_8x(endpt0 >> 7);

		if (switchColorspace)
			DecorrToRgbPacked(rbits, gbits, bbits, 0x8102040ULL);

		uint64 lo = 0x8ULL;
		lo |= (partbits & 0x3f) << 4;
		lo |= rbits << 10;
		lo |= gbits << 38;
		uint64 hi = gbits >> 26;
		hi |= bbits << 2;
		hi |= (uint64)(partbits & 0xc0) << 24;
		hi |= (uint64)index << 32;

		WriteBlock(output, indices[i], lo, hi);
	}
}

static void UnMungeMode4(byte* output, const byte* payload, int firstOffset, int secondOffset,
	int stride0, int stride1, const TArray<int>& indices, bool switchColorspace)
{
	for (int i = 0; i < indices.Num(); i++)
	{
		int loPtr = firstOffset + i * stride0;
		uint64 inLo = (uint64)Get32(payload, loPtr) | ((uint64)Get16(payload, loPtr + 4) << 32);
		int secondPtr = secondOffset + i * stride1;
		uint64 inHi0 = Get16(payload, secondPtr);
		uint64 inHi1 = Get64(payload, secondPtr + 2);

		uint64 rgba = BitExtract(inLo, 2, 40);
		if (switchColorspace)
			rgba = Mode4DecorrFast(rgba);

		uint64 crot = BitExtract(inHi0, 0, 2);
		int shiftAmount = (int)(((0ULL - crot) & 3) * 10);

		uint64 xorMask = (rgba ^ (rgba << shiftAmount)) & 0xffc0000000ULL;
		rgba ^= xorMask;
		rgba ^= xorMask >> shiftAmount;

		rgba += rgba & 0x0ffc0000000ULL;
		rgba += rgba & 0x1f000000000ULL;

		rgba |= ((((uint64)(inLo & 3) * 0x21ULL) & 0x41ULL) * 1ULL) << 30;

		uint64 lo = 0x10ULL;
		lo |= crot << 5;
		lo |= BitExtract(inLo, 42, 1) << 7;
		lo |= rgba << 8;
		lo |= BitExtract(inHi0, 2, 14) << 50;
		uint64 hi = inHi1;

		WriteBlock(output, indices[i], lo, hi);
	}
}

static void UnMungeMode5(byte* output, const byte* payload, int firstOffset, int secondOffset,
	int stride0, int stride1, const TArray<int>& indices, bool switchColorspace)
{
	for (int i = 0; i < indices.Num(); i++)
	{
		uint64 endpoints = Get64(payload, firstOffset + i * stride0);
		uint64 indicesBits = Get64(payload, secondOffset + i * stride1);

		uint64 lo = 0x20ULL;
		uint64 hi;

		if (switchColorspace)
		{
			uint64 rbits = (endpoints >> 0) & 0x000000fe000000feULL;
			uint64 gbits = (endpoints >> 8) & 0x000000fe000000feULL;
			uint64 bbits = (endpoints >> 16) & 0x000000fe000000feULL;
			DecorrToRgbPacked(rbits, gbits, bbits, 0x8000000080ULL, 0x7e0000007eULL);
			endpoints = rbits | (gbits << 8) | (bbits << 16) | (endpoints & 0xff010101ff010101ULL);

			uint32 crot = (uint32)indicesBits & 3;
			int shiftAmount = (int)(((0u - crot) & 3) << 3);

			uint64 xorMask = (endpoints ^ (endpoints << shiftAmount)) & 0xff000000ff000000ULL;
			endpoints ^= xorMask;
			endpoints ^= xorMask >> shiftAmount;

			lo |= (uint64)crot << 6;
			lo |= (uint64)Compact32To7_2x(endpoints >> 1) << 8;
			lo |= (uint64)Compact32To7_2x(endpoints >> 9) << 22;
			lo |= (uint64)Compact32To7_2x(endpoints >> 17) << 36;
			lo |= ((endpoints >> 24) & 0xffULL) << 50;
			lo |= ((endpoints >> 56) & 0x3fULL) << 58;
		}
		else
		{
			uint32 crot = (uint32)indicesBits & 3;
			int shiftAmount = (int)(((0u - crot) & 3) << 4);

			uint64 xorMask = (endpoints ^ (endpoints << shiftAmount)) & 0xffff000000000000ULL;
			endpoints ^= xorMask;
			endpoints ^= xorMask >> shiftAmount;

			lo |= (uint64)crot << 6;
			lo |= Compact8To7_6x(endpoints >> 1) << 8;
			lo |= ((endpoints >> 48) & 0xffULL) << 50;
			lo |= ((endpoints >> 56) & 0x3fULL) << 58;
		}

		hi = (endpoints >> 62) & 3;
		hi |= indicesBits & ~3ULL;

		WriteBlock(output, indices[i], lo, hi);
	}
}

static void UnMungeMode6(byte* output, const byte* payload, int firstOffset, int secondOffset,
	int stride0, int stride1, const TArray<int>& indices, bool switchColorspace)
{
	for (int i = 0; i < indices.Num(); i++)
	{
		uint64 endpoints = Get64(payload, firstOffset + i * stride0);
		uint64 indicesBits = Get64(payload, secondOffset + i * stride1);

		uint64 lo = 0x40ULL;

		if (switchColorspace)
		{
			uint64 rbits = (uint64)Compact32To7_2x(endpoints >> 0);
			uint64 gbits = (uint64)Compact32To7_2x(endpoints >> 8);
			uint64 bbits = (uint64)Compact32To7_2x(endpoints >> 16);
			uint64 abits = (uint64)Compact32To7_2x(endpoints >> 24);
			DecorrToRgbPacked(rbits, gbits, bbits, 0x2040ULL);

			lo |= rbits << 7;
			lo |= gbits << 21;
			lo |= bbits << 35;
			lo |= abits << 49;
		}
		else
		{
			uint64 deint = Compact8To7_8x(endpoints);
			lo |= deint << 7;
		}

		lo |= endpoints & (1ULL << 63);
		uint64 hi = indicesBits;

		WriteBlock(output, indices[i], lo, hi);
	}
}

static void UnMungeMode7(byte* output, const byte* payload, int firstOffset, int secondOffset,
	int stride0, int stride1, const TArray<int>& indices, bool switchColorspace)
{
	for (int i = 0; i < indices.Num(); i++)
	{
		uint64 prgbs = Get64(payload, firstOffset + i * stride0);
		uint64 rest = (uint64)Get32(payload, firstOffset + i * stride0 + 8);
		rest |= (uint64)Get32(payload, secondOffset + i * stride1) << 32;

		uint64 lo, hi;

		if (switchColorspace)
		{
			uint64 rbits = Compact16To5_4x(prgbs >> 1);
			uint64 gbits = Compact16To5_4x(prgbs >> 6);
			uint64 bbits = Compact16To5_4x(prgbs >> 11);
			uint64 pbits = Compact16To1_4x(prgbs);
			uint64 abits = BitExtract(rest, 8, 20);

			DecorrToRgbPacked(rbits, gbits, bbits, 0x84210ULL);

			lo = 0x80ULL;
			lo |= BitExtract(rest, 28, 6) << 8;
			lo |= rbits << 14;
			lo |= gbits << 34;
			lo |= bbits << 54;
			hi = bbits >> 10;
			hi |= abits << 10;
			hi |= pbits << 30;
		}
		else
		{
			uint64 pbits = BitExtract(rest, 8, 4);

			uint64 rgbbits = Expand4To5_12x(prgbs) << 1;
			rgbbits |= Expand1To5_12x(rest >> 12);
			uint64 abits = Expand4To5_4x(prgbs >> 48) << 1;
			abits |= Expand1To5_4x(rest >> 24);

			lo = 0x80ULL;
			lo |= BitExtract(rest, 28, 6) << 8;
			lo |= rgbbits << 14;
			hi = rgbbits >> 50;
			hi |= abits << 10;
			hi |= pbits << 30;
		}

		hi |= rest & ~0x3FFFFffffULL;

		WriteBlock(output, indices[i], lo, hi);
	}
}

static void UnMungeMode8(byte* output, const byte* payload, int firstOffset, const TArray<int>& indices)
{
	for (int i = 0; i < indices.Num(); i++)
		memcpy(output + indices[i] * 16, payload + firstOffset + i * 16, 16);
}

static void UnMungeMode9(byte* output, const byte* payload, int firstOffset, const TArray<int>& indices, bool switchColorspace)
{
	int t = 0;
	int coded = firstOffset;
	int count = indices.Num();

	while (t < count)
	{
		byte r = payload[coded];
		byte g = payload[coded + 1];
		byte b = payload[coded + 2];

		if (switchColorspace)
		{
			int ri = r, gi = g, bi = b;
			DecorrToRgbScalar(ri, gi, bi, 255);
			r = (byte)ri; g = (byte)gi; b = (byte)bi;
		}

		const uint64 bit6Mask = (0x40ULL << 8) | (0x40ULL << 22) | (0x40ULL << 36);
		const uint64 lo7Mask = (0x7fULL << 8) | (0x7fULL << 22) | (0x7fULL << 36);

		uint64 colorBits = ((uint64)r << 8) | ((uint64)g << 22) | ((uint64)b << 36);

		uint64 tBits = colorBits << 6;
		colorBits = ((colorBits >> 1) & lo7Mask) - (colorBits & ~lo7Mask);
		colorBits += tBits + (tBits & bit6Mask);

		colorBits |= (uint64)payload[coded + 3] << 50;
		colorBits |= 0x20ULL;

		uint32 codedBlock32 = Get32(payload, coded);

		do
		{
			WriteBlock(output, indices[t], colorBits, 0xaaaaaaacULL);
			t++;
			coded += 4;
			if (t >= count)
				break;
		} while (Get32(payload, coded) == codedBlock32);
	}
}

bool DecodeBC7Prep(const byte* payload, int payloadSize, const int32 modes[10], uint32 oodleFlags, byte* output, int outputSize)
{
	guard(DecodeBC7Prep);

	int numBlocks = 0;
	int expectedPayloadSize = 0;
	for (int i = 0; i < MODE_COUNT; i++)
	{
		numBlocks += modes[i];
		expectedPayloadSize += modes[i] * ModeSizes[i];
	}
	expectedPayloadSize += (numBlocks + 1) / 2;

	if (numBlocks == 0) return true;
	if (outputSize < numBlocks * 16) return false;
	if (payloadSize < expectedPayloadSize) return false;

	int modePos[MODE_COUNT + 1];
	modePos[0] = 0;
	for (int i = 0; i < MODE_COUNT; i++)
		modePos[i + 1] = modePos[i] + modes[i] * ModeSizes[i];

	int modeNibbleOffset = modePos[MODE_COUNT];

	TArray<int> modeIndices[MODE_COUNT];
	for (int i = 0; i < MODE_COUNT; i++)
		modeIndices[i].Empty(modes[i]);

	for (int i = 0; i < numBlocks; i++)
	{
		byte nibble = (i % 2 == 0)
			? (payload[modeNibbleOffset + i / 2] & 0xF)
			: ((payload[modeNibbleOffset + i / 2] >> 4) & 0xF);
		if (nibble < MODE_COUNT)
			modeIndices[nibble].Add(i);
	}

	bool switchColorspace = (oodleFlags & FLAG_SWITCH_COLORSPACE) != 0;

	for (int mode = 0; mode < MODE_COUNT; mode++)
	{
		const TArray<int>& indices = modeIndices[mode];
		if (indices.Num() == 0) continue;
		if (indices.Num() != modes[mode]) return false;

		int splitPoint = SplitPoint[mode];
		int modeSize = ModeSizes[mode];
		bool isSplit = (oodleFlags & (FLAG_SPLIT0 << mode)) != 0;

		int firstOffset = modePos[mode];
		int stride0, stride1, secondOffset;
		if (isSplit)
		{
			stride0 = splitPoint;
			stride1 = modeSize - splitPoint;
			secondOffset = firstOffset + splitPoint * modes[mode];
		}
		else
		{
			stride0 = modeSize;
			stride1 = modeSize;
			secondOffset = firstOffset + splitPoint;
		}

		switch (mode)
		{
		case 0: UnMungeMode0(output, payload, firstOffset, secondOffset, stride0, stride1, indices, switchColorspace); break;
		case 1: UnMungeMode1(output, payload, firstOffset, secondOffset, stride0, stride1, indices, switchColorspace); break;
		case 2: UnMungeMode2(output, payload, firstOffset, secondOffset, stride0, stride1, indices, switchColorspace); break;
		case 3: UnMungeMode3(output, payload, firstOffset, secondOffset, stride0, stride1, indices, switchColorspace); break;
		case 4: UnMungeMode4(output, payload, firstOffset, secondOffset, stride0, stride1, indices, switchColorspace); break;
		case 5: UnMungeMode5(output, payload, firstOffset, secondOffset, stride0, stride1, indices, switchColorspace); break;
		case 6: UnMungeMode6(output, payload, firstOffset, secondOffset, stride0, stride1, indices, switchColorspace); break;
		case 7: UnMungeMode7(output, payload, firstOffset, secondOffset, stride0, stride1, indices, switchColorspace); break;
		case 8: UnMungeMode8(output, payload, firstOffset, indices); break;
		case 9: UnMungeMode9(output, payload, firstOffset, indices, switchColorspace); break;
		}
	}

	return true;

	unguard;
}

#endif // WUTHERING_WAVES
