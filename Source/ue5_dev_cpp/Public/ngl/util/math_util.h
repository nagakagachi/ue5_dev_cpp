
#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <random>
#include <vector>
#include <iterator>

#include "Math/Vector.h"



namespace ngl
{
	// constexpr系.
	namespace cexpr
	{
		template<typename INTEGER_TYPE = uint32_t>
		struct TExpr
		{
			template <INTEGER_TYPE V0, INTEGER_TYPE V1>
			struct TAdd
			{
				static constexpr INTEGER_TYPE v = V0 + V1;
			};
			template <INTEGER_TYPE V0, INTEGER_TYPE V1>
			struct TAnd
			{
				static constexpr INTEGER_TYPE v = V0 & V1;
			};
			template <INTEGER_TYPE V0, INTEGER_TYPE V1>
			struct TOr
			{
				static constexpr INTEGER_TYPE v = V0 | V1;
			};
			template <INTEGER_TYPE V0, INTEGER_TYPE V1>
			struct TRightShift
			{
				static constexpr INTEGER_TYPE v = V0 >> V1;
			};
			template <INTEGER_TYPE V0, INTEGER_TYPE V1>
			struct TLeftShift
			{
				static constexpr INTEGER_TYPE v = V0 << V1;
			};


			// true/false select
			//	auto a = TSelect2<0, 1, true>::v;
			template <INTEGER_TYPE V_true, INTEGER_TYPE V_false, bool b>
			struct TSelect2
			{
				static constexpr INTEGER_TYPE v = V_true;
			};
			// true/false select
			template <INTEGER_TYPE V_true, INTEGER_TYPE V_false>
			struct TSelect2<V_true, V_false, false>
			{
				static constexpr INTEGER_TYPE v = V_false;
			};
			// Max
			template <INTEGER_TYPE V0, INTEGER_TYPE V1>
			struct TMax
			{
				static constexpr INTEGER_TYPE v = TSelect2<V0, V1, (V0 > V1)>::v;
			};
			// Min
			template <INTEGER_TYPE V0, INTEGER_TYPE V1>
			struct TMin
			{
				static constexpr INTEGER_TYPE v = TSelect2<V0, V1, (V0 < V1)>::v;
			};

			// Bit Count
			template <INTEGER_TYPE V>
			struct TCountBits
			{
				static constexpr INTEGER_TYPE v = TAdd<TAnd<V, 1>::v, TCountBits<TRightShift<V, 1>::v>::v >::v;
			};
			// Bit Count
			template <>
			struct TCountBits<1>
			{
				static constexpr INTEGER_TYPE v = 1;
			};
			// Bit Count
			template <>
			struct TCountBits<0>
			{
				static constexpr INTEGER_TYPE v = 0;
			};

			struct TSizeInBits
			{
				static constexpr uint32_t v = TCountBits<INTEGER_TYPE(~0)>::v;
			};

			// MSB Helper
			template <INTEGER_TYPE V, int BIT_POS>
			struct TMsbHelp
			{
				static constexpr INTEGER_TYPE v = TMax<TSelect2<BIT_POS, 0, 0 != TAnd<V, 1>::v>::v, TMsbHelp<(V >> 1), BIT_POS + 1>::v>::v;
			};
			template <int BIT_POS>
			struct TMsbHelp<INTEGER_TYPE(0), BIT_POS>
			{
				static constexpr INTEGER_TYPE v = 0;
			};
			// MSB
			template <INTEGER_TYPE V>
			struct TMsb
			{
				static constexpr INTEGER_TYPE v = TMsbHelp<V, 0>::v;
			};

			// LSB Helper
			template <INTEGER_TYPE V, int BIT_POS>
			struct TLsbHelp
			{
				static constexpr INTEGER_TYPE v = TMin<TSelect2<BIT_POS, TSizeInBits::v, 0 != TAnd<V, 1>::v>::v, TLsbHelp<(V >> 1), BIT_POS + 1>::v>::v;
			};
			template <int BIT_POS>
			struct TLsbHelp<INTEGER_TYPE(0), BIT_POS>
			{
				static constexpr INTEGER_TYPE v = TSizeInBits::v;
			};
			// LSB
			template <INTEGER_TYPE V>
			struct TLsb
			{
				static constexpr INTEGER_TYPE v = TLsbHelp<V, 0>::v;
			};


			// V以上の最小の二の冪に切り上げ.
			template <INTEGER_TYPE V>
			struct TRoundupToPowerOf2
			{
				static constexpr INTEGER_TYPE v = TSelect2< 1 << (TMsb<V - 1>::v + 1), 1, (V >= 2) >::v;
			};
		};

		template<std::size_t ... vs>
		struct TIndexSequence {};

		template<typename T1, typename T2>
		struct TIndexSequenceConcatenator;

		template<std::size_t ...v1, std::size_t ...v2>
		struct TIndexSequenceConcatenator<
			TIndexSequence<v1...>, TIndexSequence<v2...>>
		{
			typedef TIndexSequence<v1..., v2...> type;
		};

		template<std::size_t N>
		struct TIndexSequenceGenerator
		{
			typedef typename TIndexSequenceConcatenator< typename TIndexSequenceGenerator<N - 1>::type, TIndexSequence<N> >::type type;
		};

		template<>
		struct TIndexSequenceGenerator<0>
		{
			typedef TIndexSequence<0> type;
		};

		template<std::size_t N>
		using TMakeIndexSequence = typename TIndexSequenceGenerator<N - 1>::type;

		/*
			// コンパイル時に配列を生成する.
			// インデックス値から値を返すconstexpr関数を指定することで配列要素をカスタマイズできる.

			// 例) テーブルインデックスからその要素の値を計算するconstexpr関数を定義.
			constexpr size_t GenerateArrayElementPowerOf2(size_t v)
			{
				// test function : power of 2.
				return v*v;
			};

			// 例) 要素数5で各要素がインデックスの二乗の値であるような配列をコンパイル時に生成する.
			// {0, 1, 4, 8, 16}
			constexpr auto test_array = cexpr::TGenerateArray<uint32_t>( cexpr::TMakeIndexSequence<5>{}, GenerateArrayElementPowerOf2);

		*/
		template<typename Real, size_t... vals, typename SequenceFunctor>
		constexpr std::array<Real, sizeof...(vals)>
			TGenerateArray(TIndexSequence<vals...>, SequenceFunctor func)
		{
			return { {static_cast<Real>(func(vals))...} };
		}
		template<typename Real, size_t... vals>
		constexpr std::array<Real, sizeof...(vals)>
			TGenerateArray(TIndexSequence<vals...>)
		{
			return { {static_cast<Real>(vals)...} };
		}
	}


	// 通常のMath系.
	namespace math
	{
		// ビット数え上げ
		// 32bit version.
		constexpr int BitCount(uint32_t v)
		{
			uint32_t count = (v & 0x55555555) + ((v >> 1) & 0x55555555);
			count = (count & 0x33333333) + ((count >> 2) & 0x33333333);
			count = (count & 0x0f0f0f0f) + ((count >> 4) & 0x0f0f0f0f);
			count = (count & 0x00ff00ff) + ((count >> 8) & 0x00ff00ff);
			return (count & 0x0000ffff) + ((count >> 16) & 0x0000ffff);
		}
		// 最大ビット位置
		// 32bit version.
		constexpr int Msb(uint32_t v, bool* is_valid = nullptr)
		{
			auto valid = (v != 0);
			if (is_valid) *is_valid = valid;
			if (!valid)
				return 32; // ゼロの場合は32を返す(32bitの場合)

			v |= (v >> 1);
			v |= (v >> 2);
			v |= (v >> 4);
			v |= (v >> 8);
			v |= (v >> 16);
			return BitCount(v) - 1;
		}
		// 最小ビット位置
		// 32bit version.
		constexpr int Lsb(uint32_t v, bool* is_valid = nullptr)
		{
			auto valid = (v != 0);
			if (is_valid) *is_valid = valid;
			if (!valid)
				return 32; // ゼロの場合は32を返す(32bitの場合)

			v |= (v << 1);
			v |= (v << 2);
			v |= (v << 4);
			v |= (v << 8);
			v |= (v << 16);
			return 32 - BitCount(v);
		}

		// ビット数え上げ
		// 64bit version.
		constexpr int BitCount(uint64_t x)
		{
			constexpr uint64_t m1 = 0x5555555555555555;	//binary: 0101...
			constexpr uint64_t m2 = 0x3333333333333333; //binary: 00110011..
			constexpr uint64_t m4 = 0x0f0f0f0f0f0f0f0f; //binary:  4 zeros,  4 ones ...
			constexpr uint64_t m8 = 0x00ff00ff00ff00ff; //binary:  8 zeros,  8 ones ...
			constexpr uint64_t m16 = 0x0000ffff0000ffff; //binary: 16 zeros, 16 ones ...
			constexpr uint64_t m32 = 0x00000000ffffffff; //binary: 32 zeros, 32 ones
			constexpr uint64_t h01 = 0x0101010101010101; //the sum of 256 to the power of 0,1,2,3..

			x -= (x >> 1) & m1;				//put count of each 2 bits into those 2 bits
			x = (x & m2) + ((x >> 2) & m2);	//put count of each 4 bits into those 4 bits 
			x = (x + (x >> 4)) & m4;		//put count of each 8 bits into those 8 bits 
			return (x * h01) >> 56;			//returns left 8 bits of x + (x<<8) + (x<<16) + (x<<24) + ... 
		}
		// 最大ビット位置
		// 64bit version.
		constexpr int Msb(uint64_t v, bool* is_valid = nullptr)
		{
			auto valid = (v != 0);
			if (is_valid) *is_valid = valid;
			if (!valid)
				return 64; // ゼロの場合は64を返す(64bitの場合)

			v |= (v >> 1);
			v |= (v >> 2);
			v |= (v >> 4);
			v |= (v >> 8);
			v |= (v >> 16);
			v |= (v >> 32);
			return BitCount(v) - 1;
		}
		// 最小ビット位置
		// 64bit version.
		constexpr int Lsb(uint64_t v, bool* is_valid = nullptr)
		{
			auto valid = (v != 0);
			if (is_valid) *is_valid = valid;
			if (!valid)
				return 64; // ゼロの場合は64を返す(64bitの場合)

			v |= (v << 1);
			v |= (v << 2);
			v |= (v << 4);
			v |= (v << 8);
			v |= (v << 16);
			v |= (v << 32);
			return 64 - BitCount(v);
		}



		//	与えられたビット列を2bit飛ばしに変換
		//	0111 -> 0001001001
		static constexpr uint32_t BitSeparate2(uint32_t v)
		{
#if 1
			// https://devblogs.nvidia.com/thinking-parallel-part-iii-tree-construction-gpu/
			v = (v * 0x00010001u) & 0xFF0000FFu;
			v = (v * 0x00000101u) & 0x0F00F00Fu;
			v = (v * 0x00000011u) & 0xC30C30C3u;
			v = (v * 0x00000005u) & 0x49249249u;
			return v;
#else
			v = (v | v << 16) & 0xff0000ff;	// 11111111000000000000000011111111
			v = (v | v << 8) & 0x0f00f00f;	// 00001111000000001111000000001111
			v = (v | v << 4) & 0xc30c30c3;	// 11000011000011000011000011000011
			v = (v | v << 2) & 0x49249249;
			return v;
#endif
		}
		//	与えられたビット列を2bitずつ詰めて返す
		//	0111 -> 0001001001
		static constexpr uint32_t BitCompact2(uint32_t v)
		{
			v = v &					0x49249249u;
			v = (v ^ (v >> 2)) &	0xC30C30C3u;
			v = (v ^ (v >> 4)) &	0x0F00F00Fu;
			v = (v ^ (v >> 8)) &	0xFF0000FFu;
			v = (v ^ (v >> 16)) &	0x0000FFFFu;

			return v;
		}

		//	与えられたビット列を2bit飛ばしに変換(64bit)
		//	0111 -> 0001001001
		static constexpr uint64_t BitSeparate2_u64(uint64_t v) 
		{
			v = (v | v << 32ull) & 0b1111111111111111000000000000000000000000000000001111111111111111ull;
			v = (v | v << 16ull) &	0b0000000011111111000000000000000011111111000000000000000011111111ull;
			v = (v | v << 8ull) &	0b1111000000001111000000001111000000001111000000001111000000001111ull;
			v = (v | v << 4ull) &	0b0011000011000011000011000011000011000011000011000011000011000011ull;
			v = (v | v << 2ull) &	0b1001001001001001001001001001001001001001001001001001001001001001ull;
			return v;
		}
		static_assert(BitSeparate2_u64(0b1000000000000000000000ull) == 0b1000000000000000000000000000000000000000000000000000000000000000ull);
		static_assert(BitSeparate2_u64(0b1000000000000000000011ull) == 0b1000000000000000000000000000000000000000000000000000000000001001ull);
		static_assert(BitSeparate2_u64(0b1011000000000000000011ull) == 0b1000001001000000000000000000000000000000000000000000000000001001ull);

		//	与えられたビット列を2bitずつ詰めて返す(64bit)
		//	0111 -> 0001001001
		static constexpr uint64_t BitCompact2_u64(uint64_t v)
		{
			v = v &						0x9249249249249249ull;// 001001001 でマスク.
			v = (v ^ (v >> 2ull)) &		0x30C30C30C30C30C3ull;
			v = (v ^ (v >> 4ull)) &		0xF00F00F00F00F00Full;
			v = (v ^ (v >> 8ull)) &		0x00FF0000FF0000FFull;
			v = (v ^ (v >> 16ull)) &	0xFFFF00000000FFFFull;
			v = (v ^ (v >> 32ull)) &	0x00000000FFFFFFFFull;
			return v;
		}
		static_assert(0b1110000100000000000100ull == BitCompact2_u64(BitSeparate2_u64(0b1110000100000000000100ull)));
		static_assert(0b0110000100000000000111ull == BitCompact2_u64(BitSeparate2_u64(0b0110000100000000000111ull)));
		static_assert(0x2fffffull == BitCompact2_u64(BitSeparate2_u64(0x2fffffull)));


		// in : [0 , 1023]
		// 3Dセル座標から符号付き32bitモートンコード計算. 入力は各軸10bitまで.
		// 入力の範囲は0から1023 (10bit).
		// 使用bitwidthは 30bit=10*3 .
		// Calculates a 32-bit Morton code from [0 : 1023] range.
		// https://devblogs.nvidia.com/thinking-parallel-part-iii-tree-construction-gpu/
		static constexpr uint32_t EncodeMortonCodeX10Y10Z10(int x, int y, int z)
		{
			constexpr int range = (0x01 << 10) - 1;
			x = FMath::Min(FMath::Max(x, 0), range);//クランプ
			y = FMath::Min(FMath::Max(y, 0), range);//クランプ
			z = FMath::Min(FMath::Max(z, 0), range);//クランプ
			uint32_t xx = BitSeparate2((uint32_t)x);
			uint32_t yy = BitSeparate2((uint32_t)y);
			uint32_t zz = BitSeparate2((uint32_t)z);
			return xx | (yy << 1) | (zz << 2);
		}
		// 32bitモートンコードからセル座標復元
		static constexpr void DecodeMortonCodeX10Y10Z10(uint32_t morton, int& x, int& y, int& z)
		{
			constexpr int range = (0x01 << 10) - 1;
			x = BitCompact2(morton) & range;
			y = BitCompact2(morton >> 1) & range;
			z = BitCompact2(morton >> 2) & range;
		}

		// 3Dセル座標から符号付き64bitモートンコード計算. 入力は各軸21bitまで.
		// 使用bitwidthは 63bit=21*3 .
		static constexpr uint64_t EncodeMortonCodeX21Y21Z21(int x, int y, int z)
		{
			constexpr int range = (0x01 << 21) - 1;
			x = FMath::Min(FMath::Max(x, 0), range);//クランプ
			y = FMath::Min(FMath::Max(y, 0), range);//クランプ
			z = FMath::Min(FMath::Max(z, 0), range);//クランプ
			uint64_t xx = BitSeparate2_u64((uint64_t)x);
			uint64_t yy = BitSeparate2_u64((uint64_t)y);
			uint64_t zz = BitSeparate2_u64((uint64_t)z);
			return xx | (yy << 1) | (zz << 2);
		}
		// モートンコードからセル座標復元
		static constexpr void DecodeMortonCodeX21Y21Z21(uint64_t morton, int& x, int& y, int& z)
		{
			constexpr int range = (0x01 << 21) - 1;
			x = BitCompact2_u64(morton) & range;
			y = BitCompact2_u64(morton >> 1) & range;
			z = BitCompact2_u64(morton >> 2) & range;
		}


		// IntVector3をuint32にエンコード.
		static uint32_t EncodeIntVector3ToU11U11U10(const FIntVector& v)
		{
			constexpr uint32_t bitsizex = 11;
			constexpr uint32_t bitsizey = 11;
			constexpr uint32_t bitsizez = 10;
			constexpr uint32_t bitmaskx = (1 << bitsizex) - 1;
			constexpr uint32_t bitmasky = (1 << bitsizey) - 1;
			constexpr uint32_t bitmaskz = (1 << bitsizez) - 1;
			return (uint32_t(v.X) & bitmaskx) | ((uint32_t(v.Y) & bitmasky) << bitsizex) | ((uint32_t(v.Z) & bitmaskz) << (bitsizex + bitsizey));
		}
		// uint32からIntVector3をデコード
		static FIntVector DecodeU11U11U10ToIntVector3(const uint32_t& v)
		{
			constexpr uint32_t bitsizex = 11;
			constexpr uint32_t bitsizey = 11;
			constexpr uint32_t bitsizez = 10;
			constexpr uint32_t bitmaskx = (1 << bitsizex) - 1;
			constexpr uint32_t bitmasky = (1 << bitsizey) - 1;
			constexpr uint32_t bitmaskz = (1 << bitsizez) - 1;
			return FIntVector(v & bitmaskx, (v >> bitsizex) & bitmasky, (v >> (bitsizex + bitsizey)) & bitmaskz);
		}
		// IntVector3をuint32にエンコード.
		static uint32_t EncodeIntVector3ToU10U10U10(const FIntVector& v)
		{
			constexpr uint32_t bitsizex = 10;
			constexpr uint32_t bitsizey = 10;
			constexpr uint32_t bitsizez = 10;
			constexpr uint32_t bitmaskx = (1 << bitsizex) - 1;
			constexpr uint32_t bitmasky = (1 << bitsizey) - 1;
			constexpr uint32_t bitmaskz = (1 << bitsizez) - 1;
			return (uint32_t(v.X) & bitmaskx) | ((uint32_t(v.Y) & bitmasky) << bitsizex) | ((uint32_t(v.Z) & bitmaskz) << (bitsizex + bitsizey));
		}
		// uint32からIntVector3をデコード
		static FIntVector DecodeU10U10U10ToIntVector3(const uint32_t& v)
		{
			constexpr uint32_t bitsizex = 10;
			constexpr uint32_t bitsizey = 10;
			constexpr uint32_t bitsizez = 10;
			constexpr uint32_t bitmaskx = (1 << bitsizex) - 1;
			constexpr uint32_t bitmasky = (1 << bitsizey) - 1;
			constexpr uint32_t bitmaskz = (1 << bitsizez) - 1;
			return FIntVector(v & bitmaskx, (v >> bitsizex) & bitmasky, (v >> (bitsizex + bitsizey)) & bitmaskz);
		}

		// IntVector3をuint64にエンコード.
		static uint64_t EncodeIntVector3ToU64(const FIntVector& v)
		{
			constexpr uint32_t mask = (1 << 16) - 1;
			return uint64_t(v.X) | (uint64_t(v.Y & mask) << 16) | (uint64_t(v.Z & mask) << 32);
		}
		// uint64からIntVector3をデコード
		static FIntVector DecodeU64ToIntVector3(const uint64_t& v)
		{
			constexpr uint32_t mask = (1 << 16) - 1;
			return FIntVector(v & mask, (v >> 16) & mask, (v >> 32) & mask);
		}
		// IntVecto4の各要素を 15bit, 15bit, 15bit, 19bitに割り当てて64bitにエンコードする.
		static uint64_t EncodeInt4ToU15U15U15U19(const FIntVector4& v)
		{
			constexpr uint64_t bitwidth_xyz = 15;
			constexpr uint64_t bitwidth_w = 64 - 15 * 3;
			constexpr uint64_t mask_xyz = (1 << bitwidth_xyz) - 1;
			constexpr uint64_t mask_w = (1 << bitwidth_w) - 1;

			// 最下位にWを格納.
			return (uint64_t(v.W & mask_w))
				| (uint64_t((v.Z & mask_xyz) << (bitwidth_w)))
				| (uint64_t((v.Y & mask_xyz) << (bitwidth_w + bitwidth_xyz)))
				| (uint64_t((v.X & mask_xyz) << (bitwidth_w + bitwidth_xyz * 2)))
				;
		}
		// 15bit, 15bit, 15bit, 19bitにエンコードされた64bitをIntVecto4にデコードする.
		static FIntVector4 DecodeU15U15U15U19ToInt4(const uint64_t& v)
		{
			constexpr uint64_t bitwidth_xyz = 15;
			constexpr uint64_t bitwidth_w = 64 - 15 * 3;
			constexpr uint64_t mask_xyz = (1 << bitwidth_xyz) - 1;
			constexpr uint64_t mask_w = (1 << bitwidth_w) - 1;

			return FIntVector4((v >> (bitwidth_w + bitwidth_xyz * 2)) & mask_xyz, (v >> (bitwidth_w + bitwidth_xyz)) & mask_xyz, (v >> (bitwidth_w)) & mask_xyz, (v)&mask_w);
		}



		// 二分探索.
		// 未発見の場合は負数.
		template<typename ArrayType, typename T>
		static int BinarySearch(const ArrayType& target_array, const int start_index, const int search_range, const T& key)
		{
			// 二分探索 ( 同一値が連続する場合はその先頭を返す実装 )	
			if (0 >= search_range)
				return -1;

			int max_count = start_index + search_range;
			int left = start_index - 1;
			int right = max_count;

			while (abs(right - left) > 1) {
				int mid = (right + left) / 2;

				// rightが常に真となるように範囲を狭めていく.
				if (key <= target_array[mid])
					right = mid;
				else
					left = mid;
			}
			// 最後に範囲と値をチェックして無効なら-1
			return (max_count > right && key == target_array[right]) ? right : -1;
		}



		static constexpr bool IsPowerOfTwo(uint32 v)
		{
			return 0 == (v & (v - 1));
		}

		static const int CalcCellIndex(const FIntVector& pos, const FIntVector cell_count)
		{
			auto x = FMath::Clamp(pos.X, 0, cell_count.X - 1);
			auto y = FMath::Clamp(pos.Y, 0, cell_count.Y - 1);
			auto z = FMath::Clamp(pos.Z, 0, cell_count.Z - 1);
			return x + y * cell_count.X + z * cell_count.X * cell_count.Y;
		}
		static const int CalcCellIndex(int pos_x, int pos_y, int pos_z, const FIntVector cell_count)
		{
			return CalcCellIndex(FIntVector(pos_x, pos_y, pos_z), cell_count);
		}

		
		// ---------------------------------------------------------
		// FVector関連.

		// FVector要素のFloor.
		static FVector FVectorFloor(const FVector& v)
		{
			return FVector(FMath::FloorToFloat(v.X), FMath::FloorToFloat(v.Y), FMath::FloorToFloat(v.Z));
		}
		// FVector要素のCeil.
		static FVector FVectorCeil(const FVector& v)
		{
			return FVector(FMath::CeilToFloat(v.X), FMath::CeilToFloat(v.Y), FMath::CeilToFloat(v.Z));
		}
		// FVector要素のTrunc.
		static FVector FVectorTrunc(const FVector& v)
		{
			return FVector(FMath::TruncToFloat(v.X), FMath::TruncToFloat(v.Y), FMath::TruncToFloat(v.Z));
		}
		// 最も近い整数へ丸め
		static FVector FVectorRound(const FVector& v)
		{
			return FVector(FMath::RoundToFloat(v.X), FMath::RoundToFloat(v.Y), FMath::RoundToFloat(v.Z));
		}
		// FVector要素のClamp
		static FVector FVectorClamp(const FVector& v, const FVector& min_v, const FVector& max_v)
		{
			return FVector(FMath::Clamp(v.X, min_v.X, max_v.X), FMath::Clamp(v.Y, min_v.Y, max_v.Y), FMath::Clamp(v.Z, min_v.Z, max_v.Z));
		}
		
		// v0 > v1
		static FIntVector FVectorCompareGreater(const FVector& v0, const FVector& v1)
		{
			const auto vs0 = (v0.X > v1.X) ? 1 : 0;
			const auto vs1 = (v0.Y > v1.Y) ? 1 : 0;
			const auto vs2 = (v0.Z > v1.Z) ? 1 : 0;
			return FIntVector(vs0, vs1, vs2);
		}
		// v0 >= v1
		static FIntVector FVectorCompareGreaterEqual(const FVector& v0, const FVector& v1)
		{
			const auto vs0 = (v0.X >= v1.X) ? 1 : 0;
			const auto vs1 = (v0.Y >= v1.Y) ? 1 : 0;
			const auto vs2 = (v0.Z >= v1.Z) ? 1 : 0;
			return FIntVector(vs0, vs1, vs2);
		}
		// v0 < v1
		static FIntVector FVectorCompareLess(const FVector& v0, const FVector& v1)
		{
			const auto vs0 = (v0.X < v1.X) ? 1 : 0;
			const auto vs1 = (v0.Y < v1.Y) ? 1 : 0;
			const auto vs2 = (v0.Z < v1.Z) ? 1 : 0;
			return FIntVector(vs0, vs1, vs2);
		}
		// v0 <= v1
		static FIntVector FVectorCompareLessEqual(const FVector& v0, const FVector& v1)
		{
			const auto vs0 = (v0.X <= v1.X) ? 1 : 0;
			const auto vs1 = (v0.Y <= v1.Y) ? 1 : 0;
			const auto vs2 = (v0.Z <= v1.Z) ? 1 : 0;
			return FIntVector(vs0, vs1, vs2);
		}

		// v0 > v1
		static FIntVector FVectorCompareGreater(const FIntVector& v0, const FIntVector& v1)
		{
			const auto vs0 = (v0.X > v1.X) ? 1 : 0;
			const auto vs1 = (v0.Y > v1.Y) ? 1 : 0;
			const auto vs2 = (v0.Z > v1.Z) ? 1 : 0;
			return FIntVector(vs0, vs1, vs2);
		}
		// v0 >= v1
		static FIntVector FVectorCompareGreaterEqual(const FIntVector& v0, const FIntVector& v1)
		{
			const auto vs0 = (v0.X >= v1.X) ? 1 : 0;
			const auto vs1 = (v0.Y >= v1.Y) ? 1 : 0;
			const auto vs2 = (v0.Z >= v1.Z) ? 1 : 0;
			return FIntVector(vs0, vs1, vs2);
		}
		// v0 < v1
		static FIntVector FVectorCompareLess(const FIntVector& v0, const FIntVector& v1)
		{
			const auto vs0 = (v0.X < v1.X) ? 1 : 0;
			const auto vs1 = (v0.Y < v1.Y) ? 1 : 0;
			const auto vs2 = (v0.Z < v1.Z) ? 1 : 0;
			return FIntVector(vs0, vs1, vs2);
		}
		// v0 <= v1
		static FIntVector FVectorCompareLessEqual(const FIntVector& v0, const FIntVector& v1)
		{
			const auto vs0 = (v0.X <= v1.X) ? 1 : 0;
			const auto vs1 = (v0.Y <= v1.Y) ? 1 : 0;
			const auto vs2 = (v0.Z <= v1.Z) ? 1 : 0;
			return FIntVector(vs0, vs1, vs2);
		}

		// FVector要素の要素を選択.
		static FVector FVectorSelect(const FVector& v_true, const FVector& v_false, const FIntVector& bool_vec)
		{
			const auto vs0 = (0 != bool_vec.X) ? v_true.X : v_false.X;
			const auto vs1 = (0 != bool_vec.Y) ? v_true.Y : v_false.Y;
			const auto vs2 = (0 != bool_vec.Z) ? v_true.Z : v_false.Z;
			return FVector(vs0, vs1, vs2);
		}

		// FIntVector要素の要素を選択.
		static FIntVector FIntVectorSelect(const FIntVector& v_true, const FIntVector& v_false, const FIntVector& bool_vec)
		{
			const auto vs0 = (0 != bool_vec.X) ? v_true.X : v_false.X;
			const auto vs1 = (0 != bool_vec.Y) ? v_true.Y : v_false.Y;
			const auto vs2 = (0 != bool_vec.Z) ? v_true.Z : v_false.Z;
			return FIntVector(vs0, vs1, vs2);
		}

		// FVector要素のToInt.
		static FIntVector FVectorFloorToInt(const FVector& v)
		{
			return FIntVector(FMath::FloorToInt(v.X), FMath::FloorToInt(v.Y), FMath::FloorToInt(v.Z));
		}
		// FVector要素のFloor + ToInt.
		static FIntVector FVectorCeilToInt(const FVector& v)
		{
			return FIntVector(FMath::CeilToInt(v.X), FMath::CeilToInt(v.Y), FMath::CeilToInt(v.Z));
		}
		// 0に向かって丸め
		static FIntVector FVectorTruncToInt(const FVector& v)
		{
			return FIntVector(FMath::TruncToInt(v.X), FMath::TruncToInt(v.Y), FMath::TruncToInt(v.Z));
		}
		// 最も近い整数へ丸め
		static FIntVector FVectorRoundToInt(const FVector& v)
		{
			return FIntVector(FMath::RoundToInt(v.X), FMath::RoundToInt(v.Y), FMath::RoundToInt(v.Z));
		}
		// FIntVector要素のClamp
		static FIntVector FIntVectorClamp(const FIntVector& v, const FIntVector& min_v, const FIntVector& max_v)
		{
			return FIntVector(FMath::Clamp(v.X, min_v.X, max_v.X), FMath::Clamp(v.Y, min_v.Y, max_v.Y), FMath::Clamp(v.Z, min_v.Z, max_v.Z));
		}


		// FIntVector要素のMax.
		static FIntVector FIntVectorMax(const FIntVector& v0, const FIntVector& v1)
		{
			return FIntVector(FMath::Max(v0.X, v1.X), FMath::Max(v0.Y, v1.Y), FMath::Max(v0.Z, v1.Z));
		}
		// FIntVector要素のMin.
		static FIntVector FIntVectorMin(const FIntVector& v0, const FIntVector& v1)
		{
			return FIntVector(FMath::Min(v0.X, v1.X), FMath::Min(v0.Y, v1.Y), FMath::Min(v0.Z, v1.Z));
		}
		// FIntVector要素のAbs.
		static FIntVector FIntVectorAbs(const FIntVector& v0)
		{
			return FIntVector(FMath::Abs(v0.X), FMath::Abs(v0.Y), FMath::Abs(v0.Z));
		}

		static bool IsInner(const FIntVector& v, const FIntVector& min_v, const FIntVector& max_v)
		{
			bool b_min = (v.X >= min_v.X) && (v.Y >= min_v.Y) && (v.Z >= min_v.Z);
			bool b_max = (v.X <= max_v.X) && (v.Y <= max_v.Y) && (v.Z <= max_v.Z);
			return b_min && b_max;
		}
		static bool IsInnerWithPositive(const FIntVector& v, const FIntVector& max_v)
		{
			bool b_min = (v.X >= 0) && (v.Y >= 0) && (v.Z >= 0);
			bool b_max = (v.X <= max_v.X) && (v.Y <= max_v.Y) && (v.Z <= max_v.Z);
			return b_min && b_max;
		}

		// セルの中心基準で最も近いセル座標を計算.
		static FIntVector CalcGridAabbCenterGridCell(const FVector& pos, float cell_size)
		{
			return FVectorFloorToInt((pos + cell_size * 0.5f) / cell_size);
		}


	}




	
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 生成時サイズ指定をする, 未使用要素インデックス高速検索BitArray構造.
	//		BitArrayを別の長大なデータ配列の未使用要素管理に利用する場合の高速化を想定. false要素を高速に検索する, true要素の検索は未サポート.
	//
	//		FindFirstEmptyIndex()	:	階層ビットマスクを利用して最初に見つかるfalse要素のインデックスを返す.
	//		Set()					:	指定したインデックス要素に指定のフラグを書き込み,階層ビットマスクを更新する.
	//
	template<typename ELEMENT_CONTAINER_TYPE = uint32_t>
	class DynamicHierarchicalBitArray
	{

	public:
		DynamicHierarchicalBitArray() {}
		~DynamicHierarchicalBitArray() {}


		// 要素数を指定して初期化.
		bool Initialize(unsigned int num);

		// 全要素を指定の真偽値でクリアする.
		void Fill(bool v);

		// 要素数を返す.
		constexpr uint32_t MaxSize() const { return constant_param_.max_size_; }

		// 指定インデックスのbitの値を真偽値で取得.
		constexpr bool Get(uint32_t i) const;

		// 指定インデックスに真偽値を設定.
		void Set(uint32_t i, bool v);

		// 最初に見つかる0bit要素のインデックスを返す. false要素が存在しない場合は k_max_size 以上の値を返す.
		// return value that greater than k_max_size when false-element is not found.
		uint32_t FindFirstZeroBit() const;

		// 別の階層ビット配列からコピー.
		void Copy(const DynamicHierarchicalBitArray& src);

		// 階層ビット情報を更新する
		// 最下層ビットを直接操作した後などに利用する.
		void RefreshHierarchy();

	private:
		// 二つのBitフラグContainerをマージする.
		static constexpr ELEMENT_CONTAINER_TYPE MergeBitContainer(ELEMENT_CONTAINER_TYPE v0, ELEMENT_CONTAINER_TYPE v1)
		{
			return (((v0 >> k_container_half_size_in_bits) & v0) & k_container_half_bit_mask) |
				((((v1 >> k_container_half_size_in_bits) & v1) & k_container_half_bit_mask) << k_container_half_size_in_bits);
		}

	private:
		// uint32_t -> 32.
		static constexpr auto k_container_size_in_bits = cexpr::TExpr<ELEMENT_CONTAINER_TYPE>::TSizeInBits::v;
		// uint32_t -> 16.
		static constexpr auto k_container_half_size_in_bits = k_container_size_in_bits >> 1;
		// uint32_t -> 65535.
		static constexpr ELEMENT_CONTAINER_TYPE k_container_half_bit_mask = (ELEMENT_CONTAINER_TYPE(1) << k_container_half_size_in_bits) - 1;
		// uint32_t -> 5.
		static constexpr auto k_container_size_in_bits_log2 = (cexpr::TExpr<>::TMsb<k_container_size_in_bits>::v);
		//
		static constexpr ELEMENT_CONTAINER_TYPE k_container_bit_mask = k_container_size_in_bits - 1;


		// 階層別要素オフセットを計算.
		static constexpr int CalcLevelContainerOffset(int level)
		{
			return 1 * ((1 << level) - 1) / (2 - 1);
		}
		// 階層別要素数を計算.
		static constexpr int CalcLevelContainerCount(int level)
		{
			return 1 << level;
		}

	private:
		// 全階層を線形配置した要素配列.
		std::vector<ELEMENT_CONTAINER_TYPE> data_;

		struct ConstantParam
		{
			// 外部から指定された管理サイズ.
			uint32_t max_size_ = 0;
			// リクエストから計算されたコンテナ数
			uint32_t request_container_count_ = 0;
			// 実際に確保される最下層のコンテナ数
			uint32_t leaf_container_count_ = 0;
			// 実際に確保される最下層の要素数
			uint32_t leaf_element_count_ = 0;
			// レベル数
			uint32_t level_count_ = 0;
			// 最下層レベルのインデックス
			uint32_t leaf_level_index_ = 0;
			// 全レベル合計のコンテナ数
			uint32_t total_container_count_ = 0;
		} constant_param_ = {};
	};


	template<typename ELEMENT_CONTAINER_TYPE>
	bool DynamicHierarchicalBitArray<ELEMENT_CONTAINER_TYPE>::Initialize(unsigned int num)
	{
		constant_param_.max_size_ = std::max(num, 2u);
		// リクエストから計算されたコンテナ数
		constant_param_.request_container_count_ = std::max(1u + (num - 1u) / k_container_size_in_bits, 1u);
		// 実際に確保される最下層のコンテナ数
		constant_param_.leaf_container_count_ = (1 < constant_param_.request_container_count_) ? (1 << (math::Msb(constant_param_.request_container_count_ - 1) + 1)) : 1;
		// 実際に確保される最下層の要素数
		constant_param_.leaf_element_count_ = constant_param_.leaf_container_count_ * k_container_size_in_bits;
		// レベル数
		constant_param_.level_count_ = (math::Msb(constant_param_.leaf_container_count_)) + 1;
		// 最下層レベルのインデックス
		constant_param_.leaf_level_index_ = constant_param_.level_count_ - 1;
		// 全レベル合計のコンテナ数
		constant_param_.total_container_count_ = 1 * ((1 << constant_param_.level_count_) - 1) / (2 - 1);

		// Alloc
		data_.resize(constant_param_.total_container_count_);

		return true;
	}
	template<typename ELEMENT_CONTAINER_TYPE>
	void DynamicHierarchicalBitArray<ELEMENT_CONTAINER_TYPE>::Fill(bool v)
	{
		const ELEMENT_CONTAINER_TYPE fill_v = (v) ? ~ELEMENT_CONTAINER_TYPE(0) : 0;
		memset(data_.data(), fill_v, constant_param_.total_container_count_ * sizeof(ELEMENT_CONTAINER_TYPE));
	}
	template<typename ELEMENT_CONTAINER_TYPE>
	constexpr bool DynamicHierarchicalBitArray<ELEMENT_CONTAINER_TYPE>::Get(uint32_t i) const
	{
		const auto target_container = i >> k_container_size_in_bits_log2;
		const auto leaf_bit = i & k_container_bit_mask;
		const ELEMENT_CONTAINER_TYPE leaf_write_mask = (ELEMENT_CONTAINER_TYPE(1) << leaf_bit);

		return (MaxSize() > i) && (data_[CalcLevelContainerOffset(constant_param_.leaf_level_index_) + (i >> k_container_size_in_bits_log2)] & (leaf_write_mask));
	}
	// 指定要素に真偽値を設定.
	template<typename ELEMENT_CONTAINER_TYPE>
	void DynamicHierarchicalBitArray<ELEMENT_CONTAINER_TYPE>::Set(uint32_t i, bool v)
	{
		const auto target_container = i >> k_container_size_in_bits_log2;
		const auto leaf_bit = i & k_container_bit_mask;

		const ELEMENT_CONTAINER_TYPE leaf_write_mask = (ELEMENT_CONTAINER_TYPE(1) << leaf_bit);

		// 最下層レベルを更新
		if (v)
			data_[CalcLevelContainerOffset(constant_param_.leaf_level_index_) + target_container] |= leaf_write_mask;
		else
			data_[CalcLevelContainerOffset(constant_param_.leaf_level_index_) + target_container] &= ~leaf_write_mask;

		// 上層レベルを更新
		for (unsigned int l = 1; l <= constant_param_.leaf_level_index_; ++l)
		{
			const auto dst_level = constant_param_.leaf_level_index_ - l;
			const auto dst_container = target_container >> l;
			const auto src_container_first = CalcLevelContainerOffset(dst_level + 1) + (dst_container << 1);
			const auto dst_container_first = CalcLevelContainerOffset(dst_level) + (dst_container);

			// 隣接する二つのcontainerについて論理積で情報をマージして上層にセットする. ビット的に隣接要素の論理積では無い点に注意.
			data_[dst_container_first] = MergeBitContainer(data_[src_container_first], data_[src_container_first + 1]);
		}
	}
	// 先頭から最初に見つかるfalse要素インデックスを検索. false要素が存在しない場合は k_max_size 以上の値を返す.
	// return value that greater than k_max_size when false-element is not found.
	template<typename ELEMENT_CONTAINER_TYPE>
	uint32_t DynamicHierarchicalBitArray<ELEMENT_CONTAINER_TYPE>::FindFirstZeroBit() const
	{
		constexpr ELEMENT_CONTAINER_TYPE not_found_query = ~ELEMENT_CONTAINER_TYPE(0);

		unsigned int local_index = 0;
		unsigned int level = 0;

		if (1 >= constant_param_.level_count_)
		{
			// レベル数が1の場合はコンパイル時定数分岐で特殊チェック.

			// not_found_queryに一致する場合はこれ以上検索しても存在しないので終了.
			if (not_found_query == data_[0])
				return constant_param_.max_size_;
		}
		else
		{
			// レベルが2以上の場合. 通常はこちら.

			for (; level < constant_param_.leaf_level_index_; ++level)
			{
				const auto container_index = CalcLevelContainerOffset(level) + local_index;
				// not_found_queryに一致する場合はこれ以上検索しても存在しないので終了.
				if (not_found_query == data_[container_index])
					break;

				// 今のレベルでの探索コンテナインデックスから次の階層での探索コンテナインデックスに変換(2倍)
				local_index = local_index << 1;
				// どこかにゼロのビットがある. 対応する二つの子コンテナのどちらを探索するか. ゼロがある方を探索.
				local_index += (k_container_half_bit_mask != (data_[container_index] & (k_container_half_bit_mask))) ? 0 : 1;
			}

			// Leafレベルまで到達する前にfalse要素が存在しないことが確定したらここで終了.
			if (level != constant_param_.leaf_level_index_)
				return constant_param_.max_size_;
		}

		// 反転したコンテナのLSBが所望の最初に見つかるゼロビット. 全ビットが1の状態ではここまで到達する前にリターンする.
		const auto lsb = math::Lsb(~data_[CalcLevelContainerOffset(constant_param_.leaf_level_index_) + local_index]);
		return (local_index << k_container_size_in_bits_log2) + lsb;
	}
	// 別の階層ビット配列からコピー.
	template<typename ELEMENT_CONTAINER_TYPE>
	void DynamicHierarchicalBitArray<ELEMENT_CONTAINER_TYPE>::Copy(const DynamicHierarchicalBitArray<ELEMENT_CONTAINER_TYPE>& src)
	{
		// 最下層ビット部をコピー
		auto* p_dst = &data_[CalcLevelContainerOffset(constant_param_.leaf_level_index_)];
		const auto* p_src = &src.data_[src.CalcLevelContainerOffset(src.constant_param_.leaf_level_index_)];

		const auto dst_count = CalcLevelContainerCount(constant_param_.leaf_level_index_);
		const auto src_count = src.CalcLevelContainerCount(src.constant_param_.leaf_level_index_);
		const auto copy_count = std::min(dst_count, src_count);

		// コピー
		memcpy(p_dst, p_src, copy_count * sizeof(ELEMENT_CONTAINER_TYPE));
		// コピー先のほうが大きい場合はその部分をゼロクリア
		if (copy_count < dst_count)
		{
			memset(p_dst + copy_count, ELEMENT_CONTAINER_TYPE(0), (dst_count - copy_count) * sizeof(ELEMENT_CONTAINER_TYPE));
		}

		// 階層ビットを更新
		RefreshHierarchy();
	}

	// 階層ビット全体を更新する
	// コピー等で最下層ビットを直接操作した後などに利用する.
	template<typename ELEMENT_CONTAINER_TYPE>
	void DynamicHierarchicalBitArray<ELEMENT_CONTAINER_TYPE>::RefreshHierarchy()
	{
		if (0 < constant_param_.leaf_level_index_)
		{
			// 上層レベルを更新
			for (unsigned int l = 1; l <= constant_param_.leaf_level_index_; ++l)
			{
				const unsigned int container_count = CalcLevelContainerCount(constant_param_.leaf_level_index_ - l);

				for (unsigned int dst_container = 0; dst_container < container_count; ++dst_container)
				{
					const auto dst_level = constant_param_.leaf_level_index_ - l;
					const auto src_container_first = CalcLevelContainerOffset(dst_level + 1) + (dst_container << 1);
					const auto dst_container_first = CalcLevelContainerOffset(dst_level) + (dst_container);

					// 隣接する二つのcontainerについて論理積で情報をマージして上層にセットする. ビット的に隣接要素の論理積では無い点に注意.
					data_[dst_container_first] = MergeBitContainer(data_[src_container_first], data_[src_container_first + 1]);
				}
			}
		}
	}
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 固定長サイズ, 未使用要素インデックス高速検索BitArray構造.
	//		BitArrayを別の長大なデータ配列の未使用要素管理に利用する場合の高速化を想定. false要素を高速に検索する, true要素の検索は未サポート.
	//
	//		FindFirstEmptyIndex()	:	階層ビットマスクを利用して最初に見つかるfalse要素のインデックスを返す.
	//		Set()					:	指定したインデックス要素に指定のフラグを書き込み,階層ビットマスクを更新する.
	//
	template<unsigned int MAX_BIT_COUNT, typename ELEMENT_CONTAINER_TYPE = uint32_t>
	class FixedHierarchicalBitArray
	{

	public:
		FixedHierarchicalBitArray() {}
		~FixedHierarchicalBitArray() {}

		// 全要素を指定の真偽値でクリアする.
		void Fill(bool v);

		// 要素数を返す.
		constexpr uint32_t MaxSize() const { return k_max_size; }

		// 指定インデックスのbitの値を真偽値で取得.
		constexpr bool Get(uint32_t i) const;

		// 指定インデックスに真偽値を設定.
		void Set(uint32_t i, bool v);

		// 最初に見つかる0bit要素のインデックスを返す. false要素が存在しない場合は k_max_size 以上の値を返す.
		// return value that greater than k_max_size when false-element is not found.
		uint32_t FindFirstZeroBit() const;

		// 階層ビット情報を更新する
		// 最下層ビットを直接操作した後などに利用する.
		void RefreshHierarchy();

	public:
		static constexpr uint32_t k_max_size = MAX_BIT_COUNT;

		// uint32_t -> 32.
		static constexpr auto k_container_size_in_bits = cexpr::TExpr<ELEMENT_CONTAINER_TYPE>::TSizeInBits::v;
		// uint32_t -> 16.
		static constexpr auto k_container_half_size_in_bits = k_container_size_in_bits >> 1;
		// uint32_t -> 65535.
		static constexpr ELEMENT_CONTAINER_TYPE k_container_half_bit_mask = (ELEMENT_CONTAINER_TYPE(1) << k_container_half_size_in_bits) - 1;
		// uint32_t -> 5.
		static constexpr auto k_container_size_in_bits_log2 = (cexpr::TExpr<>::TMsb<k_container_size_in_bits>::v);
		//
		static constexpr ELEMENT_CONTAINER_TYPE k_container_bit_mask = k_container_size_in_bits - 1;


		// リクエストから計算されたコンテナ数
		static constexpr auto k_request_container_count = cexpr::TExpr<>::TMax<1, 1 + (MAX_BIT_COUNT - 1) / k_container_size_in_bits>::v;
		// 実際に確保される最下層のコンテナ数
		static constexpr auto k_leaf_container_count = cexpr::TExpr<>::TRoundupToPowerOf2<k_request_container_count>::v;
		// 実際に確保される最下層の要素数
		static constexpr auto k_leaf_element_count = k_leaf_container_count * k_container_size_in_bits;
		// レベル数
		static constexpr auto k_level_count = (cexpr::TExpr<>::TMsb<k_leaf_container_count>::v) + 1;
		// 最下層レベルのインデックス
		static constexpr auto k_leaf_level_index = k_level_count - 1;
		// 全レベル合計のコンテナ数
		static constexpr auto k_total_container_count = 1 * ((1 << k_level_count) - 1) / (2 - 1);

	private:
		// 階層別要素オフセットを計算.
		static constexpr int CalcLevelContainerOffset(int level)
		{
			return 1 * ((1 << level) - 1) / (2 - 1);
		}
		// 階層別要素数を計算.
		static constexpr int CalcLevelContainerCount(int level)
		{
			return 1 << level;
		}
		// 階層別要素オフセットテーブル. コンパイル時生成.
		static constexpr auto k_level_container_offset_table = cexpr::TGenerateArray<int>(cexpr::TMakeIndexSequence<k_level_count>{}, CalcLevelContainerOffset);
		// 階層別要素数テーブル. コンパイル時生成.
		static constexpr auto k_level_container_count_table = cexpr::TGenerateArray<int>(cexpr::TMakeIndexSequence<k_level_count>{}, CalcLevelContainerCount);

		// 二つのBitフラグContainerをマージする.
		static constexpr ELEMENT_CONTAINER_TYPE MergeBitContainer(ELEMENT_CONTAINER_TYPE v0, ELEMENT_CONTAINER_TYPE v1)
		{
			return (((v0 >> k_container_half_size_in_bits) & v0) & k_container_half_bit_mask) |
				((((v1 >> k_container_half_size_in_bits) & v1) & k_container_half_bit_mask) << k_container_half_size_in_bits);
		}
	private:
		// 全階層を線形配置した要素配列.
		std::array<ELEMENT_CONTAINER_TYPE, k_total_container_count> data_;
	};

	template<unsigned int MAX_BIT_COUNT, typename ELEMENT_CONTAINER_TYPE>
	void FixedHierarchicalBitArray<MAX_BIT_COUNT, ELEMENT_CONTAINER_TYPE>::Fill(bool v)
	{
		data_.fill((v) ? ~ELEMENT_CONTAINER_TYPE(0) : 0);
	}
	template<unsigned int MAX_BIT_COUNT, typename ELEMENT_CONTAINER_TYPE>
	constexpr bool FixedHierarchicalBitArray<MAX_BIT_COUNT, ELEMENT_CONTAINER_TYPE>::Get(uint32_t i) const
	{
		const auto target_container = i >> k_container_size_in_bits_log2;
		const auto leaf_bit = i & k_container_bit_mask;
		const ELEMENT_CONTAINER_TYPE leaf_write_mask = (ELEMENT_CONTAINER_TYPE(1) << leaf_bit);

		return (data_[k_level_container_offset_table[k_leaf_level_index] + (i >> k_container_size_in_bits_log2)] & (leaf_write_mask));
	}
	// 指定要素に真偽値を設定.
	template<unsigned int MAX_BIT_COUNT, typename ELEMENT_CONTAINER_TYPE>
	void FixedHierarchicalBitArray<MAX_BIT_COUNT, ELEMENT_CONTAINER_TYPE>::Set(uint32_t i, bool v)
	{
		const auto target_container = i >> k_container_size_in_bits_log2;
		const auto leaf_bit = i & k_container_bit_mask;

		const ELEMENT_CONTAINER_TYPE leaf_write_mask = (ELEMENT_CONTAINER_TYPE(1) << leaf_bit);

		// 最下層レベルを更新
		if (v)
			data_[k_level_container_offset_table[k_leaf_level_index] + target_container] |= leaf_write_mask;
		else
			data_[k_level_container_offset_table[k_leaf_level_index] + target_container] &= ~leaf_write_mask;

		// 上層レベルを更新
		for (int l = 1; l <= k_leaf_level_index; ++l)
		{
			const auto dst_level = k_leaf_level_index - l;
			const auto dst_container = target_container >> l;
			const auto src_container_first = k_level_container_offset_table[dst_level + 1] + (dst_container << 1);
			const auto dst_container_first = k_level_container_offset_table[dst_level] + (dst_container);

			// 隣接する二つのcontainerについて論理積で情報をマージして上層にセットする. ビット的に隣接要素の論理積では無い点に注意.
			data_[dst_container_first] = MergeBitContainer(data_[src_container_first], data_[src_container_first + 1]);
		}
	}
	// 先頭から最初に見つかるfalse要素インデックスを検索. false要素が存在しない場合は k_max_size 以上の値を返す.
	// return value that greater than k_max_size when false-element is not found.
	template<unsigned int MAX_BIT_COUNT, typename ELEMENT_CONTAINER_TYPE>
	uint32_t FixedHierarchicalBitArray<MAX_BIT_COUNT, ELEMENT_CONTAINER_TYPE>::FindFirstZeroBit() const
	{
		constexpr ELEMENT_CONTAINER_TYPE not_found_query = ~ELEMENT_CONTAINER_TYPE(0);

		unsigned int local_index = 0;
		unsigned int level = 0;

		if (1 >= k_level_count)
		{
			// レベル数が1の場合はコンパイル時定数分岐で特殊チェック.

			// not_found_queryに一致する場合はこれ以上検索しても存在しないので終了.
			if (not_found_query == data_[0])
				return k_max_size;
		}
		else
		{
			// レベルが2以上の場合. 通常はこちら.

			for (; level < k_leaf_level_index; ++level)
			{
				const auto container_index = k_level_container_offset_table[level] + local_index;
				// not_found_queryに一致する場合はこれ以上検索しても存在しないので終了.
				if (not_found_query == data_[container_index])
					break;

				// 今のレベルでの探索コンテナインデックスから次の階層での探索コンテナインデックスに変換(2倍)
				local_index = local_index << 1;
				// どこかにゼロのビットがある. 対応する二つの子コンテナのどちらを探索するか. ゼロがある方を探索.
				local_index += (k_container_half_bit_mask != (data_[container_index] & (k_container_half_bit_mask))) ? 0 : 1;
			}

			// Leafレベルまで到達する前にfalse要素が存在しないことが確定したらここで終了.
			if (level != k_leaf_level_index)
				return k_max_size;
		}

		// 反転したコンテナのLSBが所望の最初に見つかるゼロビット. 全ビットが1の状態ではここまで到達する前にリターンする.
		const auto lsb = math::Lsb(~data_[k_level_container_offset_table[k_leaf_level_index] + local_index]);
		return (local_index << k_container_size_in_bits_log2) + lsb;
	}
	// 階層ビット全体を更新する
	// コピー等で最下層ビットを直接操作した後などに利用する.
	template<unsigned int MAX_BIT_COUNT, typename ELEMENT_CONTAINER_TYPE>
	void FixedHierarchicalBitArray<MAX_BIT_COUNT, ELEMENT_CONTAINER_TYPE>::RefreshHierarchy()
	{
		if (0 < k_leaf_level_index)
		{
			// 上層レベルを更新
			for (unsigned int l = 1; l <= k_leaf_level_index; ++l)
			{
				const unsigned int container_count = CalcLevelContainerCount(k_leaf_level_index - l);

				for (unsigned int dst_container = 0; dst_container < container_count; ++dst_container)
				{
					const auto dst_level = k_leaf_level_index - l;
					const auto src_container_first = CalcLevelContainerOffset(dst_level + 1) + (dst_container << 1);
					const auto dst_container_first = CalcLevelContainerOffset(dst_level) + (dst_container);

					// 隣接する二つのcontainerについて論理積で情報をマージして上層にセットする. ビット的に隣接要素の論理積では無い点に注意.
					data_[dst_container_first] = MergeBitContainer(data_[src_container_first], data_[src_container_first + 1]);
				}
			}
		}
	}
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//	指定した型オブジェクトのページ単位メモリ確保と高速な未使用要素検索を目的とするクラス.
	//		templateで指定した型をページ単位でアロケーションする
	//		必要に応じてページの拡張をする.
	//		未使用要素の検索を階層ビット配列で高速に処理する.
	template<typename COMPONENT_TYPE>
	class PagedComponentAllocator
	{
	public:
		using Handle = uint32_t;
		static constexpr Handle InvalidHandle = ~Handle(0);

		struct Desc
		{
			// アロケーションブロックのサイズ(component count). アロケーションはこのブロック単位のComponentで処理される.
			uint32_t block_size = 1;

			// ページ単位のブロック数のLog2. 実際には power(2, block_count_per_page_log2) がpage単位のBlock数.
			// 例) 5 -> 32
			// 例) 7 -> 128
			uint32_t block_count_per_page_log2 = 5;
		};

	public:
		PagedComponentAllocator()
		{
		}
		~PagedComponentAllocator()
		{
			Finalize();
		}
		// 初期化.
		bool Initialize(const Desc& desc)
		{
			check(0 < desc.block_size);
			check(0 < desc.block_count_per_page_log2);

			block_size_ = desc.block_size;

			block_count_per_page_log2_ = desc.block_count_per_page_log2;
			block_count_per_page_ = 1 << block_count_per_page_log2_;
			page_block_index_mask_ = (1 << block_count_per_page_log2_) - 1;


			used_bits_.Initialize(block_count_per_page_ * 1);
			used_bits_.Fill(false);

			return true;
		}
		// 解放.
		void Finalize()
		{
			for (auto&& e : pool_)
			{
				if (e)
				{
					delete e;
					e = nullptr;
				}
			}
			std::vector<Page*>().swap(pool_);
		}

		// 使用可能な要素を検索してハンドルを返す.
		Handle Alloc()
		{
			auto empty_index = used_bits_.FindFirstZeroBit();

			// 未使用ビットがが見つからない場合.
			if (used_bits_.MaxSize() <= empty_index)
			{
				// 階層Bit配列を必要なサイズまで拡張
				DynamicHierarchicalBitArray<> new_hb;
				// 2倍のサイズを確保
				new_hb.Initialize(used_bits_.MaxSize() * 2u);
				// コピー
				new_hb.Copy(used_bits_);
				// move
				used_bits_ = std::move(new_hb);

				// 再検索
				empty_index = used_bits_.FindFirstZeroBit();
				// 有効か一応チェック
				check(used_bits_.MaxSize() > empty_index);
			}

			const uint32_t page_id = empty_index >> block_count_per_page_log2_;
			const uint32_t page_component_id = empty_index & page_block_index_mask_;

			// ページ配列が必要分確保されていない場合.
			if (pool_.size() <= page_id)
			{
				// リサイズ
				std::vector<Page*> new_pool;
				new_pool.resize(std::max(static_cast<int>(pool_.size()), 1) * 2);

				// 元のデータをコピー
				memcpy(new_pool.data(), pool_.data(), sizeof(Page*) * pool_.size());

				// 拡張分をnullクリア
				memset(new_pool.data() + pool_.size(), 0, sizeof(Page*) * (new_pool.size() - pool_.size()));

				// move
				pool_ = std::move(new_pool);
			}

			// ページ自体が確保されていない場合.
			if (nullptr == pool_[page_id])
			{
				// ページを確保して初期化.
				pool_[page_id] = new Page();
				// ページはページあたりのブロック数*ブロックサイズ分
				pool_[page_id]->data.resize(block_count_per_page_ * block_size_);
			}

			// 使用状態にセット
			used_bits_.Set(empty_index, true);

			return Handle(empty_index);
		}

		// ハンドルが指す要素を解放して再利用可能にする.
		void Dealloc(Handle handle)
		{
			const uint32_t page_id = handle >> block_count_per_page_log2_;
			const uint32_t page_component_id = handle & page_block_index_mask_;

			// プールサイズチェック.
			check(pool_.size() > page_id);
			// プール要素が確保されているかチェック.
			check(nullptr != pool_[page_id]);
			// 使用ビットが真がチェック.
			check(used_bits_.Get(handle));

			// 未使用状態にセット.
			used_bits_.Set(handle, false);
		}

		// ハンドルが指す要素を取得する.
		const COMPONENT_TYPE* Get(Handle handle) const
		{
			const uint32_t page_id = handle >> block_count_per_page_log2_;
			const uint32_t page_component_id = handle & page_block_index_mask_;
			// プールサイズチェック.
			check(pool_.size() > page_id);
			// プール要素が確保されているかチェック.
			check(nullptr != pool_[page_id]);
			// 使用ビットが真がチェック.
			check(used_bits_.Get(handle));
			// 実データはブロック単位.
			return &(pool_[page_id]->data[page_component_id * block_size_]);
		}
		// ハンドルが指す要素を取得する.
		COMPONENT_TYPE* Get(Handle handle)
		{
			const uint32_t page_id = handle >> block_count_per_page_log2_;
			const uint32_t page_component_id = handle & page_block_index_mask_;
			// プールサイズチェック.
			check(pool_.size() > page_id);
			// プール要素が確保されているかチェック.
			check(nullptr != pool_[page_id]);
			// 使用ビットが真がチェック.
			check(used_bits_.Get(handle));
			// 実データはブロック単位.
			return &(pool_[page_id]->data[page_component_id * block_size_]);
		}

		// 使用状況をリセット. 確保メモリは維持.
		void Reset()
		{
			used_bits_.Fill(false);
		}


		// 直接アクセス用.
		// 最大インデックスを取得する
		uint32_t NumHandle() const
		{
			return static_cast<uint32_t>(pool_.size()) * block_count_per_page_;
		}
		// 直接アクセス用.
		// インデックスで直接有効フラグを取得する.
		bool IsValid(Handle index) const
		{
			const uint32_t page_id = index >> block_count_per_page_log2_;
			const uint32_t page_component_id = index & page_block_index_mask_;
			if ((pool_.size() <= page_id) || (nullptr == pool_[page_id]))
				return false;
			return used_bits_.Get(index);
		}
		// 直接アクセス用.
		// インデックスで直接要素を取得する.
		COMPONENT_TYPE* GetByIndex(uint32_t index)
		{
			const uint32_t page_id = index >> block_count_per_page_log2_;
			const uint32_t page_component_id = index & page_block_index_mask_;
			if (!used_bits_.Get(index))
				return nullptr;
			return &(pool_[page_id]->data[page_component_id * block_size_]);
		}
		// 直接アクセス用.
		// インデックスで直接要素を取得する.
		const COMPONENT_TYPE* GetByIndex(uint32_t index) const
		{
			const uint32_t page_id = index >> block_count_per_page_log2_;
			const uint32_t page_component_id = index & page_block_index_mask_;
			if (!used_bits_.Get(index))
				return nullptr;
			return &(pool_[page_id]->data[page_component_id * block_size_]);
		}

		// 直接アクセス用.
		// ページ数を取得する
		uint32_t NumPage() const
		{
			return static_cast<uint32_t>(pool_.size());
		}
		// 直接アクセス用.
		// インデックスで直接ページを取得する.
		COMPONENT_TYPE* GetPageByIndex(uint32_t page_index)
		{
			if (!pool_[page_index])
				return nullptr;
			return pool_[page_index]->data.data();
		}



		uint32_t GetBlockSize() const
		{
			return block_size_;
		}
		uint32_t GetPageBlockCount() const
		{
			return block_count_per_page_;
		}
		constexpr uint32_t GetComponentByteSize() const
		{
			return static_cast<uint32_t>(sizeof(COMPONENT_TYPE));
		}

		// 確保メモリサイズ取得用.
		size_t GetAllocatedMemorySize() const
		{
			size_t s = 0;
			for (auto&& e : pool_)
			{
				if (e)
					s += e->data.size() * sizeof(COMPONENT_TYPE);
			}
			return s;
		}
	private:
		struct Page
		{
			std::vector<COMPONENT_TYPE> data;
		};

		uint32_t block_size_ = 1;

		uint32_t block_count_per_page_log2_ = 0;
		uint32_t block_count_per_page_ = 0;
		uint32_t page_block_index_mask_ = 0;

		// page要素の使用状態を表す階層ビット配列.
		DynamicHierarchicalBitArray<> used_bits_;
		// Pageプール.
		std::vector<Page*>	pool_;
	};



	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//	指定したバイトサイズでページ単位メモリ確保と高速な未使用要素検索を目的とするクラス.
	//		PagedComponentAllocator のバイト列版.
	//		必要に応じてページの拡張をする.
	//		未使用要素の検索を階層ビット配列で高速に処理する.
	class PagedMemoryAllocator
	{
	public:
		using Handle = uint32_t;
		static constexpr Handle InvalidHandle = ~Handle(0);

		struct Desc
		{
			// アロケーションブロックのサイズ(byte). アロケーションはこのブロック単位で処理される.
			uint32_t block_byte_size = 16;

			// ページ単位のblock数のLog2. 実際には power(2, block_count_per_page_log2) がpage単位のBlock数.
			// 例) 5 -> 32
			// 例) 7 -> 128
			uint32_t block_count_per_page_log2 = 5;
		};

	public:
		PagedMemoryAllocator()
		{
		}
		~PagedMemoryAllocator()
		{
			Finalize();
		}
		// 初期化.
		bool Initialize(const Desc& desc)
		{
			check(0 < desc.block_byte_size);
			check(0 < desc.block_count_per_page_log2);

			block_byte_size_ = desc.block_byte_size;

			block_count_per_page_log2_ = desc.block_count_per_page_log2;
			block_count_per_page_ = 1 << block_count_per_page_log2_;
			page_block_index_mask_ = (1 << block_count_per_page_log2_) - 1;

			used_bits_.Initialize(block_count_per_page_ * 1);
			used_bits_.Fill(false);

			return true;
		}
		// 解放.
		void Finalize()
		{
			for (auto&& e : pool_)
			{
				if (e)
				{
					delete e;
					e = nullptr;
				}
			}
			std::vector<Page*>().swap(pool_);
		}

		// 使用可能な要素を検索してハンドルを返す.
		Handle Alloc()
		{
			auto empty_index = used_bits_.FindFirstZeroBit();

			// 未使用ビットがが見つからない場合.
			if (used_bits_.MaxSize() <= empty_index)
			{
				// 階層Bit配列を必要なサイズまで拡張
				DynamicHierarchicalBitArray<> new_hb;
				// 2倍のサイズを確保
				new_hb.Initialize(used_bits_.MaxSize() * 2u);
				// コピー
				new_hb.Copy(used_bits_);
				// move
				used_bits_ = std::move(new_hb);

				// 再検索
				empty_index = used_bits_.FindFirstZeroBit();
				// 有効か一応チェック
				check(used_bits_.MaxSize() > empty_index);
			}

			const uint32_t page_id = empty_index >> block_count_per_page_log2_;
			const uint32_t page_component_id = empty_index & page_block_index_mask_;

			// ページ配列が必要分確保されていない場合.
			if (pool_.size() <= page_id)
			{
				// リサイズ
				std::vector<Page*> new_pool;
				new_pool.resize(std::max(static_cast<int>(pool_.size()), 1) * 2);

				// 元のデータをコピー
				memcpy(new_pool.data(), pool_.data(), sizeof(Page*) * pool_.size());

				// 拡張分をnullクリア
				memset(new_pool.data() + pool_.size(), 0, sizeof(Page*) * (new_pool.size() - pool_.size()));

				// move
				pool_ = std::move(new_pool);
			}

			// ページ自体が確保されていない場合.
			if (nullptr == pool_[page_id])
			{
				// ページを確保して初期化.
				pool_[page_id] = new Page();
				// 確保. block_byte_size_バイトのデータをblock_count_per_page_個.
				pool_[page_id]->data.resize(block_count_per_page_ * block_byte_size_);
			}

			// 使用状態にセット
			used_bits_.Set(empty_index, true);

			return Handle(empty_index);
		}

		// ハンドルが指す要素を解放して再利用可能にする.
		void Dealloc(Handle handle)
		{
			const uint32_t page_id = handle >> block_count_per_page_log2_;
			const uint32_t page_component_id = handle & page_block_index_mask_;

			// プールサイズチェック.
			check(pool_.size() > page_id);
			// プール要素が確保されているかチェック.
			check(nullptr != pool_[page_id]);
			// 使用ビットが真がチェック.
			check(used_bits_.Get(handle));

			// 未使用状態にセット.
			used_bits_.Set(handle, false);
		}

		// ハンドルが指す要素を取得する.
		const uint8_t* Get(Handle handle) const
		{
			const uint32_t page_id = handle >> block_count_per_page_log2_;
			const uint32_t page_component_id = handle & page_block_index_mask_;
			// プールサイズチェック.
			check(pool_.size() > page_id);
			// プール要素が確保されているかチェック.
			check(nullptr != pool_[page_id]);
			// 使用ビットが真がチェック.
			check(used_bits_.Get(handle));
			// 実データはblock_byte_size_単位
			return &(pool_[page_id]->data[page_component_id * block_byte_size_]);
		}
		// ハンドルが指す要素を取得する.
		uint8_t* Get(Handle handle)
		{
			const uint32_t page_id = handle >> block_count_per_page_log2_;
			const uint32_t page_component_id = handle & page_block_index_mask_;
			// プールサイズチェック.
			check(pool_.size() > page_id);
			// プール要素が確保されているかチェック.
			check(nullptr != pool_[page_id]);
			// 使用ビットが真がチェック.
			check(used_bits_.Get(handle));
			// 実データはblock_byte_size_単位
			return &(pool_[page_id]->data[page_component_id * block_byte_size_]);
		}

		// 使用状況をリセット. 確保メモリは維持.
		void Reset()
		{
			used_bits_.Fill(false);
		}


		// 直接アクセス用.
		// インデックスで直接有効フラグを取得する.
		bool IsValid(Handle index) const
		{
			const uint32_t page_id = index >> block_count_per_page_log2_;
			const uint32_t page_component_id = index & page_block_index_mask_;
			if ((pool_.size() <= page_id) || (nullptr == pool_[page_id]))
				return false;
			return used_bits_.Get(index);
		}
		// 直接アクセス用.
		// インデックスで直接要素を取得する.
		uint8_t* GetByIndex(uint32_t index)
		{
			const uint32_t page_id = index >> block_count_per_page_log2_;
			const uint32_t page_component_id = index & page_block_index_mask_;
			if (!used_bits_.Get(index))
				return nullptr;
			// 実データはblock_byte_size_単位
			return &(pool_[page_id]->data[page_component_id * block_byte_size_]);
		}
		// 直接アクセス用.
		// インデックスで直接要素を取得する.
		const uint8_t* GetByIndex(uint32_t index) const
		{
			const uint32_t page_id = index >> block_count_per_page_log2_;
			const uint32_t page_component_id = index & page_block_index_mask_;
			if (!used_bits_.Get(index))
				return nullptr;
			// 実データはblock_byte_size_単位
			return &(pool_[page_id]->data[page_component_id * block_byte_size_]);
		}
		// 直接アクセス用.
		// 最大インデックスを取得する
		uint32_t NumHandle() const
		{
			return static_cast<uint32_t>(pool_.size()) * block_count_per_page_;
		}

		// 確保メモリサイズ取得用.
		size_t GetAllocatedMemorySize() const
		{
			size_t s = 0;
			for (auto&& e : pool_)
			{
				if (e)
					s += e->data.size();
			}
			return s;
		}

	private:
		struct Page
		{
			std::vector<uint8_t> data;
		};

		uint32_t block_byte_size_ = 16;

		uint32_t block_count_per_page_log2_ = 0;
		uint32_t block_count_per_page_ = 0;
		uint32_t page_block_index_mask_ = 0;

		// page要素の使用状態を表す階層ビット配列.
		DynamicHierarchicalBitArray<> used_bits_;
		// Pageプール.
		std::vector<Page*>	pool_;
	};


	
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 固定サイズHashMapのエントリ構造体.
	struct OpenAddrFixedSize3dHashMapEntry
	{
		// CellIndex.
		uint16 pos[3];
		// ハッシュ衝突の解決のためのEntryへの相対オフセット. アドレス的に前方へのオフセットがあり得るためsigned.
		int16 link_offset;
		// 連番ユニークID. 最上位bitは使用状態に利用.
		uint32 index;
		
		static constexpr uint32 k_data_valid_mask = (1u << ((sizeof(uint32) * 8u) - 1u));
		static constexpr uint32 k_data_index_mask = ~(1u << ((sizeof(uint32) * 8u) - 1u));
		static constexpr uint32 k_invalid_index = ~((uint32)(0u));
		static constexpr uint32 GetIndex(const OpenAddrFixedSize3dHashMapEntry& entry)
		{
			return (entry.index & k_data_index_mask);
		}
		static constexpr void SetIndex(OpenAddrFixedSize3dHashMapEntry& entry, uint32 index)
		{
			entry.index = (entry.index & k_data_valid_mask) | (index & k_data_index_mask);
		}
		static constexpr bool GetValid(const OpenAddrFixedSize3dHashMapEntry& entry)
		{
			return (entry.index & k_data_valid_mask);
		}
		static constexpr void SetValid(OpenAddrFixedSize3dHashMapEntry& entry, bool valid)
		{
			const uint32 b = (valid) ? k_data_valid_mask : 0u;
			entry.index = (entry.index & k_data_index_mask) | (b);
		}
		static constexpr void SetIndexAndValid(OpenAddrFixedSize3dHashMapEntry& entry, bool valid, uint32 index)
		{
			const uint32 b = (valid) ? k_data_valid_mask : 0u;
			entry.index = (index & k_data_index_mask) | (b);
		}
		static constexpr OpenAddrFixedSize3dHashMapEntry GetInvalidEntry()
		{
			OpenAddrFixedSize3dHashMapEntry v{};
			SetValid(v, false);
			return v;
		}
	};


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 固定サイズテーブル上で管理されるオープンアドレス方式HashMap.
	// 3D空間IDからN未満のユニークIDへのマッピング.
	// https://niessnerlab.org/papers/2013/4hashing/niessner2013hashing.pdf
	// ENTRY_COUNT_LOG2 総エントリ数のLog2. 実際には 2^ENTRY_COUNT_LOG2 がエントリ総数.
	// BUCKET_SIZE_LOG2 Hash値一つに対応するバケットのエントリ数のLog2. 実際には 2^BUCKET_SIZE_LOG2 個のエントリがHash値一つに対応.
	template<uint32 ENTRY_COUNT_LOG2 = 10, uint32 BUCKET_SIZE_LOG2 = 2>
	class OpenAddrFixedSize3dHashMap
	{
	public:

		// ----------------------------------------------------------------------------------------------------------------------------
		// HashTable総エントリ数のLog2.
		static constexpr uint32	k_hash_table_total_entry_count_log2 = ENTRY_COUNT_LOG2;
		// HashTableのBucketサイズのLog2.
		static constexpr uint32	k_hash_table_bucket_size_log2 = BUCKET_SIZE_LOG2;;

		// HashTable総エントリ数. 実際のエントリ確保数.
		static constexpr uint32	k_hash_table_total_entry_count = 1 << k_hash_table_total_entry_count_log2;
		// HashTableのBucketサイズ.
		static constexpr uint32	k_hash_table_bucket_size = 1 << k_hash_table_bucket_size_log2;
		// HashTableのHashBucket数. 実際のEntryは k_hash_table_bucket_count * k_hash_table_bucket_size.
		static constexpr uint32	k_hash_table_bucket_count = k_hash_table_total_entry_count >> k_hash_table_bucket_size_log2;
		// HashTableのBucket内Indexのマスク.
		static constexpr uint32	k_hash_table_bucket_local_mask = (1 << k_hash_table_bucket_size_log2) - 1;
		
		static constexpr uint32	k_hash_table_bucket_id_mask = (1 << (k_hash_table_total_entry_count_log2 - k_hash_table_bucket_size_log2)) - 1;
		static constexpr uint32	k_full_bit_u32 = std::numeric_limits<uint32>::max();

		// Hash計算.
		static constexpr uint32 Hash(uint32 x, uint32 y, uint32 z)
		{
			// バケット数は二の冪なのでmodではなくmaskで計算.
			return (x * 73856093u + y * 19349669u + z * 8349279) & k_hash_table_bucket_id_mask;
		}

	public:
		OpenAddrFixedSize3dHashMap()
		{
			Reset();
		}
		void Init()
		{
			Reset();
		}
		void Reset()
		{
			// エントリの情報一部クリア.
			// 連番ユニークIndex設定.
			for (auto i = 0; i < table_.max_size(); ++i)
			{
				// 連番ID設定. これ以後書き換えはしない(Entryの移動で入れ替えは発生する).
				OpenAddrFixedSize3dHashMapEntry::SetIndexAndValid(table_[i], false, i);

				// link_offset無効化.
				table_[i].link_offset = 0;
			}
		}

		// 空間IDに対応するMappedIndexを検索する.
		// 未登録の空間IDの場合は無効なIndexを返す.
		uint32 Find(uint16 x, uint16 y, uint16 z) const
		{
			uint32 entry_index;
			// 存在すればdataを返す.
			if (FindInner(Hash(x, y, z), x, y, z, entry_index))
				return OpenAddrFixedSize3dHashMapEntry::GetIndex(table_[entry_index]);

			// 存在しなければ無効値を返す.
			return OpenAddrFixedSize3dHashMapEntry::k_invalid_index;
		}

		// FindやRegisterで取得したMappedIndexが有効かチェック.
		bool CheckValidIndex(uint32 mapped_index) const
		{
			//return OpenAddrFixedSize3dHashMapEntry::GetValid(mapped_index);
			return (table_.max_size() > mapped_index);
		}

		// 無効値として返すオブジェクト
		static constexpr OpenAddrFixedSize3dHashMapEntry k_invalid_entry_dummy = OpenAddrFixedSize3dHashMapEntry::GetInvalidEntry();
		// Indexによる直接Entryアクセス. 
		// mapped_index -> [0, k_hash_table_total_entry_count-1].
		const OpenAddrFixedSize3dHashMapEntry& GetEntryDirect(uint32 mapped_index) const
		{
			if (!CheckValidIndex(mapped_index))
				return k_invalid_entry_dummy;
			return table_[mapped_index];
		}

		// エントリの削除.
		void Unregister(uint16 x, uint16 y, uint16 z)
		{
			const uint16 q[3] = { x, y, z };

			const auto hash = Hash(x, y, z);

			// Hash値 * BucketサイズでHeadBucketの先頭.
			const auto head_index = hash * k_hash_table_bucket_size;

			auto entry_index = head_index;
			// HeadBucket内を探索.
			for (int li = 0; li < k_hash_table_bucket_size; ++li)
			{
				if (CheckEntryFlag(entry_index) && (0 == memcmp(table_[entry_index].pos, q, sizeof(q))))
				{
					// HeadBucket内でクエリと一致するEntryが見つかった.
					if (0 == table_[entry_index].link_offset)
					{
						// リンクが無ければ単に削除して終了.
						ResetEntry(entry_index);
					}
					else
					{
						// リンクがある場合はリンク先と自身の情報を入れ替えてからリンク先だった場所を無効化.
						// これはユニークインデックスがズレないようにするために必要.
						const auto old_link_offset = table_[entry_index].link_offset;
						const auto link_index = entry_index + old_link_offset;

						// 入れ替えとリンク補正.
						std::swap(table_[entry_index], table_[link_index]);

						ResetEntry(link_index);

						// リンク先からさらにリンクがある場合は移動分オフセット.
						if (0 != table_[entry_index].link_offset)
							table_[entry_index].link_offset += old_link_offset;
					}

					// 終了.
					return;
				}
				++entry_index;
			}

			// HeadBucket内に見つからなかったので末尾リンクリスト探索のためにインデックスを末尾に修正.
			entry_index = head_index + k_hash_table_bucket_size - 1;

			// HeadBucket内を線形探索しても見つからなかった場合は, 以後link_offsetを辿って探索する.
			for (;;)
			{
				// link_offsetが空の場合はリンク無しのため終了. 初回はHeadBucketの末尾.
				if (0 == table_[entry_index].link_offset)
					break;

				// リンクを辿る. 相対オフセットは負の可能性もある.
				const auto next_index = entry_index + table_[entry_index].link_offset;

				// 自身が有効で且つリンクがある場合はnextも存在する.
				assert(CheckEntryFlag(next_index));
				if (CheckEntryFlag(entry_index) && (0 == memcmp(table_[next_index].pos, q, sizeof(q))))
				{
					// 次のエントリがクエリと一致した.
					// 次のエントリからリンクがあれば前のエントリのリンクを繋げ, リンクが無ければリンク削除.
					table_[entry_index].link_offset = (0 == table_[next_index].link_offset) ? 0 : table_[entry_index].link_offset + table_[next_index].link_offset;

					ResetEntry(next_index);

					// 終了.
					break;
				}

				// 移動
				entry_index = next_index;
			}
		}

		// エントリの挿入.
		// 戻り値はMappedIndex. 失敗した場合は無効Index.
		uint32 Register(uint16 x, uint16 y, uint16 z)
		{
			// 検索.
			auto table_index = k_full_bit_u32;

			const auto h = Hash(x, y, z);

			if (!FindInner(h, x, y, z, table_index))
			{
				// Entryが見つからなかったので書き込み先を探す.
				// Findに渡した table_index には同一Hash値のリンクリストの末端Entryインデックスが格納されている.
				const uint16 q[3] = { x, y, z };
				// Hash値 * BucketサイズでHeadBucketの先頭.
				const auto head_index = h * k_hash_table_bucket_size;

				auto empty_index = head_index;
				bool is_find = false;
				// まずHeadBucket内を先頭から探索.
				for (auto li = 0; li < k_hash_table_bucket_size; ++li)
				{
					if (!CheckEntryFlag(empty_index))
					{
						// 未使用Entry発見
						is_find = true;

						// 座標書き込み.
						memcpy(table_[empty_index].pos, q, sizeof(q));
						// 有効化
						OpenAddrFixedSize3dHashMapEntry::SetValid(table_[empty_index], true);

						break;
					}
					++empty_index;
				}

				// HeadBucket内で空きがない場合は連続する次のBucketの先頭から,それぞれのBucketの末尾要素をスキップしながら線形探索する.
				//	(Bucketの末尾要素は必ずそのBucketをHeadBucketとするEntryが利用するため).
				//	HeadBucketに連続する次のBucketからBucket単位で探索. (並列処理のためにBucket単位で排他処理するかもしれないので).
				for (auto bi = h + 1; !is_find && bi < k_hash_table_bucket_count; ++bi)
				{
					const auto bucket_head_index = bi * k_hash_table_bucket_size;
					// k_hash_table_bucket_size - 1 である理由は, Bucketの末尾はそこをHeadBucketとするEntryに予約されているため.
					for (auto li = 0; li < k_hash_table_bucket_size - 1; ++li)
					{
						if (!CheckEntryFlag(bucket_head_index + li))
						{
							// 未使用Entry発見.
							empty_index = bucket_head_index + li;
							is_find = true;
							break;
						}
					}
				}
				// 後方に見つからない場合は前方を探索.
				for (auto bi = 0u; !is_find && bi < h; ++bi)
				{
					// 一つ前のBucketへ順に探索.
					const auto bucket_head_index = (h - bi - 1) * k_hash_table_bucket_size;
					// k_hash_table_bucket_size - 1 である理由は, Bucketの末尾はそこをHeadBucketとするEntryに予約されているため.
					for (auto li = 0; li < k_hash_table_bucket_size - 1; ++li)
					{
						if (!CheckEntryFlag(bucket_head_index + li))
						{
							// 未使用Entry発見.
							empty_index = bucket_head_index + li;
							is_find = true;
							break;
						}
					}
				}

				if (is_find && h != (empty_index >> k_hash_table_bucket_size_log2))
				{
					// ハッシュと書き込みBucket位置が異なる場合はリンク更新が必要.
					// 最初のFindで返ってきたリンクリスト末尾の後ろに新規Entryを繋げる.
					const int link_offset = static_cast<int>(empty_index) - static_cast<int>(table_index);
					const bool is_safe_range = (std::numeric_limits<int16>::min() <= link_offset) && (std::numeric_limits<int16>::max() >= link_offset);

					assert(is_safe_range);
					if (is_safe_range)
					{
						// 同一Hash値リンクリスト末端Entryから新規Entryへの相対オフセットを書き込み.
						table_[table_index].link_offset = static_cast<int16>(link_offset);

						// 座標書き込み.
						memcpy(table_[empty_index].pos, q, sizeof(q));
						// 有効化
						OpenAddrFixedSize3dHashMapEntry::SetValid(table_[empty_index], true);

						is_find = true;
					}
					else
					{
						// オフセットが表現できない範囲であったため失敗.(基本的には無いはず).
						is_find = false;
					}
				}

				// 最終チェック.
				if (is_find)
				{
					table_index = empty_index;
				}
				else
				{
					// 空きEntryが見つからなかったので不正値を入れておく.
					table_index = k_full_bit_u32;
				}
			}

			return (static_cast<uint32>(table_.max_size()) > table_index) ? OpenAddrFixedSize3dHashMapEntry::GetIndex(table_[table_index]) : OpenAddrFixedSize3dHashMapEntry::k_invalid_index;
		}

	private:
		// エントリリセット.
		void ResetEntry(uint32 index)
		{
			// リンクオフセットクリア.
			table_[index].link_offset = 0;

			//使用状況フラグクリア.
			OpenAddrFixedSize3dHashMapEntry::SetValid(table_[index], false);
		}
		// エントリの使用状況チェック.
		bool CheckEntryFlag(uint32 index) const
		{
			return OpenAddrFixedSize3dHashMapEntry::GetValid(table_[index]);
		}

		// エントリの検索.
		// out_index : 検索成功でEntryのインデックス, 失敗の場合はリンクリストの末端のEntityハンドル.
		// 検索に失敗した場合は false を返す.
		// 内部処理でリンク末端を同時に検索する必要があるため専用に定義.
		bool FindInner(uint32 hash, uint16 x, uint16 y, uint16 z, uint32& out_index) const
		{
			const uint16 q[3] = { x, y, z };

			// Hash値 * BucketサイズでHeadBucketの先頭.
			const auto head_index = hash * k_hash_table_bucket_size;

			out_index = head_index;
			// HeadBucket内を探索.
			for (int li = 0; li < k_hash_table_bucket_size; ++li)
			{
				if (CheckEntryFlag(out_index) && (0 == memcmp(table_[out_index].pos, q, sizeof(q))))
				{
					// クエリと一致するEntryが見つかったのでtrueを返す.
					// out_index には発見したEntryのインデックスが書き込まれている.
					return true;
				}
				++out_index;
			}

			// HeadBucket内に見つからなかったので末尾リンクリスト探索のためにインデックスを末尾に修正.
			out_index = head_index + k_hash_table_bucket_size - 1;

			// HeadBucket内を線形探索しても見つからなかった場合は, 以後link_offsetを辿って探索する.
			for (;;)
			{
				// link_offsetが空の場合はリンク無しのため終了.
				if (0 == table_[out_index].link_offset)
					break;

				// リンクを辿る. 相対オフセットは負の可能性もある.
				out_index += table_[out_index].link_offset;

				if (CheckEntryFlag(out_index) && (0 == memcmp(table_[out_index].pos, q, sizeof(q))))
				{
					// クエリと一致するEntryが見つかったのでtrueを返す.
					// out_index には発見したEntryのインデックスが書き込まれている.
					return true;
				}
			}

			// 存在しなかったためfalseを返す.
			// out_index には同一Hash値リンクリストの末端Entryインデックスが書き込まれている.
			return false;
		}

	public:
		// 動作チェック用.
		bool DebugCheck() const
		{
			// 連番インデックスがすべて存在するか.
			for (auto search_id = 0u; search_id < table_.max_size(); ++search_id)
			{
				bool v = false;
				for (auto i = 0; (!v) && i < table_.max_size(); ++i)
				{
					v |= (search_id == OpenAddrFixedSize3dHashMapEntry::GetIndex(table_[i]));
				}
				// 見つからなかった場合ロジックミスがある.
				if (!v)
				{
					return false;
				}
			}
			return true;
		}

	private:
		// Bucketサイズ * Bucket数のEntryTable.
		std::array<OpenAddrFixedSize3dHashMapEntry, k_hash_table_bucket_count* k_hash_table_bucket_size > table_;
	};
	
}













