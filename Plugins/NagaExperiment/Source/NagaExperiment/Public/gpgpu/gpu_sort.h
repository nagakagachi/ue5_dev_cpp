// @author: @nagakagachi
#pragma once

// shader
#include "GlobalShader.h"

#include "PipelineStateCache.h"
#include "RHIStaticStates.h"
#include "SceneUtils.h"
#include "SceneInterface.h"

#include "util/math_util.h"
#include "gpu_structured_buffer.h"

namespace naga::gpgpu
{

	// Uint2.
	struct UintVec2
	{
		uint32 x;
		uint32 y;
	};
	// Uint3.
	struct UintVec3
	{
		uint32 x;
		uint32 y;
		uint32 z;
	};
	
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Inplace Dispatch Reduced Bitonic Sort
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Dispatch削減バイトニックソートのDispatch単位用情報.
	struct DispatchReduceBitonicSortParam
	{
		uint32 bit0;
		uint32 gap01;
		uint32 bit1;
		uint32 gap12;

		uint32 start_step;
		uint32 end_step;
	};
	// Dispatch削減バイトニックソートの事前計算データ管理.
	class DispatchReduceBitonicSortPrecompParam
	{
	public:
		DispatchReduceBitonicSortPrecompParam()
		{
		}
		~DispatchReduceBitonicSortPrecompParam()
		{
		}

		// 事前計算パラメータ更新.
		// 更新があった場合は真を返す.
		bool UpdatePrecomp(uint32 num_element, uint32 thread_group_size)
		{
			// 指定数以上の最小の2の冪乗数
			const auto num_element_msb = math::Msb(FMath::Max(num_element, uint32(2)) - 1) + 1;
			const auto num_element_as_power_of_2 = 1 << num_element_msb;

			bool need_recreate = false;
			if (num_element_ != num_element_as_power_of_2 || thread_group_size_ != thread_group_size)
			{
				need_recreate = true;
			}

			if (!need_recreate)
				return false;

			num_element_ = num_element_as_power_of_2;
			thread_group_size_ = thread_group_size;

			sort_step_info_array_.Empty();
			sort_dispatch_param_array_.Empty();
			dispatch_count_ = 0;

			sort_step_count_ = num_element_msb * (num_element_msb + 1) / 2;
			sort_step_info_array_.SetNumZeroed(sort_step_count_);
			sort_dispatch_param_array_.SetNumZeroed(sort_step_count_);

			const auto sort_cs_thread_count = thread_group_size_;
			const auto m_local_bit_count = math::Lsb(sort_cs_thread_count) + 1;

			//グループスレッドが連続したn-byteへアクセスするように事前計算データを補正する.
			// SharedMemory利用版ではここでlinear_access_maskが7になるようにしないとソート結果がほんの少し失敗する. グループスレッド数512, 要素数2048, linear_access_maskが0だと発生. 要調査.
			const uint32_t linear_access_byte_size = 64; // 連続アクセスバイトサイズ.
			// 連続アクセスバイトサイズを要素数換算してマスク作成. 連続アクセス要素数以下の交換間隔マスク部を全て1で埋める,
			const uint32_t linear_access_mask = (uint32_t)(linear_access_byte_size / sizeof(UintVec2) - 1);

			// パラメータバッファとディスパッチ単位定数バッファの値を事前計算.
			{
				// 基本の情報を設定
				int step_index = 0;
				for (uint32 i = 1; i < num_element_; i = i << 1)
				{
					for (uint32_t j = i; j > 0; j = j >> 1)
					{
						sort_step_info_array_[step_index].x = math::Lsb(i << 1);
						sort_step_info_array_[step_index].y = j;
						sort_step_info_array_[step_index].z = 0;
						step_index++;
					}
				}
			}

			uint32_t dispatch_count = 0;
			{
				// ディスパッチ単位毎にパラメータを計算して格納
				uint32_t current_mask = linear_access_mask;// 0;
				uint32_t start_stage = 0;
				for (uint32 step_index = 0; step_index != sort_step_count_; step_index++)
				{
					const uint32_t cur_j = sort_step_info_array_[step_index].y;
					const uint32_t next_j = ((sort_step_count_ - 1) > step_index) ? sort_step_info_array_[step_index + 1].y : 0;

					// 1ディスパッチで実行するステップの交換間隔jについて全て論理和をとったワークグループマスク.
					// 1, 2, 1, 4, 2, 1, 8, 4, 2, 1,... の論理和をとっている.
					current_mask |= cur_j;
					// ワークグループマスクのビットカウントがスレッドグループのビットカウントを上回る直前でディスパッチを一つ追加する.
					if ((step_index == (sort_step_count_ - 1)) || (math::BitCount(current_mask | next_j) > m_local_bit_count))
					{
						{
							// adjust granurarity when working set bits are not fully populated
							// ワークグループマスクのビットカウントがスレッドグループビットカウントに満たない場合は0のビットを1にして数を合わせる.
							const uint32_t group_scale = math::BitCount(current_mask);
							uint32_t l = 1;
							for (int k = group_scale; k < m_local_bit_count; k++)
							{
								while ((l & current_mask) != 0)
									l <<= 1;
								current_mask |= l;
							}
						}

						const uint32_t mask = current_mask | (num_element_ << 1);
						const int bit0 = math::Lsb(~mask);
						const int bit1 = math::Lsb(mask & ~((((uint32_t)1) << bit0) - 1));
						const int bit2 = math::Lsb(~mask & ~((((uint32_t)1) << bit1) - 1));

						// ディスパッチ単位用情報
						auto sort_dispatch_param = &sort_dispatch_param_array_[dispatch_count];
						++dispatch_count;
						sort_dispatch_param->bit0 = bit0;
						sort_dispatch_param->gap01 = bit1 - bit0;
						sort_dispatch_param->bit1 = bit1;
						sort_dispatch_param->gap12 = bit2 - bit1;
						sort_dispatch_param->start_step = start_stage;
						sort_dispatch_param->end_step = step_index;

						// zに事前計算の値を設定.
						for (uint32 k = start_stage; k <= step_index; k++)
						{
							const uint32_t j = sort_step_info_array_[k].y;
							const uint32_t bit_mask = ((uint32_t)1 << bit1) - 1;
							sort_step_info_array_[k].z = (j & bit_mask) | ((j & ~bit_mask) >> (bit1 - bit0));
						}

						current_mask = linear_access_mask;// 0;
						start_stage = step_index + 1;
					}
				}
			}
			dispatch_count_ = dispatch_count;
			return true;
		}

		// 総ディスパッチ数.
		uint32										dispatch_count_ = 0;
		// 総ステップ数
		uint32										sort_step_count_ = 0;
		// ステップごとのパラメータ. シェーダリソースにコピーされてSrvとしてシェーダで利用される.
		TArray<UintVec3>							sort_step_info_array_;
		// ディスパッチ単位での定数バッファの値の配列.
		TArray<DispatchReduceBitonicSortParam>	sort_dispatch_param_array_;

	protected:
		// 以下の要素が変化したら再計算
		uint32	thread_group_size_ = 0;
		uint32	num_element_ = 0;
	};

	// Sorterオブジェクト
	class GpuDispatchReduceBitonicSortUint2
	{
	public:
		GpuDispatchReduceBitonicSortUint2();
		~GpuDispatchReduceBitonicSortUint2();

		// Call From GameThread.
		// max_num_element : 内部でディスパッチパラメータ用バッファを作成する必要があるため対応する最大の要素数を指定.
		bool Init(uint32 max_num_element);
		// Call From RenderThread.
		void Run(FRHICommandListImmediate& cmdlist, ERHIFeatureLevel::Type FeatureLevel, FStructuredBufferResource<UintVec2>& buffer);

	private:
		bool										is_work_buffer_init_jit_ = true;
		// 新ソートシェーダステップ情報バッファ
		FStructuredBufferResource<UintVec3>		step_info_buffer_;
		// 新ソートシェーダ用事前計算データ
		DispatchReduceBitonicSortPrecompParam	precomp_param_;
	};
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------



	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Simple Bitonic Sort with out-of-place Tiled Transpose
	//	シンプルな実装のBitonicSort
	//		スレッドグループサイズ以上のバッファに対してはout-of-placeな転置シェーダを利用して同一のシェーダでソートを実行する
	//		現状の実装では内部で転置用作業用のワークバッファを作成します.
	//		スレッドグループサイズ以下の要素のソートを1ディスパッチに減らす改造を施す予定(現在は10ディスパッチ)
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	class GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2
	{
	public:
		GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2();
		~GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2();

		// Call From GameThread.
		// max_num_element : 内部でディスパッチパラメータ用バッファを作成する必要があるため対応する最大の要素数を指定.
		bool Init(uint32 max_num_element);
		// Call From RenderThread.
		void Run(FRHICommandListImmediate& cmdlist, ERHIFeatureLevel::Type FeatureLevel, FStructuredBufferResource<UintVec2>& buffer);
	private:
		bool										is_work_buffer_init_jit_ = true;
		FStructuredBufferResource<UintVec2>		transpose_work_buffer_;
	};
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
}
