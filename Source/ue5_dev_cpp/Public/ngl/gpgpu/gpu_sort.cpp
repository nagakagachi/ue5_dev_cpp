#include "gpu_sort.h"

#include "RHICommandList.h"
#include "DataDrivenShaderPlatformInfo.h"

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Inplace Dispatch Reduced Bitonic Sort
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
/*
// シェーダメモ.
#ifndef THREAD_COUNT
#define THREAD_COUNT 512
#endif

// ソート対象リソース
RWStructuredBuffer<uint2> array_uav;
// ステップ情報バッファ
StructuredBuffer<uint3> bitonic_step_info_srv;

// ---------------------------------------------------------------------------------------------------------------------------------
// 定数バッファ
uint cb_sort_element_count;
uint cb_step_bit0;
uint cb_step_gap01;
uint cb_step_bit1;
uint cb_step_gap12;
uint cb_start_step;
uint cb_end_step;

// 共有メモリ
groupshared uint2 group_memory[THREAD_COUNT*2];

// Dispatch数削減版BitonicSortバージョン0
// 全体のソートステップを同一要素郡で処理できる単位に区切ってDispatchする. 内部ステップはグループバリアで同期する.
// 要素数は2の冪乗
[numthreads(THREAD_COUNT, 1, 1)]
void BitonicSortDispatchReduceV0_Uint2(uint3 dispatch_id : SV_DispatchThreadID, uint3 id_in_group : SV_GroupThreadID, uint index_in_group : SV_GroupIndex, uint3 group_id : SV_GroupID)
{
	const uint invalid_value = ~uint(0x00);

	const uint bit0 = cb_step_bit0;
	const uint gap01 = cb_step_gap01;
	const uint bit1 = cb_step_bit1;
	const uint gap12 = cb_step_gap12;
	const uint start_step = cb_start_step;
	const uint end_step = cb_end_step;

	// グループ固定値.
	const uint mask_bit0 = (((uint) 1) << bit0) - 1;
	const uint mask_bit1 = (((uint) 1) << bit1) - 1;
	const uint shifted_group_id = group_id.x << bit0;
	const uint group_bits = (shifted_group_id & mask_bit1) | ((shifted_group_id & ~mask_bit1) << gap12);

	// 開始ステップ用の情報.
	int step = start_step;
	uint3 ij = bitonic_step_info_srv[step];
	uint shiftI = ij.x;
	uint j = ij.y;
	uint localJ = ij.z;
	uint mask = localJ - 1;
	uint local_index = (index_in_group & mask) | ((index_in_group & ~mask) << 1);
	uint thread_bits = (local_index & mask_bit0) | ((local_index & ~mask_bit0) << gap01);
	uint index = group_bits + thread_bits;

	// Dispatchの開始ステップから終了ステップで処理する範囲のデータを共有メモリに読み込み.
	// 更に開始ステップでの入れ替えも同時に実行.
	uint2 data0 = array_uav[index];
	uint2 data1 = array_uav[index + j];
	if (((index >> shiftI) ^ (data1.x < data0.x)) & 1)
	{
		group_memory[local_index] = data1;
		group_memory[local_index + localJ] = data0;
	}
	else
	{
		group_memory[local_index] = data0;
		group_memory[local_index + localJ] = data1;
	}

	// 開始ステップ+1から終了ステップ-1までの処理ループ.
	// ループを抜ける際に一時変数に終了ステップの情報を格納して抜ける.
	while (true)
	{
		++step;
		ij = bitonic_step_info_srv[step];
		shiftI = ij.x;
		localJ = ij.z;
		mask = localJ - 1;
		thread_bits = index_in_group;
		local_index = (thread_bits & mask) | ((thread_bits & ~mask) << 1);
		GroupMemoryBarrierWithGroupSync();
		// このステップでの入れ替え対象の値を一時変数に格納
		data0 = group_memory[local_index];
		data1 = group_memory[local_index + localJ];
		if (step >= end_step)
		{
			// 終了ステップの場合は一時変数に格納するだけで入れ替えはせずに抜ける. 最後の入れ替えはループを抜けた後にUAVへの書き戻しと同時に実行.
			break;
		}

		// 入れ替え
		if (((index >> shiftI) ^ (data1.x < data0.x)) & 1)
		{
			group_memory[local_index] = data1;
			group_memory[local_index + localJ] = data0;
		}
	}

	// 最後のループで一時変数に格納した終了ステップの情報で最後の入れ替えとUAVへの書き戻しを実行.
	thread_bits = (local_index & mask_bit0) | ((local_index & ~mask_bit0) << gap01);
	index = group_bits + thread_bits;
	j = ij.y;
	if (((index >> shiftI) ^ (data1.x < data0.x)) & 1)
	{
		array_uav[index] = data1;
		array_uav[index + j] = data0;
	}
	else
	{
		array_uav[index] = data0;
		array_uav[index + j] = data1;
	}
}

*/

namespace ngl::gpgpu
{
	// ディスパッチ数削減BitonicSort
	// Dispatch Reduce Bitonic Sort Shader
	class FDispatchReduceBitonicSortUint2 : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(FDispatchReduceBitonicSortUint2, Global);

	public:
		static constexpr int CS_DISPATCH_THREAD_COUNT()
		{
			// 二の冪
			return 512; // 通常は512スレッドで,1グループ1024要素の処理. 
		}

		static bool ShouldCache(EShaderPlatform Platform)
		{
			return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

			// シェーダ切り替えマクロ指定
			OutEnvironment.SetDefine(TEXT("NGL_CS_ENTRYPOINT_DISPATCH_REDUCE_BITONIC_SORT_UINT2_V0"), 1);

			// ソートのグループスレッド数
			OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());
		}

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			// Useful when adding a permutation of a particular shader
			return true;
		}

		FDispatchReduceBitonicSortUint2() {}

		// シェーダパラメータと変数をバインド
		FDispatchReduceBitonicSortUint2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			:FGlobalShader(Initializer)
		{
			// シェーダパラメータと変数をバインド
			cb_sort_element_count_.Bind(Initializer.ParameterMap, TEXT("cb_sort_element_count"));
			cb_step_bit0_.Bind(Initializer.ParameterMap, TEXT("cb_step_bit0"));
			cb_step_gap01_.Bind(Initializer.ParameterMap, TEXT("cb_step_gap01"));
			cb_step_bit1_.Bind(Initializer.ParameterMap, TEXT("cb_step_bit1"));
			cb_step_gap12_.Bind(Initializer.ParameterMap, TEXT("cb_step_gap12"));
			cb_start_step_.Bind(Initializer.ParameterMap, TEXT("cb_start_step"));
			cb_end_step_.Bind(Initializer.ParameterMap, TEXT("cb_end_step"));

			array_uav_.Bind(Initializer.ParameterMap, TEXT("array_uav"));
			bitonic_step_info_srv_.Bind(Initializer.ParameterMap, TEXT("bitonic_step_info_srv"));
		}

		void SetParameters(FRHICommandList& RHICmdList,
			uint32 cb_sort_element_count,
			uint32 cb_step_bit0,
			uint32 cb_step_gap01,
			uint32 cb_step_bit1,
			uint32 cb_step_gap12,
			uint32 cb_start_step,
			uint32 cb_end_step,

			FRHIShaderResourceView* bitonic_step_info_srv,
			FRHIUnorderedAccessView* array_uav
		)
		{
			// 呼び出し直前にPipelineに自身が設定されているものとする.
			FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
			// UE5.3
			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
			SetShaderValue(BatchedParameters, cb_sort_element_count_, cb_sort_element_count);
			SetShaderValue(BatchedParameters, cb_step_bit0_, cb_step_bit0);
			SetShaderValue(BatchedParameters, cb_step_gap01_, cb_step_gap01);
			SetShaderValue(BatchedParameters, cb_step_bit1_, cb_step_bit1);
			SetShaderValue(BatchedParameters, cb_step_gap12_, cb_step_gap12);
			SetShaderValue(BatchedParameters, cb_start_step_, cb_start_step);
			SetShaderValue(BatchedParameters, cb_end_step_, cb_end_step);

			// SRV
			SetSRVParameter(BatchedParameters, bitonic_step_info_srv_, bitonic_step_info_srv);

			// UAV
			SetUAVParameter(BatchedParameters, array_uav_, array_uav);
		
			RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_sort_element_count_, cb_sort_element_count);
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_step_bit0_, cb_step_bit0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_step_gap01_, cb_step_gap01);
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_step_bit1_, cb_step_bit1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_step_gap12_, cb_step_gap12);
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_start_step_, cb_start_step);
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_end_step_, cb_end_step);

			// SRV
			SetSRVParameter(RHICmdList, ComputeShaderRHI, bitonic_step_info_srv_, bitonic_step_info_srv);

			// UAV
			SetUAVParameter(RHICmdList, ComputeShaderRHI, array_uav_, array_uav);
#endif
		}
		void ResetParameters(FRHICommandList& RHICmdList)
		{
			// 呼び出し直前にPipelineに自身が設定されているものとする.
			FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
			// UE5.3
			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
			// SRV
			SetSRVParameter(BatchedParameters, bitonic_step_info_srv_, nullptr);

			// UAV
			SetUAVParameter(BatchedParameters, array_uav_, nullptr);
		
			RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
			// SRV
			SetSRVParameter(RHICmdList, ComputeShaderRHI, bitonic_step_info_srv_, nullptr);

			// UAV
			SetUAVParameter(RHICmdList, ComputeShaderRHI, array_uav_, nullptr);
#endif
		}

		// bitonic sort用定数
		LAYOUT_FIELD(FShaderParameter , cb_sort_element_count_);
		LAYOUT_FIELD(FShaderParameter , cb_step_bit0_);
		LAYOUT_FIELD(FShaderParameter , cb_step_gap01_);
		LAYOUT_FIELD(FShaderParameter , cb_step_bit1_);
		LAYOUT_FIELD(FShaderParameter , cb_step_gap12_);
		LAYOUT_FIELD(FShaderParameter , cb_start_step_);
		LAYOUT_FIELD(FShaderParameter , cb_end_step_);

		// ソート対象バッファ
		LAYOUT_FIELD(FShaderResourceParameter , array_uav_);

		// bitonic ステップパラメータ
		LAYOUT_FIELD(FShaderResourceParameter , bitonic_step_info_srv_);
	};
	// シェーダコードとGlobalShaderクラスを関連付け
	IMPLEMENT_SHADER_TYPE(, FDispatchReduceBitonicSortUint2, TEXT("/Plugin/NagaExperiment/Private/compute_sort_dispatch_reduce.usf"), TEXT("BitonicSortDispatchReduceV0_Uint2"), SF_Compute)

	GpuDispatchReduceBitonicSortUint2::GpuDispatchReduceBitonicSortUint2()
	{
		is_work_buffer_init_jit_ = true;
	}
	GpuDispatchReduceBitonicSortUint2::~GpuDispatchReduceBitonicSortUint2()
	{
		// 解放
		step_info_buffer_.ReleaseResource();
	}

	// Call From GameThread.
	// max_num_element : 内部でディスパッチパラメータ用バッファを作成する必要があるため対応する最大の要素数を指定.
	bool GpuDispatchReduceBitonicSortUint2::Init(uint32 max_num_element)
	{
		// 事前計算データ構築. スレッド数は実際のCSと一致する必要がある. 現在の実装では1スレッドで2要素入れ替え処理をする実装.
		precomp_param_.UpdatePrecomp(max_num_element, FDispatchReduceBitonicSortUint2::CS_DISPATCH_THREAD_COUNT());

		if (!is_work_buffer_init_jit_)
		{
			// ステップ単位情報バッファ生成.
			step_info_buffer_.Init(precomp_param_.sort_step_count_, false, false);
			BeginInitResource(&step_info_buffer_);
		}
		return true;
	}
	// Call From RenderThread.
	void GpuDispatchReduceBitonicSortUint2::Run(FRHICommandListImmediate& cmdlist, ERHIFeatureLevel::Type FeatureLevel, FStructuredBufferResource<UintVec2>& buffer)
	{
		auto* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

		// シェーダ
		TShaderMapRef<FDispatchReduceBitonicSortUint2> sort_cs(GlobalShaderMap);

		const auto num_element = buffer.NumElement();

		// バッファサイズが二の冪かチェック
		if (!math::IsPowerOfTwo(num_element))
		{
			// 二の冪上でない場合はどうするか
			// 現状は何もせずに戻る
			UE_LOG(LogTemp, Log, TEXT("[%hs] Need Buffer Size Power of 2."), "GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2");
			return;
		}

		// 事前計算パラメータの更新が必要なら処理が実行される.
		precomp_param_.UpdatePrecomp(num_element, sort_cs->CS_DISPATCH_THREAD_COUNT());

		// 必要ならバッファ作り直し. この関数はRenderThread実行なのでOK.
		if (is_work_buffer_init_jit_)
		{
			const int need_buffer_size = precomp_param_.sort_step_count_;
			if (need_buffer_size > step_info_buffer_.NumElement())
			{
				// RenderThreadなので直接RHI破棄
				step_info_buffer_.ReleaseRHI();

				// リソース初期化
				step_info_buffer_.Init(need_buffer_size, false, false);
				// RHI初期化
				step_info_buffer_.InitRHI(cmdlist);
			}
		}

		// 事前計算パラメータをシェーダリソースにコピー
		// 本来は要素数やグループスレッドサイズが固定なら最初に一回実行するだけで良いが更新が必要かどうかの判定が面倒なので毎フレームMapして書き込み.
		{
			uint32 lockByteSize = step_info_buffer_.NumElement() * step_info_buffer_.Stride();
			// ロック
			void* locked_memory = cmdlist.LockBuffer(step_info_buffer_.GetBuffer(), 0, lockByteSize, RLM_WriteOnly);
			// 全体コピー
			FPlatformMemory::Memcpy(locked_memory, precomp_param_.sort_step_info_array_.GetData(), lockByteSize);
			// アンロック
			cmdlist.UnlockBuffer(step_info_buffer_.GetBuffer());
		}

		// Dispatch
		{
			// bufferは呼び出し側でUAVステートにしておく.

			cmdlist.PushEvent(TEXT("BitonicSortDispatchReduce"), FColor::Green);
			for (uint32_t dispatch_index = 0; dispatch_index < precomp_param_.dispatch_count_; ++dispatch_index)
			{
				FString GpuEventName = FString::Format(TEXT("BitonicSortDispatchReduce_{0}"), { dispatch_index });
				cmdlist.PushEvent(*GpuEventName, FColor::Green);


				const auto* dispatch_param = &precomp_param_.sort_dispatch_param_array_[dispatch_index];

#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(cmdlist, sort_cs.GetComputeShader());
#else
				cmdlist.SetComputeShader(sort_cs.GetComputeShader());
#endif
			
				sort_cs->SetParameters(cmdlist,
					num_element,
					dispatch_param->bit0,
					dispatch_param->start_step,
					dispatch_param->gap01,
					dispatch_param->bit1,
					dispatch_param->start_step,
					dispatch_param->end_step,
					step_info_buffer_.GetSrv(),
					buffer.GetUav()
					);

				const int CsThreadCount = sort_cs->CS_DISPATCH_THREAD_COUNT();
				// 1スレッドが2要素担当するためそれを考慮してディスパッチ
				DispatchComputeShader(cmdlist, sort_cs, (num_element / 2 + (CsThreadCount - 1)) / CsThreadCount, 1, 1);

				sort_cs->ResetParameters(cmdlist);

				// UAV-ARV->UAVで同期.
				cmdlist.Transition(FRHITransitionInfo(buffer.GetUav(), buffer.GetCurrentRhiState(), buffer.TransitionRhiState(ERHIAccess::SRVCompute)));
				cmdlist.Transition(FRHITransitionInfo(buffer.GetUav(), buffer.GetCurrentRhiState(), buffer.TransitionRhiState(ERHIAccess::UAVCompute)));

				cmdlist.PopEvent();
			}

			cmdlist.PopEvent();
		}
	}
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------







	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// out-of-place tile transposed Bitonic Sort 
	// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	/*
		// シェーダコード
		// メインの起動スレッド数
		#ifndef THREAD_COUNT
		#define THREAD_COUNT 1024
		#endif
	
		// ディスパッチサイズよりも小さいバッファサイズを許容するか. 許容する場合はサイズチェック処理が追加される.
		#ifndef ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE
		#define ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE 1
		#endif
	
		// ソート対象リソース
		RWStructuredBuffer<uint2> array_uav;
		StructuredBuffer<uint2> array_srv;
	
		// ---------------------------------------------------------------------------------------------------------------------------------
		// 定数バッファ
		uint cb_bitonic_sort_block_size; // バイトニックソートのブロックサイズ, 2, 4, 8, 16
		uint cb_bitonic_sort_order_mask; // バイトニックソートのブロック毎のソート向きマスク 2-> 0<1 2>3 4<5 6>7, 4-> 0<1<2<3 4>5>6>7
	
		// 共有メモリ
		groupshared uint key_group_memory[THREAD_COUNT];
		groupshared uint value_group_memory[THREAD_COUNT];
	
		// バイトニックソート. inplace.
		// ブロックサイズ cb_bitonic_sort_block_size まで対応. バッファをcb_bitonic_sort_block_size*N行列とみなして転置すればcb_bitonic_sort_block_size*cb_bitonic_sort_block_sizeのサイズまで対応可能.
		[numthreads(THREAD_COUNT, 1, 1)]
		void BitonicSort_Uint2(uint3 dispatch_id : SV_DispatchThreadID, uint3 id_in_group : SV_GroupThreadID, uint index_in_group : SV_GroupIndex)
		{
				// 1024未満の要素数を許容する(遅くなるはずなので注意
		#if ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE
			uint buffLen = 0;
			uint buffStride = 0;
			array_uav.GetDimensions(buffLen, buffStride);
		#endif
	
	
		#if ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE
			if ( buffLen > dispatch_id.x )
		#endif
			{
				// 共有メモリに読み込み
				uint2 elem = array_uav[dispatch_id.x];
				key_group_memory[index_in_group] = elem.x;
				value_group_memory[index_in_group] = elem.y;
			}
			GroupMemoryBarrierWithGroupSync();
	
			uint order_flag = (0 != (cb_bitonic_sort_order_mask & dispatch_id.x));
	
			// 共有メモリ上でバイトニックソートフェーズ
			for (uint j = cb_bitonic_sort_block_size >> 1; j > 0; j >>= 1)
			{
				// オーダーマスクによってブロックごとに昇順降順を切り替えて入れ替え
				uint order = (key_group_memory[index_in_group & ~j] <= key_group_memory[index_in_group | j]);
				uint select_index = (order == order_flag) ? (index_in_group ^ j) : (index_in_group);
	
				uint2 result = uint2(key_group_memory[select_index], value_group_memory[select_index]);
	
	
	
				// 計算完了同期
				GroupMemoryBarrierWithGroupSync();
				// 書き込み
				key_group_memory[index_in_group] = result.x;
				value_group_memory[index_in_group] = result.y;
	
				// 書き込み同期
				GroupMemoryBarrierWithGroupSync();
			}
	
		#if ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE
			if ( buffLen > dispatch_id.x )
		#endif
			{
				// 書き戻し
				array_uav[dispatch_id.x] = uint2(key_group_memory[index_in_group], value_group_memory[index_in_group]);
			}
		}
	*/
	class FSimpleBitonicSortUint2 : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(FSimpleBitonicSortUint2, Global);
	public:
		static constexpr int CS_DISPATCH_THREAD_COUNT()
		{
			// 二の冪
			return 1024; // 通常は1024等で運用. 
		}

		static bool ShouldCache(EShaderPlatform Platform)
		{
			return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

			// ソートのグループスレッド数
			OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

			// ディスパッチサイズ以下のバッファサイズでも動作するようにチェック処理を有効化するマクロ. できればOFFにしておきたい.
			OutEnvironment.SetDefine(TEXT("ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE"), 1);

			// シェーダコード切り替えマクロ
			OutEnvironment.SetDefine(TEXT("CS_TILED_TRANSPOSE_BITONIC_SORT_CS"), 1);
		
		}

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			// Useful when adding a permutation of a particular shader
			return true;
		}

		FSimpleBitonicSortUint2() {}

		// シェーダパラメータと変数をバインド
		FSimpleBitonicSortUint2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			:FGlobalShader(Initializer)
		{
			// シェーダパラメータと変数をバインド
			cb_bitonic_sort_block_size_.Bind(Initializer.ParameterMap, TEXT("cb_bitonic_sort_block_size"));
			cb_bitonic_sort_order_mask_.Bind(Initializer.ParameterMap, TEXT("cb_bitonic_sort_order_mask"));

			array_uav_.Bind(Initializer.ParameterMap, TEXT("array_uav"));
		}

		void SetParameters(FRHICommandList& RHICmdList,
			uint32 cb_bitonic_sort_block_size,
			uint32 cb_bitonic_sort_order_mask,

			FRHIUnorderedAccessView* array_uav
		)
		{
			// 呼び出し直前にPipelineに自身が設定されているものとする.
			FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
			// UE5.3
			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
			SetShaderValue(BatchedParameters, cb_bitonic_sort_block_size_, cb_bitonic_sort_block_size);
			SetShaderValue(BatchedParameters, cb_bitonic_sort_order_mask_, cb_bitonic_sort_order_mask);

			// UAV
			SetUAVParameter(BatchedParameters, array_uav_, array_uav);
		
			RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_bitonic_sort_block_size_, cb_bitonic_sort_block_size);
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_bitonic_sort_order_mask_, cb_bitonic_sort_order_mask);
		
			// UAV
			SetUAVParameter(RHICmdList, ComputeShaderRHI, array_uav_, array_uav);
#endif
		}
		void ResetParameters(FRHICommandList& RHICmdList)
		{
			// 呼び出し直前にPipelineに自身が設定されているものとする.
			FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
			// UE5.3
			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
			// UAV
			SetUAVParameter(BatchedParameters, array_uav_, nullptr);
		
			RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
			// UAV
			SetUAVParameter(RHICmdList, ComputeShaderRHI, array_uav_, nullptr);
#endif
		}

		// bitonic sort用定数
		LAYOUT_FIELD(FShaderParameter , cb_bitonic_sort_block_size_);
		LAYOUT_FIELD(FShaderParameter , cb_bitonic_sort_order_mask_);

		// ソート対象バッファ
		LAYOUT_FIELD(FShaderResourceParameter , array_uav_);
	};
	// シェーダコードとGlobalShaderクラスを関連付け
	IMPLEMENT_SHADER_TYPE(, FSimpleBitonicSortUint2, TEXT("/Plugin/NagaExperiment/Private/compute_sort.usf"), TEXT("BitonicSort_Uint2"), SF_Compute)

	//
	// out-of-place Tiled Transpose
	/*
		// シェーダコード
		#ifndef TRANSPOSE_TILE_SIZE
		#define TRANSPOSE_TILE_SIZE 16
		#endif
		// ブロック単位転置で対象行列のサイズがブロックサイズの倍数ではない場合を許容する
		#ifndef ARROW_TRANSPOSE_MATRIX_NEQ_MULTIPLE_OF_TILE
		#define ARROW_TRANSPOSE_MATRIX_NEQ_MULTIPLE_OF_TILE 1
		#endif
	
		// タイルベースではない単純転置モード
		#ifndef NAIVE_MODE
		#define NAIVE_MODE 0
		#endif
	
		// ソート対象リソース
		RWStructuredBuffer<uint2> array_uav;
		StructuredBuffer<uint2> array_srv;
	
		// ---------------------------------------------------------------------------------------------------------------------------------
		// 定数バッファ
		uint cb_transpose_matrix_witdh;    // 転置対象の行列の幅
		uint cb_transpose_matrix_height;   // 転置対象の行列の高
	
		// 共有メモリ
		groupshared uint2 transpose_group_memory[TRANSPOSE_TILE_SIZE * TRANSPOSE_TILE_SIZE];
	
		// 転置. array_savからarray_uavへ出力. ディスパッチは ( cb_transpose_matrix_witdh, num/cb_transpose_matrix_witdh, 1 ) のように行列のサイズディスパッチ
		// 構造化バッファへの読み書きアクセスをなるべく連続メモリにするためブロック単位での転置処理をする.
		[numthreads(TRANSPOSE_TILE_SIZE, TRANSPOSE_TILE_SIZE, 1)]
		void Transpose_Uint2(uint3 dispatch_id : SV_DispatchThreadID, uint3 id_in_group : SV_GroupThreadID, uint index_in_group : SV_GroupIndex, uint3 group_id : SV_GroupID)
		{
			uint2 base_pos = group_id.xy * uint2(TRANSPOSE_TILE_SIZE, TRANSPOSE_TILE_SIZE);
			uint2 src_pos = base_pos + id_in_group.xy;
			uint2 dst_pos = base_pos.yx + id_in_group.yx;
	
			// 対象の縦横サイズがブロックサイズの倍数ぴったりではない場合の対応
			const bool is_enable_thread = (cb_transpose_matrix_witdh > src_pos.x) && (cb_transpose_matrix_height > src_pos.y);
	
		#if NAIVE_MODE
	
		#   if ARROW_TRANSPOSE_MATRIX_NEQ_MULTIPLE_OF_TILE
			if (is_enable_thread)
		#   endif
			{
				array_uav[dst_pos.y * cb_transpose_matrix_height + dst_pos.x] = array_srv[src_pos.y * cb_transpose_matrix_witdh + src_pos.x];
			}
	
		#else
	
		#   if ARROW_TRANSPOSE_MATRIX_NEQ_MULTIPLE_OF_TILE
			// グループ内スレッド番号が転置ブロック幅の中のスレッドだけ実行 あんまり効率的じゃないかもだけど
			if (is_enable_thread)
		#   endif
			{
				// ブロック単位で連続したアドレスから共有メモリへ読み込み
				transpose_group_memory[id_in_group.x * TRANSPOSE_TILE_SIZE + id_in_group.y] = array_srv[src_pos.y * cb_transpose_matrix_witdh + src_pos.x];
			}
	
			GroupMemoryBarrierWithGroupSync();
	
		#   if ARROW_TRANSPOSE_MATRIX_NEQ_MULTIPLE_OF_TILE
			if (is_enable_thread)
		#   endif
			{
				// 共有メモリから転置して読み取ってブロック単位で連続したアドレスで書き込み
				array_uav[dst_pos.y * cb_transpose_matrix_height + dst_pos.x] = transpose_group_memory[id_in_group.x * TRANSPOSE_TILE_SIZE + id_in_group.y];
			}
		#endif
		}
	*/
	class FOutOfPlaceTiledTransposeUint2 : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(FOutOfPlaceTiledTransposeUint2, Global);
	public:
		static constexpr int CS_TRANSPOSE_TILE_SIZE()
		{
			return 16;// 16や32で運用. Orbisでは16のほうが高速.
		}

		static bool ShouldCache(EShaderPlatform Platform)
		{
			return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

			// Transposeのブロックサイズ
			OutEnvironment.SetDefine(TEXT("TRANSPOSE_TILE_SIZE"), CS_TRANSPOSE_TILE_SIZE());

			// Transposeでブロックサイズの倍数サイズ以外の行列を許容する
			OutEnvironment.SetDefine(TEXT("ARROW_TRANSPOSE_MATRIX_NEQ_MULTIPLE_OF_TILE"), 1);

			// シェーダコード切り替えマクロ
			OutEnvironment.SetDefine(TEXT("CS_TILED_TRANSPOSE_CS"), 1);
		}

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			// Useful when adding a permutation of a particular shader
			return true;
		}

		FOutOfPlaceTiledTransposeUint2() {}

		// シェーダパラメータと変数をバインド
		FOutOfPlaceTiledTransposeUint2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			:FGlobalShader(Initializer)
		{
			// シェーダパラメータと変数をバインド
			cb_transpose_matrix_witdh_.Bind(Initializer.ParameterMap, TEXT("cb_transpose_matrix_witdh"));
			cb_transpose_matrix_height_.Bind(Initializer.ParameterMap, TEXT("cb_transpose_matrix_height"));

			array_uav_.Bind(Initializer.ParameterMap, TEXT("array_uav"));
			array_srv_.Bind(Initializer.ParameterMap, TEXT("array_srv"));
		}

		void SetParameters(FRHICommandList& RHICmdList,
			uint32 cb_transpose_matrix_witdh,
			uint32 cb_transpose_matrix_height,

			FRHIShaderResourceView* array_srv,
			FRHIUnorderedAccessView* array_uav
		)
		{
			// 呼び出し直前にPipelineに自身が設定されているものとする.
			FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
			// UE5.3
			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
			SetShaderValue(BatchedParameters, cb_transpose_matrix_witdh_, cb_transpose_matrix_witdh);
			SetShaderValue(BatchedParameters, cb_transpose_matrix_height_, cb_transpose_matrix_height);

			// SRV
			SetSRVParameter(BatchedParameters, array_srv_, array_srv);
			// UAV
			SetUAVParameter(BatchedParameters, array_uav_, array_uav);
		
			RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_transpose_matrix_witdh_, cb_transpose_matrix_witdh);
			SetShaderValue(RHICmdList, ComputeShaderRHI, cb_transpose_matrix_height_, cb_transpose_matrix_height);

			// SRV
			SetSRVParameter(RHICmdList, ComputeShaderRHI, array_srv_, array_srv);
			// UAV
			SetUAVParameter(RHICmdList, ComputeShaderRHI, array_uav_, array_uav);
#endif
		}
		void ResetParameters(FRHICommandList& RHICmdList)
		{
			// 呼び出し直前にPipelineに自身が設定されているものとする.
			FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
			// UE5.3
			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
			// SRV
			SetSRVParameter(BatchedParameters, array_srv_, nullptr);
			// UAV
			SetUAVParameter(BatchedParameters, array_uav_, nullptr);
		
			RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
			// SRV
			SetSRVParameter(RHICmdList, ComputeShaderRHI, array_srv_, nullptr);
			// UAV
			SetUAVParameter(RHICmdList, ComputeShaderRHI, array_uav_, nullptr);
#endif
		}

		// ブロック単位転置用定数
		LAYOUT_FIELD(FShaderParameter , cb_transpose_matrix_witdh_);
		LAYOUT_FIELD(FShaderParameter , cb_transpose_matrix_height_);

		// ソート対象バッファ
		LAYOUT_FIELD(FShaderResourceParameter , array_uav_);
		// 現状の転置シェーダは別バッファへ書き出しとなるためバッファを二つ用意してsrvとuavを指定する
		LAYOUT_FIELD(FShaderResourceParameter , array_srv_);
	};
	// シェーダコードとGlobalShaderクラスを関連付け
	IMPLEMENT_SHADER_TYPE(, FOutOfPlaceTiledTransposeUint2, TEXT("/Plugin/NagaExperiment/Private/compute_sort.usf"), TEXT("Transpose_Uint2"), SF_Compute)



	GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2::GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2()
	{
		// 初期化時ではなく実行時にサイズが足りなければ作り直すモード.
		is_work_buffer_init_jit_ = true;
	}
	GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2::~GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2()
	{
		// 解放
		transpose_work_buffer_.ReleaseResource();
	}

	// Call From GameThread.
	// max_num_element : 内部でディスパッチパラメータ用バッファを作成する必要があるため対応する最大の要素数を指定.
	bool GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2::Init(uint32 max_num_element)
	{
		if (!is_work_buffer_init_jit_)
		{
			// out-of-place Transposeのためのバッファを確保する.
			// 最大値は現在採用している転置付きBitonicSortのDispatchサイズの二乗まで. ここまでが共有メモリを利用して高速にソートできる限界.
			const auto ENTITY_MAX_LIMIT = FSimpleBitonicSortUint2::CS_DISPATCH_THREAD_COUNT() * FSimpleBitonicSortUint2::CS_DISPATCH_THREAD_COUNT();
			// 最小値はTiledTransposeのタイルサイズにしておく.
			const auto ENTITY_MIN_LIMIT = FOutOfPlaceTiledTransposeUint2::CS_TRANSPOSE_TILE_SIZE();

			// 指定された値以上の最小の二の冪
			const auto msb = math::Msb(FMath::Max(max_num_element, 2u) - 1u);
			const auto reserve_size = FMath::Clamp((0x01 << (msb + 1)), ENTITY_MIN_LIMIT, ENTITY_MAX_LIMIT);

			transpose_work_buffer_.Init(reserve_size, true, false);
			BeginInitResource(&transpose_work_buffer_);
		}
		return true;
	}
	// Call From RenderThread.
	void GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2::Run(FRHICommandListImmediate& cmdlist, ERHIFeatureLevel::Type FeatureLevel, FStructuredBufferResource<UintVec2>& buffer)
	{
		auto* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

		// ソートシェーダ
		TShaderMapRef<FSimpleBitonicSortUint2> sort_cs(GlobalShaderMap);
		// 転置シェーダ(out-of-place)
		TShaderMapRef<FOutOfPlaceTiledTransposeUint2> transpose_cs(GlobalShaderMap);


		const auto num_element = buffer.NumElement();

		// バッファサイズが二の冪かチェック
		if (!math::IsPowerOfTwo(num_element))
		{
			// 二の冪上でない場合はどうするか
			// 現状は何もせずに戻る
			UE_LOG(LogTemp, Log, TEXT("[%hs] Need Buffer Size Power of 2."), "GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2");
			return;
		}

		// 必要ならバッファ作り直し. この関数はRenderThread実行なのでOK.
		if (is_work_buffer_init_jit_)
		{
			if (num_element > transpose_work_buffer_.NumElement())
			{
				// RenderThreadなので直接RHI破棄
				transpose_work_buffer_.ReleaseRHI();

				// リソース初期化
				transpose_work_buffer_.Init(num_element, true, false);
				// RHI初期化
				transpose_work_buffer_.InitRHI(cmdlist);
			}
		}


		{
			cmdlist.PushEvent(*FString::Format(TEXT("BitonicSort"), { 0 }), FColor::Green);

			// ディスパッチサイズ以下のバイトニックソート. これ以上大きい場合はさらに転置バイトニックソートを続ける
			// シェーダセット
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
			// UE5.1から非推奨となった関数を置き換え.
			SetComputePipelineState(cmdlist, sort_cs.GetComputeShader());
#else
			cmdlist.SetComputeShader(sort_cs.GetComputeShader());
#endif

			// UAV ソート対象
			for (int i = 2; i <= num_element && i <= sort_cs->CS_DISPATCH_THREAD_COUNT(); i *= 2)
			{
				FString GpuEventName = FString::Format(TEXT("BitonicSort_{0}"), { i });
				cmdlist.PushEvent(*GpuEventName, FColor::Green);

				cmdlist.Transition(FRHITransitionInfo(buffer.GetUav(), buffer.GetCurrentRhiState(), buffer.TransitionRhiState(ERHIAccess::UAVCompute)));

				sort_cs->SetParameters(cmdlist, i, i, buffer.GetUav());

				// Dispatch.
				const int CsThreadCount = sort_cs->CS_DISPATCH_THREAD_COUNT();
				DispatchComputeShader(cmdlist, sort_cs, static_cast<unsigned int>((num_element + CsThreadCount - 1) / CsThreadCount), 1, 1);


				cmdlist.PopEvent();
			}
			// UAVリセット
			sort_cs->ResetParameters(cmdlist);

			// SortCSのスレッド数よりも大きい場合はそのままでは共有メモリ利用ができないため転置をしてから適用する
			const unsigned int matrix_w = sort_cs->CS_DISPATCH_THREAD_COUNT();
			const unsigned int matrix_h = num_element / sort_cs->CS_DISPATCH_THREAD_COUNT();
			for (int i = sort_cs->CS_DISPATCH_THREAD_COUNT() * 2; i <= num_element; i *= 2)
			{
				cmdlist.PushEvent(*FString::Format(TEXT("BitonicSort_{0}"), { i }), FColor::Green);

				// 転置
				{
					FString GpuEventName = FString::Format(TEXT("BitonicSort_Transpose_{0}"), { i });
					cmdlist.PushEvent(*GpuEventName, FColor::Green);

					cmdlist.Transition(FRHITransitionInfo(buffer.GetUav(), buffer.GetCurrentRhiState(), buffer.TransitionRhiState(ERHIAccess::SRVCompute)));
					cmdlist.Transition(FRHITransitionInfo(transpose_work_buffer_.GetUav(), transpose_work_buffer_.GetCurrentRhiState(), transpose_work_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));

					// シェーダセット
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
					// UE5.1から非推奨となった関数を置き換え.
					SetComputePipelineState(cmdlist, transpose_cs.GetComputeShader());
#else
					cmdlist.SetComputeShader(transpose_cs.GetComputeShader());
#endif

					transpose_cs->SetParameters(cmdlist, matrix_w, matrix_h, buffer.GetSrv(), transpose_work_buffer_.GetUav());

					// Dispatch.
					const int CsTransposeTileSize = transpose_cs->CS_TRANSPOSE_TILE_SIZE();
					const int CsGroupCountX = (matrix_w + CsTransposeTileSize - 1) / CsTransposeTileSize;
					const int CsGroupCountY = (matrix_h + CsTransposeTileSize - 1) / CsTransposeTileSize;
					DispatchComputeShader(cmdlist, transpose_cs, CsGroupCountX, CsGroupCountY, 1);

					// リセット
					transpose_cs->ResetParameters(cmdlist);


					cmdlist.Transition(FRHITransitionInfo(transpose_work_buffer_.GetUav(), transpose_work_buffer_.GetCurrentRhiState(), transpose_work_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));

					cmdlist.PopEvent();
				}

				// ソート
				{
					FString GpuEventName = FString::Format(TEXT("BitonicSort_{0}"), { i });
					cmdlist.PushEvent(*GpuEventName, FColor::Green);

					cmdlist.Transition(FRHITransitionInfo(transpose_work_buffer_.GetUav(), transpose_work_buffer_.GetCurrentRhiState(), transpose_work_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));

					// シェーダセット
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
					// UE5.1から非推奨となった関数を置き換え.
					SetComputePipelineState(cmdlist, sort_cs.GetComputeShader());
#else
					cmdlist.SetComputeShader(sort_cs.GetComputeShader());
#endif

					sort_cs->SetParameters(cmdlist, (i / sort_cs->CS_DISPATCH_THREAD_COUNT()), (i & ~(num_element)) / sort_cs->CS_DISPATCH_THREAD_COUNT(), transpose_work_buffer_.GetUav());

					// Dispatch.
					const int CsThreadCount = sort_cs->CS_DISPATCH_THREAD_COUNT();
					DispatchComputeShader(cmdlist, sort_cs, static_cast<unsigned int>((num_element + CsThreadCount - 1) / CsThreadCount), 1, 1);

					// UAVリセット
					sort_cs->ResetParameters(cmdlist);


					cmdlist.Transition(FRHITransitionInfo(transpose_work_buffer_.GetUav(), transpose_work_buffer_.GetCurrentRhiState(), transpose_work_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));

					cmdlist.PopEvent();
				}

				// 再度転置して戻す
				{
					FString GpuEventName = FString::Format(TEXT("BitonicSort_Transpose_{0}"), { i });
					cmdlist.PushEvent(*GpuEventName, FColor::Green);

					cmdlist.Transition(FRHITransitionInfo(buffer.GetUav(), buffer.GetCurrentRhiState(), buffer.TransitionRhiState(ERHIAccess::UAVCompute)));


					// シェーダセット
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
					// UE5.1から非推奨となった関数を置き換え.
					SetComputePipelineState(cmdlist, transpose_cs.GetComputeShader());
#else
					cmdlist.SetComputeShader(transpose_cs.GetComputeShader());
#endif

					// すでに転置されているのでwとhが逆
					transpose_cs->SetParameters(cmdlist, matrix_h, matrix_w, transpose_work_buffer_.GetSrv(), buffer.GetUav());

					// Dispatch.
					const int CsTransposeTileSize = transpose_cs->CS_TRANSPOSE_TILE_SIZE();
					// すでに転置されているのでwとhが逆
					const int CsGroupCountX = (matrix_h + CsTransposeTileSize - 1) / CsTransposeTileSize;
					const int CsGroupCountY = (matrix_w + CsTransposeTileSize - 1) / CsTransposeTileSize;
					DispatchComputeShader(cmdlist, transpose_cs, CsGroupCountX, CsGroupCountY, 1);

					// リセット
					transpose_cs->ResetParameters(cmdlist);

					cmdlist.Transition(FRHITransitionInfo(buffer.GetUav(), buffer.GetCurrentRhiState(), buffer.TransitionRhiState(ERHIAccess::SRVCompute)));

					cmdlist.PopEvent();
				}

				// 二度の転置で戻った状態でソート
				{
					FString GpuEventName = FString::Format(TEXT("BitonicSort_{0}"), { i });
					cmdlist.PushEvent(*GpuEventName, FColor::Green);

					cmdlist.Transition(FRHITransitionInfo(buffer.GetUav(), buffer.GetCurrentRhiState(), buffer.TransitionRhiState(ERHIAccess::UAVCompute)));

					// シェーダセット
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
					// UE5.1から非推奨となった関数を置き換え.
					SetComputePipelineState(cmdlist, sort_cs.GetComputeShader());
#else
					cmdlist.SetComputeShader(sort_cs.GetComputeShader());
#endif

					// 定数
					// UAV ソート対象
					sort_cs->SetParameters(cmdlist, sort_cs->CS_DISPATCH_THREAD_COUNT(), i, buffer.GetUav());

					// Dispatch.
					const int CsThreadCount = sort_cs->CS_DISPATCH_THREAD_COUNT();
					DispatchComputeShader(cmdlist, sort_cs, static_cast<unsigned int>((num_element + CsThreadCount - 1) / CsThreadCount), 1, 1);

					// UAVリセット
					sort_cs->ResetParameters(cmdlist);

					cmdlist.PopEvent();
				}
				cmdlist.PopEvent();
			}
			cmdlist.PopEvent();
		}
	}
}