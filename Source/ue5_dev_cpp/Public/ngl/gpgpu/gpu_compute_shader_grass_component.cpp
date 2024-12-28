// Fill out your copyright notice in the Description page of Project Settings.


#include "gpu_compute_shader_grass_component.h"

#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"

#include "PrimitiveSceneProxy.h"
#include "DynamicMeshBuilder.h"

#include "RHICommandList.h"

#include "PipelineStateCache.h"
// shader
#include "GlobalShader.h"
#include "RHIStaticStates.h"

#include "SceneUtils.h"
#include "SceneInterface.h"

#include "ngl/util/math_util.h"
#include "gpu_compute_shader_vertex_buffer.h"
#include "gpu_structured_buffer.h"
#include "gpu_sort.h"


#define NGL_REPLACE_SHADER_PARAM_METHOD 1

// プロシージャルメッシュのジオメトリをワールド空間扱いで処理する
// メッシュ描画時のマトリクスも単位行列になる
// ダイナミックシャドウの個別フラスタム計算用のバウンディングボックスをコンポーネント位置とは無関係にプレイヤー周辺をカバーするようなものにする(予定)
#define NGL_PROCEDURAL_MESH_USE_WORLD_SPACE_GEOMETRY 1

namespace ngl_procedual_mesh
{
	/** プロシージャルメッシュ用バッファ群 */
	class FNagaProceduralMeshBuffers
	{
	public:

		// RHI位置頂点バッファ
		ngl::gpgpu::FComputeShaderPositionVertexBufferT PositionVertexBuffer;

		// RHIカラー頂点バッファ
		ngl::gpgpu::FComputeShaderColorVertexBufferT ColorVertexBuffer;

		// RHIタンジェントノーマル頂点バッファ. デフォルトで4byte-PackedVector*2
		ngl::gpgpu::FComputeShaderTangentsVertexBufferT<> TangentsVertexBuffer;

		// RHIUV頂点バッファ
		ngl::gpgpu::FComputeShaderTexcoordVertexBufferT<> TexcoordVertexBuffer;

		// RHIインデックスバッファ
		ngl::gpgpu::FComputeShaderIndexBuffer32 IndexBuffer;

		// 頂点ファクトリ
		FLocalVertexFactory VertexFactory;

		FNagaProceduralMeshBuffers(ERHIFeatureLevel::Type InFeatureLevel)
			: VertexFactory(InFeatureLevel, "FNagaProceduralMeshBuffers")
		{}

		~FNagaProceduralMeshBuffers()
		{
			PositionVertexBuffer.ReleaseResource();
			TangentsVertexBuffer.ReleaseResource();
			TexcoordVertexBuffer.ReleaseResource();
			ColorVertexBuffer.ReleaseResource();
			IndexBuffer.ReleaseResource();
			VertexFactory.ReleaseResource();
		}
	};
}

namespace ngl::gpgpu
{
}

// ComputeGrassのエンティティ構造体
struct GrassEntityInfo
{
	FVector4f dir_len0	= FVector4f(0, 0, 1, 1);
	FVector4f pos0		= FVector4f(0, 0, 0, 0);
	FVector4f vel0		= FVector4f(0, 0, 0, 0);
	FVector4f pos1		= FVector4f(0, 0, 0, 0);
	FVector4f vel1		= FVector4f(0, 0, 0, 0);
	float life_sec		= 0.0f;
	float life_sec_init = 0.0f;

	uint32 pad[2];// パディング
};


// ComputeGrassのバッファについての各種カウント情報. uint32で構成する.
struct GrassEntityCountInfo
{
	// 無効要素の個数. ソートされたバッファの末尾に無効要素がこの数だけ配置されている.
	// エミッタ等はこのカウンタを参照して書き込み位置を決定し、必要なら加算する.
	// Atomicな加算はいろいろ準備が必要なので、現状は使わずにどれかのスレッドが代表して追加した要素数を加算する予定.
	// どこかのスレッドが追加をスキップしていたとしても最大値を加算する. 多い分には若干無駄ができるだけで破綻はせず、次のフレームのソートでまた適切にカウントが計算される.
	uint32	disable_count_;
};

// ComputeGrassのグリッドセル情報構造体
struct GridCellInfo
{
	FVector4f representative_pos = FVector4f(0, 0, 0, 0);
	FVector4f representative_vel = FVector4f(0, 0, 0, 0);
};




// Shader
class FComputeShaderGrassShader : public FGlobalShader
{
public:
	static constexpr int CS_DISPATCH_THREAD_COUNT()
	{
		return 1024;
	}

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// Useful when adding a permutation of a particular shader
		return true;
	}

	FComputeShaderGrassShader() {}

	// シェーダパラメータと変数をバインド
	FComputeShaderGrassShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FGlobalShader(Initializer)
	{
	}
};
class FComputeShaderGrassUpdateCS : public FComputeShaderGrassShader
{
	DECLARE_SHADER_TYPE(FComputeShaderGrassUpdateCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_COMPUTE_GRASS_UPDATE"), 1);
	}

	/** Default constructor. */
	FComputeShaderGrassUpdateCS() {}

	/** Initialization constructor. */
	FComputeShaderGrassUpdateCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGrassShader(Initializer)
	{
		// シェーダパラメータと変数をバインド
		cb_delta_sec.Bind(Initializer.ParameterMap, TEXT("cb_delta_sec"));

		cb_sim_space_aabb_min.Bind(Initializer.ParameterMap, TEXT("cb_sim_space_aabb_min"));
		cb_sim_space_aabb_max.Bind(Initializer.ParameterMap, TEXT("cb_sim_space_aabb_max"));
		cb_sim_space_cell_count.Bind(Initializer.ParameterMap, TEXT("cb_sim_space_cell_count"));
		cb_sim_space_cell_size_inv.Bind(Initializer.ParameterMap, TEXT("cb_sim_space_cell_size_inv"));



		// 後で構造化バッファにしたい定数バッファ
		cb_debug_obstacle_sphere_info_.Bind(Initializer.ParameterMap, TEXT("cb_debug_obstacle_sphere_info"));
		cb_debug_destruct_sphere_info_.Bind(Initializer.ParameterMap, TEXT("cb_debug_destruct_sphere_info"));

		// 構造化バッファ
		instance_uav_.Bind(Initializer.ParameterMap, TEXT("instance_uav"));
		hash_uav_.Bind(Initializer.ParameterMap, TEXT("hash_uav"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		float delta_sec,
		FVector3f sim_space_aabb_min,
		FVector3f sim_space_aabb_max,
		FIntVector sim_space_cell_count,
		float sim_space_cell_size_inv,
		FVector4f debug_obstacle_sphere_info,
		FVector4f debug_destruct_sphere_info,

		FRHIUnorderedAccessView* instance_uav,
		FRHIUnorderedAccessView* hash_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		SetShaderValue(BatchedParameters, cb_delta_sec, delta_sec);
		SetShaderValue(BatchedParameters, cb_sim_space_aabb_min, sim_space_aabb_min);
		SetShaderValue(BatchedParameters, cb_sim_space_aabb_max, sim_space_aabb_max);
		SetShaderValue(BatchedParameters, cb_sim_space_cell_count, sim_space_cell_count);
		SetShaderValue(BatchedParameters, cb_sim_space_cell_size_inv, sim_space_cell_size_inv);
		SetShaderValue(BatchedParameters, cb_debug_obstacle_sphere_info_, debug_obstacle_sphere_info);
		SetShaderValue(BatchedParameters, cb_debug_destruct_sphere_info_, debug_destruct_sphere_info);
		// UAV
		SetUAVParameter(BatchedParameters, instance_uav_, instance_uav);
		// 初期化でゼロクリアしているので最初のフレーム等で不定値が入っていて誤作動することはないと思うが注意.
		SetUAVParameter(BatchedParameters, hash_uav_, hash_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_delta_sec, delta_sec);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_sim_space_aabb_min, sim_space_aabb_min);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_sim_space_aabb_max, sim_space_aabb_max);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_sim_space_cell_count, sim_space_cell_count);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_sim_space_cell_size_inv, sim_space_cell_size_inv);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_debug_obstacle_sphere_info_, debug_obstacle_sphere_info);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_debug_destruct_sphere_info_, debug_destruct_sphere_info);
	
		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, instance_uav_, instance_uav);
		// 初期化でゼロクリアしているので最初のフレーム等で不定値が入っていて誤作動することはないと思うが注意.
		SetUAVParameter(RHICmdList, ComputeShaderRHI, hash_uav_, hash_uav);
#endif
	}
	void ResetParameters(FRHICommandList& RHICmdList)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// UAVリセット
		SetUAVParameter(BatchedParameters, instance_uav_, nullptr);
		SetUAVParameter(BatchedParameters, hash_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// UAVリセット
		SetUAVParameter(RHICmdList, ComputeShaderRHI, instance_uav_, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, hash_uav_, nullptr);
#endif
	}

	LAYOUT_FIELD(FShaderParameter , cb_delta_sec);
	// シミュレーション空間のAABB. 近傍探索構造の構築がこの範囲で処理される 
	LAYOUT_FIELD(FShaderParameter , cb_sim_space_aabb_min);
	LAYOUT_FIELD(FShaderParameter , cb_sim_space_aabb_max);
	// 各軸の空間分割数
	LAYOUT_FIELD(FShaderParameter , cb_sim_space_cell_count);
	// 各軸の空間分割数の逆数
	LAYOUT_FIELD(FShaderParameter , cb_sim_space_cell_size_inv);


	LAYOUT_FIELD(FShaderParameter , cb_debug_obstacle_sphere_info_);
	LAYOUT_FIELD(FShaderParameter , cb_debug_destruct_sphere_info_);

	// エンティティデータ
	LAYOUT_FIELD(FShaderResourceParameter , instance_uav_);
	LAYOUT_FIELD(FShaderResourceParameter , hash_uav_);
};

class FComputeShaderGrassUpdateWithGridCellCS : public FComputeShaderGrassShader
{
	DECLARE_SHADER_TYPE(FComputeShaderGrassUpdateWithGridCellCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_COMPUTE_GRASS_UPDATE_WITH_GRIDCELL"), 1);
	}

	/** Default constructor. */
	FComputeShaderGrassUpdateWithGridCellCS() {}

	/** Initialization constructor. */
	FComputeShaderGrassUpdateWithGridCellCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGrassShader(Initializer)
	{
		// シェーダパラメータと変数をバインド
		cb_delta_sec.Bind(Initializer.ParameterMap, TEXT("cb_delta_sec"));

		cb_sim_space_aabb_min.Bind(Initializer.ParameterMap, TEXT("cb_sim_space_aabb_min"));
		cb_sim_space_aabb_max.Bind(Initializer.ParameterMap, TEXT("cb_sim_space_aabb_max"));
		cb_sim_space_cell_count.Bind(Initializer.ParameterMap, TEXT("cb_sim_space_cell_count"));
		cb_sim_space_cell_size_inv.Bind(Initializer.ParameterMap, TEXT("cb_sim_space_cell_size_inv"));

		// 構造化バッファ
		instance_uav_.Bind(Initializer.ParameterMap, TEXT("instance_uav"));

		hash_srv_.Bind(Initializer.ParameterMap, TEXT("hash_srv"));
		hash_kind_scan_srv_.Bind(Initializer.ParameterMap, TEXT("hash_kind_scan_srv"));
		gridcell_data_srv_.Bind(Initializer.ParameterMap, TEXT("gridcell_data_srv"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		float delta_sec,
		FVector3f sim_space_aabb_min,
		FVector3f sim_space_aabb_max,
		FIntVector sim_space_cell_count,
		float sim_space_cell_size_inv,

		FRHIShaderResourceView* hash_srv,
		FRHIShaderResourceView* hash_kind_scan_srv,
		FRHIShaderResourceView* gridcell_data_srv,
		FRHIUnorderedAccessView* instance_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		SetShaderValue(BatchedParameters, cb_delta_sec, delta_sec);
		SetShaderValue(BatchedParameters, cb_sim_space_aabb_min, sim_space_aabb_min);
		SetShaderValue(BatchedParameters, cb_sim_space_aabb_max, sim_space_aabb_max);
		SetShaderValue(BatchedParameters, cb_sim_space_cell_count, sim_space_cell_count);
		SetShaderValue(BatchedParameters, cb_sim_space_cell_size_inv, sim_space_cell_size_inv);

		// SRV
		SetSRVParameter(BatchedParameters, hash_srv_, hash_srv);
		SetSRVParameter(BatchedParameters, hash_kind_scan_srv_, hash_kind_scan_srv);
		SetSRVParameter(BatchedParameters, gridcell_data_srv_, gridcell_data_srv);

		// UAV
		SetUAVParameter(BatchedParameters, instance_uav_, instance_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_delta_sec, delta_sec);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_sim_space_aabb_min, sim_space_aabb_min);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_sim_space_aabb_max, sim_space_aabb_max);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_sim_space_cell_count, sim_space_cell_count);
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_sim_space_cell_size_inv, sim_space_cell_size_inv);

		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv_, hash_srv);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_kind_scan_srv_, hash_kind_scan_srv);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, gridcell_data_srv_, gridcell_data_srv);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, instance_uav_, instance_uav);
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
		SetSRVParameter(BatchedParameters, hash_srv_, nullptr);
		SetSRVParameter(BatchedParameters, hash_kind_scan_srv_, nullptr);
		SetSRVParameter(BatchedParameters, gridcell_data_srv_, nullptr);

		// UAV
		SetUAVParameter(BatchedParameters, instance_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv_, nullptr);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_kind_scan_srv_, nullptr);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, gridcell_data_srv_, nullptr);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, instance_uav_, nullptr);
#endif
	}

	LAYOUT_FIELD(FShaderParameter , cb_delta_sec);

	// シミュレーション空間のAABB. 近傍探索構造の構築がこの範囲で処理される 
	LAYOUT_FIELD(FShaderParameter , cb_sim_space_aabb_min);
	LAYOUT_FIELD(FShaderParameter , cb_sim_space_aabb_max);
	// 各軸の空間分割数
	LAYOUT_FIELD(FShaderParameter , cb_sim_space_cell_count);
	// 各軸の空間分割数の逆数
	LAYOUT_FIELD(FShaderParameter , cb_sim_space_cell_size_inv);

	LAYOUT_FIELD(FShaderResourceParameter , hash_srv_);
	LAYOUT_FIELD(FShaderResourceParameter , hash_kind_scan_srv_);
	LAYOUT_FIELD(FShaderResourceParameter , gridcell_data_srv_);
	LAYOUT_FIELD(FShaderResourceParameter , instance_uav_);
};


class FComputeShaderGrassGenerateMeshCS : public FComputeShaderGrassShader
{
	DECLARE_SHADER_TYPE(FComputeShaderGrassGenerateMeshCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_COMPUTE_GRASS_GEN_MESH"), 1);
	}

	/** Default constructor. */
	FComputeShaderGrassGenerateMeshCS() {}

	/** Initialization constructor. */
	FComputeShaderGrassGenerateMeshCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGrassShader(Initializer)
	{
		cb_entity_mesh_info_.Bind(Initializer.ParameterMap, TEXT("cb_entity_mesh_info"));

		instance_srv_.Bind(Initializer.ParameterMap, TEXT("instance_srv"));
		hash_srv_.Bind(Initializer.ParameterMap, TEXT("hash_srv"));
		// 書き換え対象頂点バッファ
		vtx_pos_buffer_.Bind(Initializer.ParameterMap, TEXT("vtx_pos_buffer"));
		vtx_color_buffer_.Bind(Initializer.ParameterMap, TEXT("vtx_color_buffer"));
		vtx_uv_buffer_.Bind(Initializer.ParameterMap, TEXT("vtx_uv_buffer"));
		vtx_tangent_space_buffer_.Bind(Initializer.ParameterMap, TEXT("vtx_tangent_space_buffer"));
		index_buffer_.Bind(Initializer.ParameterMap, TEXT("index_buffer"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FUintVector4 entity_mesh_info,

		FRHIShaderResourceView* hash_srv,
		FRHIShaderResourceView* instance_srv,

		FRHIUnorderedAccessView* vtx_pos_buffer,
		FRHIUnorderedAccessView* vtx_color_buffer,
		FRHIUnorderedAccessView* vtx_uv_buffer,
		FRHIUnorderedAccessView* vtx_tangent_space_buffer,
		FRHIUnorderedAccessView* index_buffer
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		SetShaderValue(BatchedParameters, cb_entity_mesh_info_, entity_mesh_info);

		// SRV
		SetSRVParameter(BatchedParameters, instance_srv_, instance_srv);
		SetSRVParameter(BatchedParameters, hash_srv_, hash_srv);

		// UAV
		SetUAVParameter(BatchedParameters, vtx_pos_buffer_, vtx_pos_buffer);
		SetUAVParameter(BatchedParameters, vtx_color_buffer_, vtx_color_buffer);
		SetUAVParameter(BatchedParameters, vtx_uv_buffer_, vtx_uv_buffer);
		SetUAVParameter(BatchedParameters, vtx_tangent_space_buffer_, vtx_tangent_space_buffer);
		SetUAVParameter(BatchedParameters, index_buffer_, index_buffer);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_entity_mesh_info_, entity_mesh_info);

		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, instance_srv_, instance_srv);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv_, hash_srv);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, vtx_pos_buffer_, vtx_pos_buffer);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, vtx_color_buffer_, vtx_color_buffer);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, vtx_uv_buffer_, vtx_uv_buffer);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, vtx_tangent_space_buffer_, vtx_tangent_space_buffer);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, index_buffer_, index_buffer);
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
		SetSRVParameter(BatchedParameters, instance_srv_, nullptr);
		SetSRVParameter(BatchedParameters, hash_srv_, nullptr);

		// UAV
		SetUAVParameter(BatchedParameters, vtx_pos_buffer_, nullptr);
		SetUAVParameter(BatchedParameters, vtx_color_buffer_, nullptr);
		SetUAVParameter(BatchedParameters, vtx_uv_buffer_, nullptr);
		SetUAVParameter(BatchedParameters, vtx_tangent_space_buffer_, nullptr);
		SetUAVParameter(BatchedParameters, index_buffer_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, instance_srv_, nullptr);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv_, nullptr);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, vtx_pos_buffer_, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, vtx_color_buffer_, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, vtx_uv_buffer_, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, vtx_tangent_space_buffer_, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, index_buffer_, nullptr);
#endif
	}

	LAYOUT_FIELD(FShaderParameter , cb_entity_mesh_info_);

	LAYOUT_FIELD(FShaderResourceParameter , instance_srv_);
	LAYOUT_FIELD(FShaderResourceParameter , hash_srv_);
	// 頂点バッファ
	LAYOUT_FIELD(FShaderResourceParameter , vtx_pos_buffer_);
	LAYOUT_FIELD(FShaderResourceParameter , vtx_color_buffer_);
	LAYOUT_FIELD(FShaderResourceParameter , vtx_uv_buffer_);
	LAYOUT_FIELD(FShaderResourceParameter , vtx_tangent_space_buffer_);
	LAYOUT_FIELD(FShaderResourceParameter , index_buffer_);
};
class FComputeShaderGrassGenerateCountCS : public FComputeShaderGrassShader
{
	DECLARE_SHADER_TYPE(FComputeShaderGrassGenerateCountCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());// このシェーダは1スレッド固定なので意味なし.

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_COMPUTE_GRASS_GEN_COUNT"), 1);
	}

	/** Default constructor. */
	FComputeShaderGrassGenerateCountCS() {}

	/** Initialization constructor. */
	FComputeShaderGrassGenerateCountCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGrassShader(Initializer)
	{
		hash_srv_.Bind(Initializer.ParameterMap, TEXT("hash_srv"));
		hash_msb_start_index_32_srv_.Bind(Initializer.ParameterMap, TEXT("hash_msb_start_index_32_srv"));

		entity_count_info_uav_.Bind(Initializer.ParameterMap, TEXT("entity_count_info_uav"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FRHIShaderResourceView* hash_srv,
		FRHIShaderResourceView* hash_msb_start_index_32_srv,

		FRHIUnorderedAccessView* entity_count_info_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// SRV
		SetSRVParameter(BatchedParameters, hash_srv_, hash_srv);
		SetSRVParameter(BatchedParameters, hash_msb_start_index_32_srv_, hash_msb_start_index_32_srv);

		// UAV
		SetUAVParameter(BatchedParameters, entity_count_info_uav_, entity_count_info_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv_, hash_srv);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_msb_start_index_32_srv_, hash_msb_start_index_32_srv);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, entity_count_info_uav_, entity_count_info_uav);
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
		SetSRVParameter(BatchedParameters, hash_srv_, nullptr);
		SetSRVParameter(BatchedParameters, hash_msb_start_index_32_srv_, nullptr);

		// UAV
		SetUAVParameter(BatchedParameters, entity_count_info_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv_, nullptr);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_msb_start_index_32_srv_, nullptr);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, entity_count_info_uav_, nullptr);
#endif
	}

	LAYOUT_FIELD(FShaderResourceParameter , hash_srv_);
	LAYOUT_FIELD(FShaderResourceParameter , hash_msb_start_index_32_srv_);

	LAYOUT_FIELD(FShaderResourceParameter , entity_count_info_uav_);
};

class FComputeShaderGrassAppendDirectCS : public FComputeShaderGrassShader
{
	DECLARE_SHADER_TYPE(FComputeShaderGrassAppendDirectCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_COMPUTE_GRASS_APPEND_DIRECT"), 1);
	}

	/** Default constructor. */
	FComputeShaderGrassAppendDirectCS() {}

	/** Initialization constructor. */
	FComputeShaderGrassAppendDirectCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGrassShader(Initializer)
	{
		// シェーダパラメータと変数をバインド
		cb_append_instance_count.Bind(Initializer.ParameterMap, TEXT("cb_append_instance_count"));

		// 構造化バッファ
		append_instance_srv.Bind(Initializer.ParameterMap, TEXT("append_instance_srv"));
		instance_uav_.Bind(Initializer.ParameterMap, TEXT("instance_uav"));
		entity_count_info_uav.Bind(Initializer.ParameterMap, TEXT("entity_count_info_uav"));
		hash_srv.Bind(Initializer.ParameterMap, TEXT("hash_srv"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FRHIShaderResourceView* sorted_hash_buffer,

		int append_count,
		FRHIShaderResourceView* append_info_buffer,

		FRHIUnorderedAccessView* entity_base_info_buffer,
		FRHIUnorderedAccessView* sorted_entity_counter_buffer
		)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// SRV
		SetSRVParameter(BatchedParameters, hash_srv, sorted_hash_buffer);
		// 追加個数定数バッファ
		SetShaderValue(BatchedParameters, cb_append_instance_count, append_count);
		// SRV 追加用パラメータ
		SetSRVParameter(BatchedParameters, append_instance_srv, append_info_buffer);
		// UAV
		SetUAVParameter(BatchedParameters, instance_uav_, entity_base_info_buffer);
		// 初期化でゼロクリアしているので最初のフレーム等で不定値が入っていて誤作動することはないと思うが注意.
		SetUAVParameter(BatchedParameters, entity_count_info_uav, sorted_entity_counter_buffer);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv, sorted_hash_buffer);
		// 追加個数定数バッファ
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_append_instance_count, append_count);
		// SRV 追加用パラメータ
		SetSRVParameter(RHICmdList, ComputeShaderRHI, append_instance_srv, append_info_buffer);
		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, instance_uav_, entity_base_info_buffer);
		// 初期化でゼロクリアしているので最初のフレーム等で不定値が入っていて誤作動することはないと思うが注意.
		SetUAVParameter(RHICmdList, ComputeShaderRHI, entity_count_info_uav, sorted_entity_counter_buffer);
#endif
	}
	void ResetParameters(FRHICommandList& RHICmdList)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// SRVリセット
		SetSRVParameter(BatchedParameters, append_instance_srv, nullptr);
		SetSRVParameter(BatchedParameters, hash_srv, nullptr);
		// UAVリセット
		SetUAVParameter(BatchedParameters, instance_uav_, nullptr);
		SetUAVParameter(BatchedParameters, entity_count_info_uav, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRVリセット
		SetSRVParameter(RHICmdList, ComputeShaderRHI, append_instance_srv, nullptr);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv, nullptr);
		// UAVリセット
		SetUAVParameter(RHICmdList, ComputeShaderRHI, instance_uav_, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, entity_count_info_uav, nullptr);
#endif
	}

	LAYOUT_FIELD(FShaderParameter , cb_append_instance_count);
	LAYOUT_FIELD(FShaderResourceParameter , append_instance_srv);

	LAYOUT_FIELD(FShaderResourceParameter , hash_srv);
	LAYOUT_FIELD(FShaderResourceParameter , instance_uav_);
	LAYOUT_FIELD(FShaderResourceParameter , entity_count_info_uav);
};

class FComputeShaderGrassGatherToGridCellCS : public FComputeShaderGrassShader
{
	DECLARE_SHADER_TYPE(FComputeShaderGrassGatherToGridCellCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_COMPUTE_GRASS_GATHER_TO_GRIDCELL"), 1);
	}

	/** Default constructor. */
	FComputeShaderGrassGatherToGridCellCS() {}

	/** Initialization constructor. */
	FComputeShaderGrassGatherToGridCellCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGrassShader(Initializer)
	{
		// 構造化バッファ
		hash_srv_.Bind(Initializer.ParameterMap, TEXT("hash_srv"));
		instance_srv_.Bind(Initializer.ParameterMap, TEXT("instance_srv"));
		enable_hash_info_srv_.Bind(Initializer.ParameterMap, TEXT("enable_hash_info_srv"));
		gridcell_data_uav_.Bind(Initializer.ParameterMap, TEXT("gridcell_data_uav"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FRHIShaderResourceView* hash_srv,
		FRHIShaderResourceView* instance_srv,
		FRHIShaderResourceView* enable_hash_info_srv,

		FRHIUnorderedAccessView* gridcell_data_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// SRV
		SetSRVParameter(BatchedParameters, hash_srv_, hash_srv);
		SetSRVParameter(BatchedParameters, instance_srv_, instance_srv);
		SetSRVParameter(BatchedParameters, enable_hash_info_srv_, enable_hash_info_srv);

		// UAV
		SetUAVParameter(BatchedParameters, gridcell_data_uav_, gridcell_data_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv_, hash_srv);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, instance_srv_, instance_srv);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, enable_hash_info_srv_, enable_hash_info_srv);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, gridcell_data_uav_, gridcell_data_uav);
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
		SetSRVParameter(BatchedParameters, hash_srv_, nullptr);
		SetSRVParameter(BatchedParameters, instance_srv_, nullptr);
		SetSRVParameter(BatchedParameters, enable_hash_info_srv_, nullptr);

		// UAV
		SetUAVParameter(BatchedParameters, gridcell_data_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_srv_, nullptr);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, instance_srv_, nullptr);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, enable_hash_info_srv_, nullptr);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, gridcell_data_uav_, nullptr);
#endif
	}

	LAYOUT_FIELD(FShaderResourceParameter , hash_srv_);
	LAYOUT_FIELD(FShaderResourceParameter , instance_srv_);
	LAYOUT_FIELD(FShaderResourceParameter , enable_hash_info_srv_);

	LAYOUT_FIELD(FShaderResourceParameter , gridcell_data_uav_);
};
// シェーダコードとGlobalShaderクラスを関連付け
IMPLEMENT_SHADER_TYPE(, FComputeShaderGrassUpdateCS, TEXT("/Plugin/NagaExperiment/Private/compute_grass.usf"), TEXT("UpdateInstanceCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FComputeShaderGrassUpdateWithGridCellCS, TEXT("/Plugin/NagaExperiment/Private/compute_grass.usf"), TEXT("UpdateInstanceWithGridCellCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FComputeShaderGrassGenerateMeshCS, TEXT("/Plugin/NagaExperiment/Private/compute_grass.usf"), TEXT("GenerateMeshCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FComputeShaderGrassGenerateCountCS, TEXT("/Plugin/NagaExperiment/Private/compute_grass.usf"), TEXT("GenerateEntityCountInfoCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FComputeShaderGrassAppendDirectCS, TEXT("/Plugin/NagaExperiment/Private/compute_grass.usf"), TEXT("AppendDirectInstanceCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FComputeShaderGrassGatherToGridCellCS, TEXT("/Plugin/NagaExperiment/Private/compute_grass.usf"), TEXT("GatherToGridCellCS"), SF_Compute)


// ソート済みバッファから情報を取得するShader
class FComputeShaderGenSortedBufferInfoShader : public FGlobalShader
{
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

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// Useful when adding a permutation of a particular shader
		return true;
	}

	FComputeShaderGenSortedBufferInfoShader() {}

	// シェーダパラメータと変数をバインド
	FComputeShaderGenSortedBufferInfoShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FGlobalShader(Initializer)
	{
	}
};

// ソート済みバッファKeyのMSB開始インデックスバッファをクリア
class FClearSortedBufferInfoCS : public FComputeShaderGenSortedBufferInfoShader
{
	DECLARE_SHADER_TYPE(FClearSortedBufferInfoCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// ソートのグループスレッド数
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// ディスパッチサイズ以下のバッファサイズでも動作するようにチェック処理を有効化するマクロ. できればOFFにしておきたい.
		OutEnvironment.SetDefine(TEXT("ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE"), 1);

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_CLEAR_SORTED_BUFFER_INFO"), 1);
	}

	/** Default constructor. */
	FClearSortedBufferInfoCS() {}

	/** Initialization constructor. */
	FClearSortedBufferInfoCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGenSortedBufferInfoShader(Initializer)
	{
		hash_msb_start_index_32_uav_.Bind(Initializer.ParameterMap, TEXT("hash_msb_start_index_32_uav"));
		cb_clear_value_.Bind(Initializer.ParameterMap, TEXT("cb_clear_value"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		uint32 clear_value,

		FRHIUnorderedAccessView* hash_msb_start_index_32_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// 追加個数定数バッファ
		SetShaderValue(BatchedParameters, cb_clear_value_, clear_value);
		// UAV
		SetUAVParameter(BatchedParameters, hash_msb_start_index_32_uav_, hash_msb_start_index_32_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// 追加個数定数バッファ
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_clear_value_, clear_value);
		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, hash_msb_start_index_32_uav_, hash_msb_start_index_32_uav);
#endif
	}
	void ResetParameters(FRHICommandList& RHICmdList)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// UAVリセット
		SetUAVParameter(BatchedParameters, hash_msb_start_index_32_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// UAVリセット
		SetUAVParameter(RHICmdList, ComputeShaderRHI, hash_msb_start_index_32_uav_, nullptr);
#endif
	}

	// 32要素のソート済みバッファ情報UAV
	LAYOUT_FIELD(FShaderResourceParameter , hash_msb_start_index_32_uav_);
	LAYOUT_FIELD(FShaderParameter , cb_clear_value_);
};

class FClearUint2BufferCS : public FComputeShaderGenSortedBufferInfoShader
{
	DECLARE_SHADER_TYPE(FClearUint2BufferCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// ソートのグループスレッド数
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// ディスパッチサイズ以下のバッファサイズでも動作するようにチェック処理を有効化するマクロ. できればOFFにしておきたい.
		OutEnvironment.SetDefine(TEXT("ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE"), 1);

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_CLEAR_UINT2_BUFFER"), 1);
	}

	/** Default constructor. */
	FClearUint2BufferCS() {}

	/** Initialization constructor. */
	FClearUint2BufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGenSortedBufferInfoShader(Initializer)
	{
		out_uav_.Bind(Initializer.ParameterMap, TEXT("out_uav"));
		cb_clear_value_.Bind(Initializer.ParameterMap, TEXT("cb_clear_value"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FUintVector2 clear_value,

		FRHIUnorderedAccessView* uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// 追加個数定数バッファ
		SetShaderValue(BatchedParameters, cb_clear_value_, clear_value);

		// UAV
		SetUAVParameter(BatchedParameters, out_uav_, uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// 追加個数定数バッファ
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_clear_value_, clear_value);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, out_uav_, uav);
#endif
	}
	void ResetParameters(FRHICommandList& RHICmdList)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// UAVリセット
		SetUAVParameter(BatchedParameters, out_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// UAVリセット
		SetUAVParameter(RHICmdList, ComputeShaderRHI, out_uav_, nullptr);
#endif
	}

	// 32要素のソート済みバッファ情報UAV
	LAYOUT_FIELD(FShaderResourceParameter , out_uav_);
	LAYOUT_FIELD(FShaderParameter , cb_clear_value_);
};

// ソート済みバッファHashのMSB開始インデックスバッファを生成
class FGenSortedBufferInfoUint2CS : public FComputeShaderGenSortedBufferInfoShader
{
	DECLARE_SHADER_TYPE(FGenSortedBufferInfoUint2CS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// ソートのグループスレッド数
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// ディスパッチサイズ以下のバッファサイズでも動作するようにチェック処理を有効化するマクロ. できればOFFにしておきたい.
		OutEnvironment.SetDefine(TEXT("ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE"), 1);

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_GEN_SORTED_BUFFER_INFO"), 1);
	}

	/** Default constructor. */
	FGenSortedBufferInfoUint2CS() {}

	/** Initialization constructor. */
	FGenSortedBufferInfoUint2CS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGenSortedBufferInfoShader(Initializer)
	{
		hash_msb_start_index_32_uav_.Bind(Initializer.ParameterMap, TEXT("hash_msb_start_index_32_uav"));
		array_srv_.Bind(Initializer.ParameterMap, TEXT("array_srv"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FRHIShaderResourceView* array_srv,
		FRHIUnorderedAccessView* hash_msb_start_index_32_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// SRV
		SetSRVParameter(BatchedParameters, array_srv_, array_srv);

		// UAV
		SetUAVParameter(BatchedParameters, hash_msb_start_index_32_uav_, hash_msb_start_index_32_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, array_srv_, array_srv);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, hash_msb_start_index_32_uav_, hash_msb_start_index_32_uav);
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
		SetUAVParameter(BatchedParameters, hash_msb_start_index_32_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, array_srv_, nullptr);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, hash_msb_start_index_32_uav_, nullptr);
#endif
	}

	// ソート済みバッファSRV
	LAYOUT_FIELD(FShaderResourceParameter , array_srv_);
	// 32要素のソート済みバッファ情報UAV
	LAYOUT_FIELD(FShaderResourceParameter , hash_msb_start_index_32_uav_);
};

// ソート済みバッファHashのインデックス変化位置バッファを生成
class FGenSortedBufferKeyDiffUint2CS : public FComputeShaderGenSortedBufferInfoShader
{
	DECLARE_SHADER_TYPE(FGenSortedBufferKeyDiffUint2CS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// ソートのグループスレッド数
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// ディスパッチサイズ以下のバッファサイズでも動作するようにチェック処理を有効化するマクロ. できればOFFにしておきたい.
		OutEnvironment.SetDefine(TEXT("ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE"), 1);

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_GEN_SORTED_BUFFER_KEY_DIFF"), 1);
	}

	/** Default constructor. */
	FGenSortedBufferKeyDiffUint2CS() {}

	/** Initialization constructor. */
	FGenSortedBufferKeyDiffUint2CS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGenSortedBufferInfoShader(Initializer)
	{
		out_uav_.Bind(Initializer.ParameterMap, TEXT("out_uav"));
		array_srv_.Bind(Initializer.ParameterMap, TEXT("array_srv"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FRHIShaderResourceView* array_srv,
		FRHIUnorderedAccessView* out_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// SRV
		SetSRVParameter(BatchedParameters, array_srv_, array_srv);

		// UAV
		SetUAVParameter(BatchedParameters, out_uav_, out_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, array_srv_, array_srv);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, out_uav_, out_uav);
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
		SetUAVParameter(BatchedParameters, out_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, array_srv_, nullptr);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, out_uav_, nullptr);
#endif
	}

	// ソート済みバッファSRV
	LAYOUT_FIELD(FShaderResourceParameter , array_srv_);
	// ソート済みバッファのハッシュの差分バッファ
	LAYOUT_FIELD(FShaderResourceParameter , out_uav_);
};

// スレッドグループ*2要素単位のExclusiveScan
class FExclusiveScanPerGroupUintCS : public FComputeShaderGenSortedBufferInfoShader
{
	DECLARE_SHADER_TYPE(FExclusiveScanPerGroupUintCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// ソートのグループスレッド数. 1グループはこの2倍の要素をScanする
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// ディスパッチサイズ以下のバッファサイズでも動作するようにチェック処理を有効化するマクロ. できればOFFにしておきたい.
		OutEnvironment.SetDefine(TEXT("ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE"), 1);

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_EXCLUSIVE_SCAN_PER_GROUP"), 1);
	}

	/** Default constructor. */
	FExclusiveScanPerGroupUintCS() {}

	/** Initialization constructor. */
	FExclusiveScanPerGroupUintCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGenSortedBufferInfoShader(Initializer)
	{
		scan_per_group_uav_.Bind(Initializer.ParameterMap, TEXT("scan_per_group_uav"));
		block_sums_uav_.Bind(Initializer.ParameterMap, TEXT("block_sums_uav"));
		array_srv_.Bind(Initializer.ParameterMap, TEXT("array_srv"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FRHIShaderResourceView* array_srv,

		FRHIUnorderedAccessView* scan_per_group_uav,
		FRHIUnorderedAccessView* block_sums_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// SRV
		SetSRVParameter(BatchedParameters, array_srv_, array_srv);

		// UAV
		SetUAVParameter(BatchedParameters, scan_per_group_uav_, scan_per_group_uav);
		SetUAVParameter(BatchedParameters, block_sums_uav_, block_sums_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, array_srv_, array_srv);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, scan_per_group_uav_, scan_per_group_uav);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, block_sums_uav_, block_sums_uav);
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
		SetUAVParameter(BatchedParameters, scan_per_group_uav_, nullptr);
		SetUAVParameter(BatchedParameters, block_sums_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, array_srv_, nullptr);

		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, scan_per_group_uav_, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, block_sums_uav_, nullptr);
#endif
	}

	// ソート済みバッファSRV
	LAYOUT_FIELD(FShaderResourceParameter , array_srv_);
	// ソート済みバッファのハッシュの差分バッファ
	LAYOUT_FIELD(FShaderResourceParameter , scan_per_group_uav_);
	LAYOUT_FIELD(FShaderResourceParameter , block_sums_uav_);
};

// ブロック単位ExclusiveScanから完全なExclusiveScanを生成
class FCompleteBlockExclusiveScanUintCS : public FComputeShaderGenSortedBufferInfoShader
{
	DECLARE_SHADER_TYPE(FCompleteBlockExclusiveScanUintCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// ソートのグループスレッド数. 1グループはこの2倍の要素をScanする
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// ディスパッチサイズ以下のバッファサイズでも動作するようにチェック処理を有効化するマクロ. できればOFFにしておきたい.
		OutEnvironment.SetDefine(TEXT("ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE"), 1);

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_COMPLETE_BLOCK_EXCLUSIVE_SCAN"), 1);
	}

	/** Default constructor. */
	FCompleteBlockExclusiveScanUintCS() {}

	/** Initialization constructor. */
	FCompleteBlockExclusiveScanUintCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGenSortedBufferInfoShader(Initializer)
	{
		array_uav_.Bind(Initializer.ParameterMap, TEXT("array_uav"));
		upper_scan_srv_.Bind(Initializer.ParameterMap, TEXT("upper_scan_srv"));
		cb_block_size_.Bind(Initializer.ParameterMap, TEXT("cb_block_size"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		uint32 block_size,

		FRHIShaderResourceView* upper_scan_srv,

		FRHIUnorderedAccessView* array_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// 追加個数定数バッファ
		SetShaderValue(BatchedParameters, cb_block_size_, block_size);

		// SRV
		SetSRVParameter(BatchedParameters, upper_scan_srv_, upper_scan_srv);
		// SRV 追加用パラメータ
		SetUAVParameter(BatchedParameters, array_uav_, array_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// 追加個数定数バッファ
		SetShaderValue(RHICmdList, ComputeShaderRHI, cb_block_size_, block_size);

		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, upper_scan_srv_, upper_scan_srv);
		// SRV 追加用パラメータ
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
		SetSRVParameter(BatchedParameters, upper_scan_srv_, nullptr);
		// SRV 追加用パラメータ
		SetUAVParameter(BatchedParameters, array_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, upper_scan_srv_, nullptr);
		// SRV 追加用パラメータ
		SetUAVParameter(RHICmdList, ComputeShaderRHI, array_uav_, nullptr);
#endif
	}

	// ソート済みバッファSRV
	LAYOUT_FIELD(FShaderResourceParameter , upper_scan_srv_);
	// ソート済みバッファのハッシュの差分バッファ
	LAYOUT_FIELD(FShaderResourceParameter , array_uav_);
	// 定数バッファ
	// ブロックサイズ
	LAYOUT_FIELD(FShaderParameter , cb_block_size_);
};

// 有効ハッシュ情報バッファ生成
class FGenEnableHashInfoBufferCS : public FComputeShaderGenSortedBufferInfoShader
{
	DECLARE_SHADER_TYPE(FGenEnableHashInfoBufferCS, Global);

public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// ソートのグループスレッド数
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());

		// ディスパッチサイズ以下のバッファサイズでも動作するようにチェック処理を有効化するマクロ. できればOFFにしておきたい.
		OutEnvironment.SetDefine(TEXT("ARROW_BUFFER_SIZE_LESS_THAN_DISPATCH_THREAD_SIZE"), 1);

		// シェーダ切り替えマクロ指定
		OutEnvironment.SetDefine(TEXT("CS_GEN_ENABLE_HASH_BUFFER"), 1);
	}

	/** Default constructor. */
	FGenEnableHashInfoBufferCS() {}

	/** Initialization constructor. */
	FGenEnableHashInfoBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FComputeShaderGenSortedBufferInfoShader(Initializer)
	{
		hash_scan_srv_.Bind(Initializer.ParameterMap, TEXT("hash_scan_srv"));
		hash_id_srv_.Bind(Initializer.ParameterMap, TEXT("hash_id_srv"));
		enable_hash_info_uav_.Bind(Initializer.ParameterMap, TEXT("enable_hash_info_uav"));
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FRHIShaderResourceView* hash_scan_srv,
		FRHIShaderResourceView* hash_id_srv,

		FRHIUnorderedAccessView* enable_hash_info_uav
	)
	{
		// 呼び出し直前にPipelineに自身が設定されているものとする.
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// SRV
		SetSRVParameter(BatchedParameters, hash_scan_srv_, hash_scan_srv);
		SetSRVParameter(BatchedParameters, hash_id_srv_, hash_id_srv);
		SetUAVParameter(BatchedParameters, enable_hash_info_uav_, enable_hash_info_uav);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_scan_srv_, hash_scan_srv);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_id_srv_, hash_id_srv);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, enable_hash_info_uav_, enable_hash_info_uav);
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
		SetSRVParameter(BatchedParameters, hash_scan_srv_, nullptr);
		SetSRVParameter(BatchedParameters, hash_id_srv_, nullptr);
		SetUAVParameter(BatchedParameters, enable_hash_info_uav_, nullptr);
	
		RHICmdList.SetBatchedShaderParameters(ComputeShaderRHI, BatchedParameters);
#else
		// SRV
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_scan_srv_, nullptr);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, hash_id_srv_, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, enable_hash_info_uav_, nullptr);
#endif
	}

	LAYOUT_FIELD(FShaderResourceParameter , hash_scan_srv_);

	LAYOUT_FIELD(FShaderResourceParameter , hash_id_srv_);

	LAYOUT_FIELD(FShaderResourceParameter , enable_hash_info_uav_);
};

// シェーダコードとGlobalShaderクラスを関連付け
IMPLEMENT_SHADER_TYPE(, FClearSortedBufferInfoCS, TEXT("/Plugin/NagaExperiment/Private/compute_sorted_buffer_util.usf"), TEXT("ClearSortedBufferInfo"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FClearUint2BufferCS, TEXT("/Plugin/NagaExperiment/Private/compute_sorted_buffer_util.usf"), TEXT("ClearUint2Buffer"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FGenSortedBufferInfoUint2CS, TEXT("/Plugin/NagaExperiment/Private/compute_sorted_buffer_util.usf"), TEXT("GenSortedBufferInfo"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FGenSortedBufferKeyDiffUint2CS, TEXT("/Plugin/NagaExperiment/Private/compute_sorted_buffer_util.usf"), TEXT("GenSortedBufferKeyDiff"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FExclusiveScanPerGroupUintCS, TEXT("/Plugin/NagaExperiment/Private/compute_sorted_buffer_util.usf"), TEXT("ExclusiveScanPerGroup"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FCompleteBlockExclusiveScanUintCS, TEXT("/Plugin/NagaExperiment/Private/compute_sorted_buffer_util.usf"), TEXT("CompleteBlockExclusiveScan"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FGenEnableHashInfoBufferCS, TEXT("/Plugin/NagaExperiment/Private/compute_sorted_buffer_util.usf"), TEXT("GenEnableHashInfoBuffer"), SF_Compute)



// 乱数でバッファリセット
class FComputeSetRandomValueUint2Cs : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputeSetRandomValueUint2Cs, Global);

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
		OutEnvironment.SetDefine(TEXT("NGL_CS_ENTRYPOINT_SET_RANDOM_UINT2"), 1);

		// ソートのグループスレッド数
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), CS_DISPATCH_THREAD_COUNT());
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// Useful when adding a permutation of a particular shader
		return true;
	}

	FComputeSetRandomValueUint2Cs() {}

	// シェーダパラメータと変数をバインド
	FComputeSetRandomValueUint2Cs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FGlobalShader(Initializer)
	{
		buffer_uav.Bind(Initializer.ParameterMap, TEXT("buffer_uav"));
	}

	// ソート対象バッファ
	LAYOUT_FIELD(FShaderResourceParameter , buffer_uav);
};

// シェーダコードとGlobalShaderクラスを関連付け
IMPLEMENT_SHADER_TYPE(, FComputeSetRandomValueUint2Cs, TEXT("/Plugin/NagaExperiment/Private/compute_sort_dispatch_reduce.usf"), TEXT("SetRandomValue_Uint2"), SF_Compute)



















// プロシージャルメッシュプロキシ
class FComputeShaderMeshProxy : public FPrimitiveSceneProxy
{
public:
	/*
	static inline void InitOrUpdateResource(FRenderResource* Resource)
	{
		if (!Resource->IsInitialized())
			Resource->InitResource();
		else
			Resource->UpdateRHI();
	}
	*/

public:
	FComputeShaderMeshProxy(const UComputeShaderMeshComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
		, component_(InComponent)
		, material_(InComponent->material_)
		, MaterialRelevance(InComponent->GetMaterialRelevance(GetScene().GetFeatureLevel()))
		, buffers_(ERHIFeatureLevel::SM5)
	{
		// 確保する最大エンティティ数
		// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		// 内部ではGPUソートのために二の冪とする. ディスパッチサイズよりも小さい場合がシェーダ側でのサイズチェック処理マクロを有効にする必要がある.遅くなるのでなるべく無効にしておきたい.
		// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		{
			// 最大値は採用しているソートCSによる. 少なくとも1024*1024までは可能(メモリが足らないかも).
			const int ENTITY_MAX_LIMIT = 1024 * 1024;
			// Tiled Transposeを使う場合はタイルサイズ以上にする. 現状実装のタイルサイズは16.
			const int ENTITY_MIN_LIMIT = 32;

			auto msb = ngl::math::Msb( FMath::Max( uint32(component_->grass_max_count_ - 1), 1u ) );

			// 指定された値以上の最小の二の冪
			max_entity_ = FMath::Clamp((0x01 << (msb + 1)), ENTITY_MIN_LIMIT, ENTITY_MAX_LIMIT);
		}

		// シミュレーション空間情報
		{
			// 下限チェック
			sim_space_cell_size_ = FMath::Max( 0.1f, component_->grass_simulation_space_cell_width_ );
		}


		// エンティティ一つに割り当てる頂点数とトライアングル数を決定
		num_entity_vertex_ = 12;// 1エンティティの頂点数
		num_entity_triangle_ = 4;// 1エンティティのトライアングル数


		bool enable_uav = true;
		bool cpu_access = false;
		int numTexcoord = 1;

		// バッファだけ確保
		// 頂点バッファ
		int numVertex = num_entity_vertex_ * max_entity_;
		buffers_.PositionVertexBuffer.Init(numVertex, enable_uav);
		buffers_.ColorVertexBuffer.Init(numVertex, enable_uav);
		buffers_.TangentsVertexBuffer.Init(numVertex, enable_uav);
		buffers_.TexcoordVertexBuffer.Init(numVertex, enable_uav);
		// インデックスバッファ
		int numIndex = 3 * num_entity_triangle_ * max_entity_;
		buffers_.IndexBuffer.Init(numIndex, enable_uav, cpu_access);


		// エンティティ用のバッファ
		// エンティティ毎の位置を与えるバッファ
		const float entity_pos_rand_range_z = 0.0f;
		const float entity_pos1_offset_len = 1.0f;
		TArray<GrassEntityInfo> grass_entity_array;
		TArray<ngl::gpgpu::UintVec2> entity_hash_array;


		int debug_sq_placement_w = 32;
		int debug_sq_placement_h = (int)ceilf(((double)max_entity_/(double)debug_sq_placement_w));
		for (auto i = 0; i < max_entity_; ++i)
		{
			// 適当に乱数配置
			auto pos0 = FVector3f::ZeroVector;
			auto pos1 = FVector3f(0, 0, entity_pos1_offset_len);

			GrassEntityInfo instData;
			instData.pos0.Set(pos0.X, pos0.Y, pos0.Z, 1.0f);
			instData.dir_len0 = FVector4f(FVector4(pos1.GetUnsafeNormal(), pos1.Size()));
			instData.pos1 = instData.pos0 + pos1;

			instData.life_sec		= 0.0f;// エミッタのテストのため最初は全部死亡.
			instData.life_sec_init	= 0.0f;

			ngl::gpgpu::UintVec2 hashData;
			hashData.x = 0;
			hashData.y = 0;

			grass_entity_array.Add(instData);
			entity_hash_array.Add(hashData);
		}

		entity_base_info_buffer_.Init(grass_entity_array, true, false);
		sorted_entity_counter_buffer_.Init(1, true, false);
		// 追加用バッファは追加用シェーダの1グループのスレッド数分
		entity_append_buffer_.Init(FComputeShaderGrassAppendDirectCS::CS_DISPATCH_THREAD_COUNT(), false, true);
		sorted_hash_buffer_.Init(entity_hash_array, true, false);
		// ハッシュが32bit uintなので32
		sorted_hash_bit_msb_index_buffer_.Init(32, true, false);
		sorted_hash_exclusive_scan_work.Init(max_entity_, true, false);
		sorted_hash_exclusive_scan.Init(max_entity_, true, false);
		// ワープスレッド数32を下限として、多くともその二倍のサイズのブロックでブロック単位Scanが行われると仮定して要素数を決定.
		const int WARP_THREAD_SIZE = 32;
		sorted_hash_sums_exclusive_scan0_.Init(FMath::Max(1, max_entity_ / (WARP_THREAD_SIZE * 2)), true, false);
		sorted_hash_sums_exclusive_scan1_.Init(FMath::Max(1, max_entity_ / (WARP_THREAD_SIZE * 2)), true, false);

		// 有効ハッシュ 安全を考えてエンティティ数と一致させるか.. 或いはエンティティが二つ以上存在するハッシュだけ積み込むような実装にする?
		enable_hash_buffer_.Init(max_entity_, true, false);

		grid_cell_data_buffer_.Init(max_entity_, true, false);

		// タイルベース転置バイトニックソート
		{
			simple_tiled_transposed_bitonic_sorter_.Init(max_entity_);
		}

		// InplaceなDispatch数削減版バイトニックソート
		{
			dispatch_reduce_bitonic_sorter_.Init(max_entity_);
		}


		// 各種バッファのRHIリソース初期化リクエスト( RenderCommandにInitRHI等がリクエストされる )
		BeginInitResource(&entity_base_info_buffer_);
		BeginInitResource(&sorted_entity_counter_buffer_);
		BeginInitResource(&entity_append_buffer_);
		BeginInitResource(&sorted_hash_buffer_);
		BeginInitResource(&sorted_hash_bit_msb_index_buffer_);
		BeginInitResource(&sorted_hash_exclusive_scan_work);
		BeginInitResource(&sorted_hash_exclusive_scan);
		BeginInitResource(&sorted_hash_sums_exclusive_scan0_);
		BeginInitResource(&sorted_hash_sums_exclusive_scan1_);
		BeginInitResource(&enable_hash_buffer_);
		BeginInitResource(&grid_cell_data_buffer_);

		// ジオメトリ用
		BeginInitResource(&buffers_.PositionVertexBuffer);
		BeginInitResource(&buffers_.TangentsVertexBuffer);
		BeginInitResource(&buffers_.TexcoordVertexBuffer);
		BeginInitResource(&buffers_.ColorVertexBuffer);
		BeginInitResource(&buffers_.IndexBuffer);


		// VertexFactoryの初期化リクエスト
		ngl_procedual_mesh::FNagaProceduralMeshBuffers* bufferHolder = &buffers_;
		uint32 LightMapIndex = 0;
		ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
			[bufferHolder, LightMapIndex](FRHICommandListImmediate& RHICmdList)
		{
			// ファクトリのセットアップ
			auto vtxFactoryPtr = &bufferHolder->VertexFactory;
			FLocalVertexFactory::FDataType Data;
			bufferHolder->PositionVertexBuffer.BindVertexBuffer(vtxFactoryPtr, Data);
			bufferHolder->ColorVertexBuffer.BindVertexBuffer(vtxFactoryPtr, Data);
			bufferHolder->TangentsVertexBuffer.BindVertexBuffer(vtxFactoryPtr, Data);
			bufferHolder->TexcoordVertexBuffer.BindVertexBuffer(vtxFactoryPtr, Data);
			bufferHolder->TexcoordVertexBuffer.BindVertexBufferAsLightMap(vtxFactoryPtr, Data, LightMapIndex);
			vtxFactoryPtr->SetData(RHICmdList, Data);

			// セットアップ後に頂点ファクトリ初期化. SetData後じゃないとだめっぽい
			bufferHolder->VertexFactory.InitResource(RHICmdList);
		});
	}
	~FComputeShaderMeshProxy()
	{
		entity_base_info_buffer_.ReleaseResource();
		sorted_entity_counter_buffer_.ReleaseResource();
		entity_append_buffer_.ReleaseResource();

		sorted_hash_buffer_.ReleaseResource();
		sorted_hash_bit_msb_index_buffer_.ReleaseResource();
		sorted_hash_exclusive_scan_work.ReleaseResource();
		sorted_hash_exclusive_scan.ReleaseResource();
		sorted_hash_sums_exclusive_scan0_.ReleaseResource();
		sorted_hash_sums_exclusive_scan1_.ReleaseResource();
		enable_hash_buffer_.ReleaseResource();
		grid_cell_data_buffer_.ReleaseResource();
	}

	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	// 描画メッシュ収集時に呼ばれる関数
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (!material_)
		{
			return;
		}

		FMaterialRenderProxy* MaterialProxy = material_->GetRenderProxy();
		// For each view..
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				// Draw the mesh.
				FMeshBatch& Mesh = Collector.AllocateMesh();
				FMeshBatchElement& BatchElement = Mesh.Elements[0];

				Mesh.bWireframe = false;

				// VertexFactory
				Mesh.VertexFactory = &buffers_.VertexFactory;
				Mesh.MaterialRenderProxy = MaterialProxy;

				// 標準のメッシュ定数バッファパラメータ
				bool bHasPrecomputedVolumetricLightmap;
				FMatrix PreviousLocalToWorld;
				int32 SingleCaptureIndex;
				bool bOutputVelocity;
				// 前回フレームでのワールドマトリクス等を取得
				GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
				FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
#if NGL_PROCEDURAL_MESH_USE_WORLD_SPACE_GEOMETRY
				// コンポーネントComputeで直接ワールド空間でのメッシュを計算する場合はこちら
#if ((5<=ENGINE_MAJOR_VERSION) && (4<=ENGINE_MINOR_VERSION))
				// UE5.4 で実行時エラーとなったため修正.
				DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), FMatrix::Identity, FMatrix::Identity, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity);
#elif ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				DynamicPrimitiveUniformBuffer.Set(FMatrix::Identity, FMatrix::Identity, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity);
#else
				DynamicPrimitiveUniformBuffer.Set(FMatrix::Identity, FMatrix::Identity, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);
#endif
#else
				// 今回のワールドマトリクスと前回のワールドマトリクスを設定
				DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);
#endif
				BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

				// 描画頂点の範囲情報等
				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = buffers_.IndexBuffer.NumIndex() / 3;
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = buffers_.PositionVertexBuffer.GetNumVertices() - 1;
				// RHIインデックスバッファ
				BatchElement.IndexBuffer = &buffers_.IndexBuffer;
				// トポロジ
				Mesh.Type = PT_TriangleList;

				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.bCanApplyViewModeOverrides = false;
				Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();

				// メッシュ収集オブジェクトに登録
				Collector.AddMesh(ViewIndex, Mesh);
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const
	{
#if 0
		bool bVisible = true;
		FPrimitiveViewRelevance Result;
	
		Result.bDrawRelevance = IsShown(View);

		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = IsShadowCast(View);

		return Result;
#else
		// FStaticMeshSceneProxy からコピー
		checkSlow(IsInParallelRenderingThread());

		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View) && View->Family->EngineShowFlags.StaticMeshes;
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;

		bool bInCollisionView = false;
		//const bool bAllowStaticLighting = FReadOnlyCVARCache::Get().bAllowStaticLighting;

		// 常にDynamic
		Result.bDynamicRelevance = true;

		Result.bShadowRelevance = IsShadowCast(View);

		MaterialRelevance.SetPrimitiveViewRelevance(Result);

		if (!View->Family->EngineShowFlags.Materials
			)
		{
			//Result.bOpaqueRelevance = true;
			Result.bOpaque = true;
		}

		//Result.bVelocityRelevance = IsMovable() && Result.bOpaqueRelevance && Result.bRenderInMainPass;
		Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;

		return Result;
#endif
	}
	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest && !ShouldRenderCustomDepth();
	}
	virtual bool IsUsingDistanceCullFade() const override
	{
		return MaterialRelevance.bUsesDistanceCullFade;
	}
	virtual uint32 GetMemoryFootprint() const { return sizeof(*this) + GetAllocatedSize(); }
	//uint32 GetAllocatedSize() const { return FPrimitiveSceneProxy::GetAllocatedSize(); }

	// Computeのエントリ
	void EnqueueDispatchComputeShader()
	{
		// 描画コマンドの発行
		ERHIFeatureLevel::Type FeatureLevel = component_->GetWorld()->Scene->GetFeatureLevel();
		ENQUEUE_RENDER_COMMAND(CaptureCommand)(
			[this, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			DispatchComputeShader_RenderThread(
				RHICmdList,
				this,
				FeatureLevel);
		}
		);
	}
	// RenderThreadにおけるCompute処理
	static void DispatchComputeShader_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FComputeShaderMeshProxy* proxy,
		ERHIFeatureLevel::Type FeatureLevel)
	{
		check(IsInRenderingThread());

#if WANTS_DRAW_MESH_EVENTS
		FString EventName("ProceduralMesh");
		//SCOPED_DRAW_EVENTF(RHICmdList, SceneCapture, TEXT("DispatchComputeShader_RenderThread %s"), *EventName);
#else
		//SCOPED_DRAW_EVENT(RHICmdList, DispatchComputeShader_RenderThread);
#endif


		// シミュレーション空間情報の作成
		FBox		sim_space_aabb = FBox( -FVector(500.0f), FVector(500.0f));
		FIntVector sim_space_cell_count = FIntVector(1,1,1);
		float sim_space_cell_size_inv = 1.0f / proxy->sim_space_cell_size_;
		{
			// 最大分割数. 現時点ではこの解像度分メモリ確保はしない方針なので最大限まで有効にする. 等間隔格子分メモリ確保する場合は注意する.
			constexpr int MAX_CELL_COUNT = 0x01 << 8;// Hashは1軸10bit確保しているが一応余裕も持って8bitまでにしておく.

			FVector world_pos = proxy->component_->GetComponentTransform().GetLocation();
			FVector aabb_size = FVector(proxy->sim_space_cell_size_ * static_cast<float>(MAX_CELL_COUNT));

			sim_space_aabb = FBox::BuildAABB( world_pos, aabb_size * 0.5f);
			sim_space_cell_count = FIntVector(MAX_CELL_COUNT);
		}


		auto* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

		// 要素追加.
		// 前のフレームのソートによって末尾に無効要素が固まっており、さらに無効要素の個数がわかっているのでその後ろの部分に新規要素を追加する.
		if(proxy->is_first_render_update_completed_)
		{
			const auto* emission_buffer = proxy->component_->GetCurrentFrameEmissionBuffer(true);

			if (0 < emission_buffer->Num())
			{
				// Lockして書き込み
				auto& append_info_buffer = proxy->entity_append_buffer_;
				{
					uint32 lockByteSize = emission_buffer->Num() * emission_buffer->GetTypeSize();
					//void* CapsuleShapeLockedData = RHILockBuffer(append_info_buffer.GetBuffer(), 0, lockByteSize, RLM_WriteOnly);
					void* CapsuleShapeLockedData = RHICmdList.LockBuffer(append_info_buffer.GetBuffer(), 0, lockByteSize, RLM_WriteOnly);
					FPlatformMemory::Memcpy(CapsuleShapeLockedData, emission_buffer->GetData(), lockByteSize);
					//RHIUnlockBuffer(append_info_buffer.GetBuffer());
					RHICmdList.UnlockBuffer(append_info_buffer.GetBuffer());
				}


				RHICmdList.Transition(FRHITransitionInfo(proxy->entity_base_info_buffer_.GetUav(), proxy->entity_base_info_buffer_.GetCurrentRhiState(), proxy->entity_base_info_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_entity_counter_buffer_.GetUav(), proxy->sorted_entity_counter_buffer_.GetCurrentRhiState(), proxy->sorted_entity_counter_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));

				// UAV書き換え
				TShaderMapRef<FComputeShaderGrassAppendDirectCS> append_direct_cs(GlobalShaderMap);

#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(RHICmdList, append_direct_cs.GetComputeShader());
#else
				RHICmdList.SetComputeShader(append_direct_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
				append_direct_cs->SetParameters(RHICmdList, 
					proxy->sorted_hash_buffer_.GetSrv(), emission_buffer->Num(), append_info_buffer.GetSrv(), proxy->entity_base_info_buffer_.GetUav(), proxy->sorted_entity_counter_buffer_.GetUav());
#else
				// SRV
				SetSRVParameter(RHICmdList, append_direct_cs.GetComputeShader(), append_direct_cs->hash_srv, proxy->sorted_hash_buffer_.GetSrv());
				// 追加個数定数バッファ
				SetShaderValue(RHICmdList, append_direct_cs.GetComputeShader(), append_direct_cs->cb_append_instance_count, emission_buffer->Num());
				// SRV 追加用パラメータ
				SetSRVParameter(RHICmdList, append_direct_cs.GetComputeShader(), append_direct_cs->append_instance_srv, append_info_buffer.GetSrv());				
				// UAV
				SetUAVParameter(RHICmdList, append_direct_cs.GetComputeShader(), append_direct_cs->instance_uav_, proxy->entity_base_info_buffer_.GetUav());
				// 初期化でゼロクリアしているので最初のフレーム等で不定値が入っていて誤作動することはないと思うが注意.
				SetUAVParameter(RHICmdList, append_direct_cs.GetComputeShader(), append_direct_cs->entity_count_info_uav, proxy->sorted_entity_counter_buffer_.GetUav());
#endif
				// Dispatch 要素追加シェーダはカウンタの更新の同期のため1グループのみ. 1Fに1024要素追加できれば十分と思われるが...
				const int CsThreadCount = append_direct_cs->CS_DISPATCH_THREAD_COUNT();
				DispatchComputeShader(RHICmdList, append_direct_cs, 1, 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
				append_direct_cs->ResetParameters(RHICmdList);
#else
				// SRVリセット
				SetSRVParameter(RHICmdList, append_direct_cs.GetComputeShader(), append_direct_cs->append_instance_srv, nullptr);
				SetSRVParameter(RHICmdList, append_direct_cs.GetComputeShader(), append_direct_cs->hash_srv, nullptr);
				// UAVリセット
				SetUAVParameter(RHICmdList, append_direct_cs.GetComputeShader(), append_direct_cs->instance_uav_, nullptr);
				SetUAVParameter(RHICmdList, append_direct_cs.GetComputeShader(), append_direct_cs->entity_count_info_uav, nullptr);
#endif

				RHICmdList.Transition(FRHITransitionInfo(proxy->entity_base_info_buffer_.GetUav(), proxy->entity_base_info_buffer_.GetCurrentRhiState(), proxy->entity_base_info_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_entity_counter_buffer_.GetUav(), proxy->sorted_entity_counter_buffer_.GetCurrentRhiState(), proxy->sorted_entity_counter_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
			}
		}

		// 要素更新と状態バッファ更新. 
		// 後段のソートのために状態バッファに各要素の状態を書き込み. 状態バッファのHashは無効要素がソートで末尾に集まるように最上位ビット1のような無効値にしておく.
		// 基本的にはここがメインの要素に対する処理.
		// ディスパッチ数は前のフレームで確定した有効要素数.
		{

			const auto* obstacleBufferPtr = proxy->component_->GetCurrentFrameObstacleSphereBuffer(true);
			FVector4f obstacle_sphere = FVector4f(0 < obstacleBufferPtr->Num() ? (*obstacleBufferPtr)[0] : FVector4());

			const auto* destructionBufferPtr = proxy->component_->GetCurrentFrameDestructionSphereBuffer(true);
			FVector4f destruction_sphere = FVector4f(0 < destructionBufferPtr->Num() ? (*destructionBufferPtr)[0] : FVector4());


			RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_buffer_.GetUav(), proxy->sorted_hash_buffer_.GetCurrentRhiState(), proxy->sorted_hash_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));
			RHICmdList.Transition(FRHITransitionInfo(proxy->entity_base_info_buffer_.GetUav(), proxy->entity_base_info_buffer_.GetCurrentRhiState(), proxy->entity_base_info_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));


			// UAV書き換え
			TShaderMapRef<FComputeShaderGrassUpdateCS> cs(GlobalShaderMap);

#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
			// UE5.1から非推奨となった関数を置き換え.
			SetComputePipelineState(RHICmdList, cs.GetComputeShader());
#else
			RHICmdList.SetComputeShader(cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
			cs->SetParameters(RHICmdList,
				*proxy->component_->GetCurrentFrameDeltaSec(true), FVector3f(sim_space_aabb.Min), FVector3f(sim_space_aabb.Max), sim_space_cell_count, sim_space_cell_size_inv, obstacle_sphere, destruction_sphere,
				proxy->entity_base_info_buffer_.GetUav(), proxy->sorted_hash_buffer_.GetUav()
			);
#else
			// デルタタイム
			SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_delta_sec, *proxy->component_->GetCurrentFrameDeltaSec(true));

			// シミュレーション空間情報
			SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_sim_space_aabb_min, FVector3f(sim_space_aabb.Min));
			SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_sim_space_aabb_max, FVector3f(sim_space_aabb.Max));
			SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_sim_space_cell_count, sim_space_cell_count);
			SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_sim_space_cell_size_inv, sim_space_cell_size_inv);


			// 障害物球情報
			SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_debug_obstacle_sphere_info_, obstacle_sphere);
		
			// 破壊球情報
			SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_debug_destruct_sphere_info_, destruction_sphere);

			// UAV
			SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->instance_uav_, proxy->entity_base_info_buffer_.GetUav());
			SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->hash_uav_, proxy->sorted_hash_buffer_.GetUav());
#endif
			// Dispatch.
			const int CsThreadCount = cs->CS_DISPATCH_THREAD_COUNT();
			DispatchComputeShader(RHICmdList, cs, (proxy->max_entity_ + (CsThreadCount - 1)) / CsThreadCount, 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
			cs->ResetParameters(RHICmdList);
#else
			// UAVリセット
			SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->instance_uav_, nullptr);
			SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->hash_uav_, nullptr);
#endif

			RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_buffer_.GetUav(), proxy->sorted_hash_buffer_.GetCurrentRhiState(), proxy->sorted_hash_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
			RHICmdList.Transition(FRHITransitionInfo(proxy->entity_base_info_buffer_.GetUav(), proxy->entity_base_info_buffer_.GetCurrentRhiState(), proxy->entity_base_info_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
		}

		// ソート
		{
			RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_buffer_.GetUav(), proxy->sorted_hash_buffer_.GetCurrentRhiState(), proxy->sorted_hash_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));
#if 0
			// Tiled Transpose Bitonic を利用. 可読性良好, ディスパッチ数が多いためDispatch Reduce Bitonicより低速
			proxy->simple_tiled_transposed_bitonic_sorter_.Run( RHICmdList, FeatureLevel, proxy->sorted_hash_buffer_);
#else
			// Dispatch Reduce Bitonic　を利用. 可読性に難あり. ディスパッチ数が非常に少なくTiled Transpose Bitonicの倍近く高速.
			proxy->dispatch_reduce_bitonic_sorter_.Run(RHICmdList, FeatureLevel, proxy->sorted_hash_buffer_);
#endif

			RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_buffer_.GetUav(), proxy->sorted_hash_buffer_.GetCurrentRhiState(), proxy->sorted_hash_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
		}

		// 有効要素カウント.
		{
			// ソート済みバッファHashMsb開始インデックスバッファをクリア
			{
				// クリア値は無効な値として全ビット1とする.
				uint32 invalid_value_uint32 = ~uint32(0);


				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_bit_msb_index_buffer_.GetUav(), proxy->sorted_hash_bit_msb_index_buffer_.GetCurrentRhiState(), proxy->sorted_hash_bit_msb_index_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));


				TShaderMapRef<FClearSortedBufferInfoCS> clear_sorted_buffer_info_cs(GlobalShaderMap);
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(RHICmdList, clear_sorted_buffer_info_cs.GetComputeShader());
#else
				RHICmdList.SetComputeShader(clear_sorted_buffer_info_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
				clear_sorted_buffer_info_cs->SetParameters(RHICmdList,
					invalid_value_uint32,
					proxy->sorted_hash_bit_msb_index_buffer_.GetUav()
					);
#else
				SetUAVParameter(RHICmdList, clear_sorted_buffer_info_cs.GetComputeShader(), clear_sorted_buffer_info_cs->hash_msb_start_index_32_uav_, proxy->sorted_hash_bit_msb_index_buffer_.GetUav());
				SetShaderValue(RHICmdList, clear_sorted_buffer_info_cs.GetComputeShader(), clear_sorted_buffer_info_cs->cb_clear_value_, invalid_value_uint32);
#endif
				// Dispatch. 32 threadなので1グループのみ.
				DispatchComputeShader(RHICmdList, clear_sorted_buffer_info_cs, 1, 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
				clear_sorted_buffer_info_cs->ResetParameters(RHICmdList);
#else
				// UAVリセット
				SetUAVParameter(RHICmdList, clear_sorted_buffer_info_cs.GetComputeShader(), clear_sorted_buffer_info_cs->hash_msb_start_index_32_uav_, nullptr);
#endif

				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_bit_msb_index_buffer_.GetUav(), proxy->sorted_hash_bit_msb_index_buffer_.GetCurrentRhiState(), proxy->sorted_hash_bit_msb_index_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
			}

			// ソート済みバッファHashMsb開始インデックスバッファへ書き込み. MSB開始インデックスをバッファに格納しておくことであとでいろいろ使う.
			{
				// ソート済みバッファを読み取るのでバリア
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_bit_msb_index_buffer_.GetUav(), proxy->sorted_hash_bit_msb_index_buffer_.GetCurrentRhiState(), proxy->sorted_hash_bit_msb_index_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));

				// ソート済みバッファ情報UAVを構築
				TShaderMapRef<FGenSortedBufferInfoUint2CS> gen_sorted_buffer_info_cs(GlobalShaderMap);
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(RHICmdList, gen_sorted_buffer_info_cs.GetComputeShader());
#else
				RHICmdList.SetComputeShader(gen_sorted_buffer_info_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
				gen_sorted_buffer_info_cs->SetParameters(RHICmdList,
					proxy->sorted_hash_buffer_.GetSrv(),
					proxy->sorted_hash_bit_msb_index_buffer_.GetUav()
					);
#else
				// UAV
				SetUAVParameter(RHICmdList, gen_sorted_buffer_info_cs.GetComputeShader(), gen_sorted_buffer_info_cs->hash_msb_start_index_32_uav_, proxy->sorted_hash_bit_msb_index_buffer_.GetUav());
				// SRV
				SetSRVParameter(RHICmdList, gen_sorted_buffer_info_cs.GetComputeShader(), gen_sorted_buffer_info_cs->array_srv_, proxy->sorted_hash_buffer_.GetSrv());
#endif

				// Dispatch.
				const int CsThreadCount = gen_sorted_buffer_info_cs->CS_DISPATCH_THREAD_COUNT();
				DispatchComputeShader(RHICmdList, gen_sorted_buffer_info_cs, static_cast<unsigned int>((proxy->max_entity_ + CsThreadCount - 1) / CsThreadCount), 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
				gen_sorted_buffer_info_cs->ResetParameters(RHICmdList);
#else
				// UAVリセット
				SetUAVParameter(RHICmdList, gen_sorted_buffer_info_cs.GetComputeShader(), gen_sorted_buffer_info_cs->hash_msb_start_index_32_uav_, nullptr);
				// SRVリセット
				SetSRVParameter(RHICmdList, gen_sorted_buffer_info_cs.GetComputeShader(), gen_sorted_buffer_info_cs->array_srv_, nullptr);
#endif

				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_bit_msb_index_buffer_.GetUav(), proxy->sorted_hash_bit_msb_index_buffer_.GetCurrentRhiState(), proxy->sorted_hash_bit_msb_index_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
			}


			// ソート済みバッファHashMsb開始インデックスバッファからComputeGrass用のカウンタバッファにコピー
			{
				// MSB開始インデックスバッファを読み取るのでバリア
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_entity_counter_buffer_.GetUav(), proxy->sorted_entity_counter_buffer_.GetCurrentRhiState(), proxy->sorted_entity_counter_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));


				TShaderMapRef<FComputeShaderGrassGenerateCountCS> gen_grass_count_info_cs(GlobalShaderMap);
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(RHICmdList, gen_grass_count_info_cs.GetComputeShader());
#else
				RHICmdList.SetComputeShader(gen_grass_count_info_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
				gen_grass_count_info_cs->SetParameters(RHICmdList,
					proxy->sorted_hash_buffer_.GetSrv(),
					proxy->sorted_hash_bit_msb_index_buffer_.GetSrv(),
					proxy->sorted_entity_counter_buffer_.GetUav()
					);
#else
				SetUAVParameter(RHICmdList, gen_grass_count_info_cs.GetComputeShader(), gen_grass_count_info_cs->entity_count_info_uav_, proxy->sorted_entity_counter_buffer_.GetUav());
				SetSRVParameter(RHICmdList, gen_grass_count_info_cs.GetComputeShader(), gen_grass_count_info_cs->hash_srv_, proxy->sorted_hash_buffer_.GetSrv());
				SetSRVParameter(RHICmdList, gen_grass_count_info_cs.GetComputeShader(), gen_grass_count_info_cs->hash_msb_start_index_32_srv_, proxy->sorted_hash_bit_msb_index_buffer_.GetSrv());
#endif

				// 1スレッドグループ実行.
				DispatchComputeShader(RHICmdList, gen_grass_count_info_cs, 1, 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
				gen_grass_count_info_cs->ResetParameters(RHICmdList);
#else
				SetUAVParameter(RHICmdList, gen_grass_count_info_cs.GetComputeShader(), gen_grass_count_info_cs->entity_count_info_uav_, nullptr);
				SetSRVParameter(RHICmdList, gen_grass_count_info_cs.GetComputeShader(), gen_grass_count_info_cs->hash_srv_, nullptr);
				SetSRVParameter(RHICmdList, gen_grass_count_info_cs.GetComputeShader(), gen_grass_count_info_cs->hash_msb_start_index_32_srv_, nullptr);
#endif

				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_entity_counter_buffer_.GetUav(), proxy->sorted_entity_counter_buffer_.GetCurrentRhiState(), proxy->sorted_entity_counter_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
			}
		}


		// 念のための有効ハッシュバッファクリア. 終端書き込みで変なアクセスしていないか確認のためクリアする. 実際には不要のはず.
#if 0
		{
			TShaderMapRef<FClearUint2BufferCS> clear_uint2_buffer_cs(GlobalShaderMap);
			RHICmdList.SetComputeShader(clear_uint2_buffer_cs.GetComputeShader());
			SetUAVParameter(RHICmdList, clear_uint2_buffer_cs.GetComputeShader(), clear_uint2_buffer_cs->out_uav, proxy->enable_hash_buffer_.GetUav());
			// クリア値は無効な値として全ビット1とする.
			UintVec2 clear_value;
			clear_value.x = 0;
			clear_value.y = 0;
			SetShaderValue(RHICmdList, clear_uint2_buffer_cs.GetComputeShader(), clear_uint2_buffer_cs->cb_clear_value, clear_value);

			// Dispatch. 32 threadなので1グループのみ.
			const int CsThreadCount = clear_uint2_buffer_cs->CS_DISPATCH_THREAD_COUNT();
			DispatchComputeShader(RHICmdList, *clear_uint2_buffer_cs, static_cast<unsigned int>((proxy->max_entity_ + CsThreadCount - 1) / CsThreadCount), 1, 1);

			// UAVリセット
			SetUAVParameter(RHICmdList, clear_uint2_buffer_cs.GetComputeShader(), clear_uint2_buffer_cs->out_uav, nullptr);
		}
#endif

		// ソート済みハッシュキーのExclusiveScan(prefixsum).
{
// PrefixSumの元データ構築
			{
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_exclusive_scan_work.GetUav(), proxy->sorted_hash_exclusive_scan_work.GetCurrentRhiState(), proxy->sorted_hash_exclusive_scan_work.TransitionRhiState(ERHIAccess::UAVCompute)));


				TShaderMapRef<FGenSortedBufferKeyDiffUint2CS> gen_sorted_key_diff_cs(GlobalShaderMap);
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(RHICmdList, gen_sorted_key_diff_cs.GetComputeShader());
#else
				RHICmdList.SetComputeShader(gen_sorted_key_diff_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
				gen_sorted_key_diff_cs->SetParameters(RHICmdList,
					proxy->sorted_hash_buffer_.GetSrv(),
					proxy->sorted_hash_exclusive_scan_work.GetUav()
					);
#else
				SetSRVParameter(RHICmdList, gen_sorted_key_diff_cs.GetComputeShader(), gen_sorted_key_diff_cs->array_srv_, proxy->sorted_hash_buffer_.GetSrv());
				SetUAVParameter(RHICmdList, gen_sorted_key_diff_cs.GetComputeShader(), gen_sorted_key_diff_cs->out_uav_, proxy->sorted_hash_exclusive_scan_work.GetUav());
#endif

				// Dispatch.
				const int CsThreadCount = gen_sorted_key_diff_cs->CS_DISPATCH_THREAD_COUNT();
				DispatchComputeShader(RHICmdList, gen_sorted_key_diff_cs, static_cast<unsigned int>((proxy->max_entity_ + CsThreadCount - 1) / CsThreadCount), 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
				gen_sorted_key_diff_cs->ResetParameters(RHICmdList);
#else
				// UAVリセット
				SetUAVParameter(RHICmdList, gen_sorted_key_diff_cs.GetComputeShader(), gen_sorted_key_diff_cs->out_uav_, nullptr);
				// SRVリセット
				SetSRVParameter(RHICmdList, gen_sorted_key_diff_cs.GetComputeShader(), gen_sorted_key_diff_cs->array_srv_, nullptr);
#endif

				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_exclusive_scan_work.GetUav(), proxy->sorted_hash_exclusive_scan_work.GetCurrentRhiState(), proxy->sorted_hash_exclusive_scan_work.TransitionRhiState(ERHIAccess::SRVCompute)));
			}

// スレッドグループ*2要素毎に処理するブロック単位ExclusiveScan
// このシェーダは1グループにつきグループスレッド数の2倍の要素をScanするのでディスパッチグループ数に注意.
TShaderMapRef<FExclusiveScanPerGroupUintCS> exclusive_scan_per_group_cs(GlobalShaderMap);
const uint32 exclusive_scan_group_size = exclusive_scan_per_group_cs->CS_DISPATCH_THREAD_COUNT() * 2;
				{
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_exclusive_scan.GetUav(), proxy->sorted_hash_exclusive_scan.GetCurrentRhiState(), proxy->sorted_hash_exclusive_scan.TransitionRhiState(ERHIAccess::UAVCompute)));
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_sums_exclusive_scan0_.GetUav(), proxy->sorted_hash_sums_exclusive_scan0_.GetCurrentRhiState(), proxy->sorted_hash_sums_exclusive_scan0_.TransitionRhiState(ERHIAccess::UAVCompute)));


#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader());
#else
				RHICmdList.SetComputeShader(exclusive_scan_per_group_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
				exclusive_scan_per_group_cs->SetParameters(RHICmdList,
					proxy->sorted_hash_exclusive_scan_work.GetSrv(),
					proxy->sorted_hash_exclusive_scan.GetUav(),
					proxy->sorted_hash_sums_exclusive_scan0_.GetUav()
					);
#else
				SetSRVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->array_srv_, proxy->sorted_hash_exclusive_scan_work.GetSrv());
				SetUAVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->scan_per_group_uav_, proxy->sorted_hash_exclusive_scan.GetUav());
				SetUAVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->block_sums_uav_, proxy->sorted_hash_sums_exclusive_scan0_.GetUav());
#endif

				// 1スレッドが2要素担当するのでグループ数は全要素に対してグループスレッド数*2で計算
				DispatchComputeShader(RHICmdList, exclusive_scan_per_group_cs, static_cast<unsigned int>((proxy->max_entity_ + exclusive_scan_group_size - 1) / exclusive_scan_group_size), 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
				exclusive_scan_per_group_cs->ResetParameters(RHICmdList);
#else
				// UAVリセット
				SetUAVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->scan_per_group_uav_, nullptr);
				SetUAVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->block_sums_uav_, nullptr);
				// SRVリセット
				SetSRVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->array_srv_, nullptr);
#endif

				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_exclusive_scan.GetUav(), proxy->sorted_hash_exclusive_scan.GetCurrentRhiState(), proxy->sorted_hash_exclusive_scan.TransitionRhiState(ERHIAccess::SRVCompute)));
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_sums_exclusive_scan0_.GetUav(), proxy->sorted_hash_sums_exclusive_scan0_.GetCurrentRhiState(), proxy->sorted_hash_sums_exclusive_scan0_.TransitionRhiState(ERHIAccess::SRVCompute)));
				}
// ブロック毎の合計バッファに対してScanを実行
				{
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_exclusive_scan_work.GetUav(), proxy->sorted_hash_exclusive_scan_work.GetCurrentRhiState(), proxy->sorted_hash_exclusive_scan_work.TransitionRhiState(ERHIAccess::UAVCompute)));


#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader());
#else
				RHICmdList.SetComputeShader(exclusive_scan_per_group_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
				exclusive_scan_per_group_cs->SetParameters(RHICmdList,
					proxy->sorted_hash_sums_exclusive_scan0_.GetSrv(),
					proxy->sorted_hash_exclusive_scan_work.GetUav(),
					proxy->sorted_hash_sums_exclusive_scan1_.GetUav()
				);
#else
				SetSRVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->array_srv_, proxy->sorted_hash_sums_exclusive_scan0_.GetSrv());
				SetUAVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->scan_per_group_uav_, proxy->sorted_hash_exclusive_scan_work.GetUav());
				SetUAVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->block_sums_uav_, proxy->sorted_hash_sums_exclusive_scan1_.GetUav());// 実際は使わないがダミーとして
#endif

				// 1スレッドが2要素担当するのでグループ数は全要素に対してグループスレッド数*2で計算
				DispatchComputeShader(RHICmdList, exclusive_scan_per_group_cs, static_cast<unsigned int>((proxy->max_entity_ + exclusive_scan_group_size - 1) / exclusive_scan_group_size), 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
#else
				// UAVリセット
				SetUAVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->scan_per_group_uav_, nullptr);
				SetUAVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->block_sums_uav_, nullptr);
				// SRVリセット
				SetSRVParameter(RHICmdList, exclusive_scan_per_group_cs.GetComputeShader(), exclusive_scan_per_group_cs->array_srv_, nullptr);
#endif

				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_exclusive_scan_work.GetUav(), proxy->sorted_hash_exclusive_scan_work.GetCurrentRhiState(), proxy->sorted_hash_exclusive_scan_work.TransitionRhiState(ERHIAccess::SRVCompute)));
				}

// ExclusiveScanの完了
				{
				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_exclusive_scan.GetUav(), proxy->sorted_hash_exclusive_scan.GetCurrentRhiState(), proxy->sorted_hash_exclusive_scan.TransitionRhiState(ERHIAccess::UAVCompute)));

				// このシェーダは1グループにつきグループスレッド数の2倍の要素をScanするのでディスパッチグループ数に注意.
				TShaderMapRef<FCompleteBlockExclusiveScanUintCS> complete_exclusive_scan_cs(GlobalShaderMap);
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(RHICmdList, complete_exclusive_scan_cs.GetComputeShader());
#else
				RHICmdList.SetComputeShader(complete_exclusive_scan_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
				complete_exclusive_scan_cs->SetParameters(RHICmdList,
					exclusive_scan_group_size,
					proxy->sorted_hash_exclusive_scan_work.GetSrv(),
					proxy->sorted_hash_exclusive_scan.GetUav()
					);
#else
				SetSRVParameter(RHICmdList, complete_exclusive_scan_cs.GetComputeShader(), complete_exclusive_scan_cs->upper_scan_srv_, proxy->sorted_hash_exclusive_scan_work.GetSrv());
				SetUAVParameter(RHICmdList, complete_exclusive_scan_cs.GetComputeShader(), complete_exclusive_scan_cs->array_uav_, proxy->sorted_hash_exclusive_scan.GetUav());
				SetShaderValue(RHICmdList, complete_exclusive_scan_cs.GetComputeShader(), complete_exclusive_scan_cs->cb_block_size_, exclusive_scan_group_size);
#endif

				// Dispatch.
				const int CsThreadCount = complete_exclusive_scan_cs->CS_DISPATCH_THREAD_COUNT();
				DispatchComputeShader(RHICmdList, complete_exclusive_scan_cs, static_cast<unsigned int>((proxy->max_entity_ + CsThreadCount - 1) / CsThreadCount), 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
				complete_exclusive_scan_cs->ResetParameters(RHICmdList);
#else
				// UAVリセット
				SetUAVParameter(RHICmdList, complete_exclusive_scan_cs.GetComputeShader(), complete_exclusive_scan_cs->array_uav_, nullptr);
				// SRVリセット
				SetSRVParameter(RHICmdList, complete_exclusive_scan_cs.GetComputeShader(), complete_exclusive_scan_cs->upper_scan_srv_, nullptr);
#endif

				RHICmdList.Transition(FRHITransitionInfo(proxy->sorted_hash_exclusive_scan.GetUav(), proxy->sorted_hash_exclusive_scan.GetCurrentRhiState(), proxy->sorted_hash_exclusive_scan.TransitionRhiState(ERHIAccess::SRVCompute)));
				}
}

		// ExclusiveScane結果から有効なハッシュ値に関する情報を構築->enable_hash_buffer_
		// 0番要素に有効なハッシュ値の個数N
		// 1番以降にN個の有効なハッシュの値とそのハッシュ値が開始するsorted_hash_buffer_上のインデックスが格納される
		// 0番に有効要素数を格納しているのは二分探索時に必要な情報を集約するため.
{
// バッファ生成
			{
				RHICmdList.Transition(FRHITransitionInfo(proxy->enable_hash_buffer_.GetUav(), proxy->enable_hash_buffer_.GetCurrentRhiState(), proxy->enable_hash_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));


				TShaderMapRef<FGenEnableHashInfoBufferCS> gen_enable_hash_info_buffer_cs(GlobalShaderMap);
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				// UE5.1から非推奨となった関数を置き換え.
				SetComputePipelineState(RHICmdList, gen_enable_hash_info_buffer_cs.GetComputeShader());
#else
				RHICmdList.SetComputeShader(gen_enable_hash_info_buffer_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
				gen_enable_hash_info_buffer_cs->SetParameters(RHICmdList,
					proxy->sorted_hash_exclusive_scan.GetSrv(),
					proxy->sorted_hash_buffer_.GetSrv(),
					proxy->enable_hash_buffer_.GetUav()
					);
#else
				SetSRVParameter(RHICmdList, gen_enable_hash_info_buffer_cs.GetComputeShader(), gen_enable_hash_info_buffer_cs->hash_scan_srv_, proxy->sorted_hash_exclusive_scan.GetSrv());
				SetSRVParameter(RHICmdList, gen_enable_hash_info_buffer_cs.GetComputeShader(), gen_enable_hash_info_buffer_cs->hash_id_srv_, proxy->sorted_hash_buffer_.GetSrv());
				SetUAVParameter(RHICmdList, gen_enable_hash_info_buffer_cs.GetComputeShader(), gen_enable_hash_info_buffer_cs->enable_hash_info_uav_, proxy->enable_hash_buffer_.GetUav());
#endif

				// Dispatch.
				const int CsThreadCount = gen_enable_hash_info_buffer_cs->CS_DISPATCH_THREAD_COUNT();
				DispatchComputeShader(RHICmdList, gen_enable_hash_info_buffer_cs, static_cast<unsigned int>((proxy->max_entity_ + CsThreadCount - 1) / CsThreadCount), 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
				gen_enable_hash_info_buffer_cs->ResetParameters(RHICmdList);
#else
				SetUAVParameter(RHICmdList, gen_enable_hash_info_buffer_cs.GetComputeShader(), gen_enable_hash_info_buffer_cs->enable_hash_info_uav_, nullptr);
				SetSRVParameter(RHICmdList, gen_enable_hash_info_buffer_cs.GetComputeShader(), gen_enable_hash_info_buffer_cs->hash_scan_srv_, nullptr);
				SetSRVParameter(RHICmdList, gen_enable_hash_info_buffer_cs.GetComputeShader(), gen_enable_hash_info_buffer_cs->hash_id_srv_, nullptr);
#endif

				RHICmdList.Transition(FRHITransitionInfo(proxy->enable_hash_buffer_.GetUav(), proxy->enable_hash_buffer_.GetCurrentRhiState(), proxy->enable_hash_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
			}
}
		// グリッドセルの更新
{
RHICmdList.Transition(FRHITransitionInfo(proxy->grid_cell_data_buffer_.GetUav(), proxy->grid_cell_data_buffer_.GetCurrentRhiState(), proxy->grid_cell_data_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));

TShaderMapRef<FComputeShaderGrassGatherToGridCellCS> gather_to_gridcell_cs(GlobalShaderMap);
#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
// UE5.1から非推奨となった関数を置き換え.
SetComputePipelineState(RHICmdList, gather_to_gridcell_cs.GetComputeShader());
#else
RHICmdList.SetComputeShader(gather_to_gridcell_cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
gather_to_gridcell_cs->SetParameters(RHICmdList,
	proxy->sorted_hash_buffer_.GetSrv(),
	proxy->entity_base_info_buffer_.GetSrv(),
	proxy->enable_hash_buffer_.GetSrv(),
	proxy->grid_cell_data_buffer_.GetUav()
	);
#else
SetSRVParameter(RHICmdList, gather_to_gridcell_cs.GetComputeShader(), gather_to_gridcell_cs->instance_srv_, proxy->entity_base_info_buffer_.GetSrv());
SetSRVParameter(RHICmdList, gather_to_gridcell_cs.GetComputeShader(), gather_to_gridcell_cs->hash_srv_, proxy->sorted_hash_buffer_.GetSrv());
SetSRVParameter(RHICmdList, gather_to_gridcell_cs.GetComputeShader(), gather_to_gridcell_cs->enable_hash_info_srv_, proxy->enable_hash_buffer_.GetSrv());
SetUAVParameter(RHICmdList, gather_to_gridcell_cs.GetComputeShader(), gather_to_gridcell_cs->gridcell_data_uav_, proxy->grid_cell_data_buffer_.GetUav());
#endif

// Dispatch.
const int CsThreadCount = gather_to_gridcell_cs->CS_DISPATCH_THREAD_COUNT();
DispatchComputeShader(RHICmdList, gather_to_gridcell_cs, static_cast<unsigned int>((proxy->max_entity_ + CsThreadCount - 1) / CsThreadCount), 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
gather_to_gridcell_cs->ResetParameters(RHICmdList);
#else
SetUAVParameter(RHICmdList, gather_to_gridcell_cs.GetComputeShader(), gather_to_gridcell_cs->gridcell_data_uav_, nullptr);
SetSRVParameter(RHICmdList, gather_to_gridcell_cs.GetComputeShader(), gather_to_gridcell_cs->enable_hash_info_srv_, nullptr);
SetSRVParameter(RHICmdList, gather_to_gridcell_cs.GetComputeShader(), gather_to_gridcell_cs->hash_srv_, nullptr);
SetSRVParameter(RHICmdList, gather_to_gridcell_cs.GetComputeShader(), gather_to_gridcell_cs->instance_srv_, nullptr);
#endif
RHICmdList.Transition(FRHITransitionInfo(proxy->grid_cell_data_buffer_.GetUav(), proxy->grid_cell_data_buffer_.GetCurrentRhiState(), proxy->grid_cell_data_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
}

		// グリッドセル情報を利用したエンティティ更新
{
RHICmdList.Transition(FRHITransitionInfo(proxy->entity_base_info_buffer_.GetUav(), proxy->entity_base_info_buffer_.GetCurrentRhiState(), proxy->entity_base_info_buffer_.TransitionRhiState(ERHIAccess::UAVCompute)));


TShaderMapRef<FComputeShaderGrassUpdateWithGridCellCS> cs(GlobalShaderMap);

#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
// UE5.1から非推奨となった関数を置き換え.
SetComputePipelineState(RHICmdList, cs.GetComputeShader());
#else
RHICmdList.SetComputeShader(cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
cs->SetParameters(RHICmdList,
	*proxy->component_->GetCurrentFrameDeltaSec(true), FVector3f(sim_space_aabb.Min), FVector3f(sim_space_aabb.Max), sim_space_cell_count, sim_space_cell_size_inv,
	proxy->sorted_hash_buffer_.GetSrv(), proxy->sorted_hash_exclusive_scan.GetSrv(), proxy->grid_cell_data_buffer_.GetSrv(), proxy->entity_base_info_buffer_.GetUav()
);
#else
// デルタタイム
SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_delta_sec, *proxy->component_->GetCurrentFrameDeltaSec(true));

// シミュレーション空間情報
SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_sim_space_aabb_min, FVector3f(sim_space_aabb.Min));
SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_sim_space_aabb_max, FVector3f(sim_space_aabb.Max));
SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_sim_space_cell_count, sim_space_cell_count);
SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_sim_space_cell_size_inv, sim_space_cell_size_inv);

// UAV
SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->instance_uav_, proxy->entity_base_info_buffer_.GetUav());
// SRV
SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->hash_srv_, proxy->sorted_hash_buffer_.GetSrv());
SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->hash_kind_scan_srv_, proxy->sorted_hash_exclusive_scan.GetSrv());
SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->gridcell_data_srv_, proxy->grid_cell_data_buffer_.GetSrv());
#endif

// Dispatch.
const int CsThreadCount = cs->CS_DISPATCH_THREAD_COUNT();
DispatchComputeShader(RHICmdList, cs, (proxy->max_entity_ + (CsThreadCount - 1)) / CsThreadCount, 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
cs->ResetParameters(RHICmdList);
#else
// UAVリセット
SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->instance_uav_, nullptr);
// SRVリセット
SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->hash_srv_, nullptr);
SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->hash_kind_scan_srv_, nullptr);
SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->gridcell_data_srv_, nullptr);
#endif


RHICmdList.Transition(FRHITransitionInfo(proxy->entity_base_info_buffer_.GetUav(), proxy->entity_base_info_buffer_.GetCurrentRhiState(), proxy->entity_base_info_buffer_.TransitionRhiState(ERHIAccess::SRVCompute)));
}

		// 頂点バッファ作成
{
RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.PositionVertexBuffer.GetUav(), proxy->buffers_.PositionVertexBuffer.GetCurrentRhiState(), proxy->buffers_.PositionVertexBuffer.TransitionRhiState(ERHIAccess::UAVCompute)));
RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.ColorVertexBuffer.GetUav(), proxy->buffers_.ColorVertexBuffer.GetCurrentRhiState(), proxy->buffers_.ColorVertexBuffer.TransitionRhiState(ERHIAccess::UAVCompute)));
RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.TangentsVertexBuffer.GetUav(), proxy->buffers_.TangentsVertexBuffer.GetCurrentRhiState(), proxy->buffers_.TangentsVertexBuffer.TransitionRhiState(ERHIAccess::UAVCompute)));
RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.TexcoordVertexBuffer.GetUav(), proxy->buffers_.TexcoordVertexBuffer.GetCurrentRhiState(), proxy->buffers_.TexcoordVertexBuffer.TransitionRhiState(ERHIAccess::UAVCompute)));
RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.IndexBuffer.GetUav(), proxy->buffers_.IndexBuffer.GetCurrentRhiState(), proxy->buffers_.IndexBuffer.TransitionRhiState(ERHIAccess::UAVCompute)));

	{
	const auto entity_mesh_info = FUintVector4(proxy->num_entity_vertex_, proxy->num_entity_triangle_, 0, 0);


	// 頂点バッファをCSで書き換え
	TShaderMapRef<FComputeShaderGrassGenerateMeshCS> cs(GlobalShaderMap);

#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
	// UE5.1から非推奨となった関数を置き換え.
	SetComputePipelineState(RHICmdList, cs.GetComputeShader());
#else
	RHICmdList.SetComputeShader(cs.GetComputeShader());
#endif

#if NGL_REPLACE_SHADER_PARAM_METHOD
	cs->SetParameters(RHICmdList,
		entity_mesh_info,
		proxy->sorted_hash_buffer_.GetSrv(),
		proxy->entity_base_info_buffer_.GetSrv(),

		proxy->buffers_.PositionVertexBuffer.GetUav(),
		proxy->buffers_.ColorVertexBuffer.GetUav(),
		proxy->buffers_.TexcoordVertexBuffer.GetUav(),
		proxy->buffers_.TangentsVertexBuffer.GetUav(),
		proxy->buffers_.IndexBuffer.GetUav()
		);
#else
	// 1要素の頂点数, トライアングル数.
	SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->cb_entity_mesh_info_, entity_mesh_info);

	// set SRV
	SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->instance_srv_, proxy->entity_base_info_buffer_.GetSrv());
	SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->hash_srv_, proxy->sorted_hash_buffer_.GetSrv());

	// set UAV
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->vtx_pos_buffer_, proxy->buffers_.PositionVertexBuffer.GetUav());
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->vtx_color_buffer_, proxy->buffers_.ColorVertexBuffer.GetUav());
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->vtx_uv_buffer_, proxy->buffers_.TexcoordVertexBuffer.GetUav());
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->vtx_tangent_space_buffer_, proxy->buffers_.TangentsVertexBuffer.GetUav());
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->index_buffer_, proxy->buffers_.IndexBuffer.GetUav());
#endif

	// Dispatch. 本来はシェーダ側スレッド数と必要なスレッド数から適切にグループ数を設定する
	const int CsThreadCount = cs->CS_DISPATCH_THREAD_COUNT();
	DispatchComputeShader(RHICmdList, cs, static_cast<unsigned int>((proxy->max_entity_ + CsThreadCount - 1) / CsThreadCount), 1, 1);

#if NGL_REPLACE_SHADER_PARAM_METHOD
	cs->ResetParameters(RHICmdList);
#else
	// リソースリセットしてみる. これが必須. これやらないと4つ目以降のUAV書き込みが反映されない(というかゼロ書き込み)になる.
	// 多分UAVとして設定した状態でメッシュ描画のリソースとして利用しようとして競合してたっぽいかな.
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->vtx_pos_buffer_, nullptr);
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->vtx_color_buffer_, nullptr);
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->vtx_uv_buffer_, nullptr);
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->vtx_tangent_space_buffer_, nullptr);
	SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->index_buffer_, nullptr);

	SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->instance_srv_, nullptr);
	SetSRVParameter(RHICmdList, cs.GetComputeShader(), cs->hash_srv_, nullptr);
#endif
	}

RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.PositionVertexBuffer.GetUav(), proxy->buffers_.PositionVertexBuffer.GetCurrentRhiState(), proxy->buffers_.PositionVertexBuffer.TransitionRhiState(ERHIAccess::VertexOrIndexBuffer)));
RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.ColorVertexBuffer.GetUav(), proxy->buffers_.ColorVertexBuffer.GetCurrentRhiState(), proxy->buffers_.ColorVertexBuffer.TransitionRhiState(ERHIAccess::VertexOrIndexBuffer)));
RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.TangentsVertexBuffer.GetUav(), proxy->buffers_.TangentsVertexBuffer.GetCurrentRhiState(), proxy->buffers_.TangentsVertexBuffer.TransitionRhiState(ERHIAccess::VertexOrIndexBuffer)));
RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.TexcoordVertexBuffer.GetUav(), proxy->buffers_.TexcoordVertexBuffer.GetCurrentRhiState(), proxy->buffers_.TexcoordVertexBuffer.TransitionRhiState(ERHIAccess::VertexOrIndexBuffer)));
RHICmdList.Transition(FRHITransitionInfo(proxy->buffers_.IndexBuffer.GetUav(), proxy->buffers_.IndexBuffer.GetCurrentRhiState(), proxy->buffers_.IndexBuffer.TransitionRhiState(ERHIAccess::VertexOrIndexBuffer)));
}

		// 初回の描画更新が終わったことを設定
		proxy->is_first_render_update_completed_ = true;
	}

protected:
	const UComputeShaderMeshComponent* component_;
	UMaterialInterface *material_;
	/** The view relevance for all the static mesh's materials. */
	FMaterialRelevance MaterialRelevance;

	// エンティティ総数. 二の冪であるように調整される.
	int		max_entity_ = 1;
	// エンティティ一つに割り当てる頂点数.
	int		num_entity_vertex_ = 3;
	// エンティティ一つに割り当てるトライアングル数
	int		num_entity_triangle_ = 1;

	// シミュレーション空間情報
	float	sim_space_cell_size_ = 250.0f;


	// 初回の描画アップデートが実行されてソート済みバッファやカウントバッファが更新されているか.
	// 追加シェーダ等のSRVとして上記バッファを利用するため1度でもソートなどが実行されていないといけない.
	bool	is_first_render_update_completed_ = false;

	// 頂点バッファなど
	ngl_procedual_mesh::FNagaProceduralMeshBuffers buffers_;
	// コンピュート実体一つ一つの情報を格納したバッファ
	ngl::gpgpu::FStructuredBufferResource<GrassEntityInfo>	entity_base_info_buffer_;

	// ソート済みバッファの未使用要素の個数などを格納するバッファ.
	ngl::gpgpu::FStructuredBufferResource<GrassEntityCountInfo>	sorted_entity_counter_buffer_;

	// インスタンス追加用情報バッファ
	ngl::gpgpu::FStructuredBufferResource<ngl::gpgpu::GrassEntityAppendInfo>	entity_append_buffer_;


	// ここからは汎用のソートやソート済みバッファの情報取得用
	// 転置シェーダのために入出力二つのバッファが必要.
	ngl::gpgpu::FStructuredBufferResource<ngl::gpgpu::UintVec2>			sorted_hash_buffer_;

	// タイルベース転置BitonicSortオブジェクト
	ngl::gpgpu::GpuSimpleOutOfPlaceTiledTransposeBitonicSortUint2	simple_tiled_transposed_bitonic_sorter_;
	// Dispatch数削減BitonicSortオブジェクト
	ngl::gpgpu::GpuDispatchReduceBitonicSortUint2					dispatch_reduce_bitonic_sorter_;


	// Exclusive Scan生成用の作業バッファ
	// ソート済みハッシュの変化位置に1それ以外0なバッファを生成したりグループ単位Scanバッファの各グループ担当分の最後の要素を書き込んで全体Scanの入力とするバッファに使う.
	ngl::gpgpu::FStructuredBufferResource<uint32>			sorted_hash_exclusive_scan_work;
	ngl::gpgpu::FStructuredBufferResource<uint32>			sorted_hash_exclusive_scan;
	// ブロック単位Scan時にブロック毎の合計値を書き込むバッファ. 作業用に二つ
	ngl::gpgpu::FStructuredBufferResource<uint32>			sorted_hash_sums_exclusive_scan0_;
	ngl::gpgpu::FStructuredBufferResource<uint32>			sorted_hash_sums_exclusive_scan1_;

	// 有効なハッシュ値に関する情報のバッファ
	// 0番要素に有効なハッシュ値の個数N
	// 1番以降にN個の有効なハッシュの値とそのハッシュ値が開始するsorted_hash_buffer_上のインデックスが格納される
	// 0番に有効要素数を格納しているのは二分探索時に必要な情報を集約するため.
	ngl::gpgpu::FStructuredBufferResource<ngl::gpgpu::UintVec2>			enable_hash_buffer_;

	// グリッドセルの情報
	ngl::gpgpu::FStructuredBufferResource<GridCellInfo>		grid_cell_data_buffer_;

	// ソート済みバッファHashの各ビットMSB開始インデックスバッファ. 32要素. 主に末尾の無効要素へのアクセスのため.
	ngl::gpgpu::FStructuredBufferResource<uint32>			sorted_hash_bit_msb_index_buffer_;



};


UComputeShaderMeshComponent::UComputeShaderMeshComponent()
{
	// Tick有効
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
	// Tickグループ指定. 要検証.
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;

	delta_sec_[0] = delta_sec_[1] = 1.0f / 60.0f;
}

FPrimitiveSceneProxy* UComputeShaderMeshComponent::CreateSceneProxy()
{
	// 自前のProxyを生成
	sceneProxy_ = new FComputeShaderMeshProxy(this);
	return sceneProxy_;
}

// Proxy更新 EnqueueDispatchComputeShader();
void UComputeShaderMeshComponent::SendRenderDynamicData_Concurrent()
{
	if (sceneProxy_)
	{
		// Proxy側の描画用処理と描画コマンド発行
		sceneProxy_->EnqueueDispatchComputeShader();
	}
}
void UComputeShaderMeshComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// フリップ
	flip_index_ = 1 - flip_index_;

	// 0バッファのほうはクリア
	obstacle_sphere_buffer_[flip_index_].Empty(obstacle_sphere_buffer_[flip_index_].Max());
	destruction_sphere_buffer_[flip_index_].Empty(destruction_sphere_buffer_[flip_index_].Max());
	emission_buffer_[flip_index_].Empty(emission_buffer_[flip_index_].Max());

	*GetCurrentFrameDeltaSec() = DeltaTime;

	// SendRenderDynamicData_Concurrent の呼び出しをリクエスト
	MarkRenderDynamicDataDirty();
}

UMaterialInterface* UComputeShaderMeshComponent::GetMaterial(int32 ElementIndex) const
{
	if (ElementIndex == 0)
	{
		return material_;
	}
	else
	{
		return nullptr;
	}
}

void UComputeShaderMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (material_)
	{
		OutMaterials.Add(material_);
	}
}

int32 UComputeShaderMeshComponent::GetNumMaterials()const
{
	if (material_)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
void UComputeShaderMeshComponent::SetMaterial(int32 ElementIndex, class UMaterialInterface* Material)
{
	if (ElementIndex == 0)
	{
		material_ = Material;
		// MaterialInstanceDynamicの設定などの設定でDirtyにして更新を予約しておかないとその後のマテリアルパラメータ設定などが反映されない.
		MarkRenderStateDirty();
	}
}
void UComputeShaderMeshComponent::SetMaterialByName(FName MaterialSlotName, class UMaterialInterface* Material)
{
	SetMaterial(0, Material);
}
FMaterialRelevance UComputeShaderMeshComponent::GetMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const
{
	// Combine the material relevance for all materials.
	FMaterialRelevance Result;
	for (int32 ElementIndex = 0; ElementIndex < GetNumMaterials(); ElementIndex++)
	{
		UMaterialInterface const* MaterialInterface = GetMaterial(ElementIndex);
		if (!MaterialInterface)
		{
			MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		Result |= MaterialInterface->GetRelevance_Concurrent(InFeatureLevel);
	}
	return Result;
}


FBoxSphereBounds UComputeShaderMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	float default_bound = 50000.0f;// カリングはComputeで自前で実行するのでなるべくシステムでのカリングをされないように大きくする.


#if NGL_PROCEDURAL_MESH_USE_WORLD_SPACE_GEOMETRY
	FTransform ComponentTransform = FTransform::Identity;
#else
	FTransform ComponentTransform = LocalToWorld;
#endif

	return FBoxSphereBounds(ComponentTransform.GetLocation(), FVector(default_bound, default_bound, default_bound), default_bound);
}


void UComputeShaderMeshComponent::AddCurrentFrameObstacleSphere(const FVector& pos, float radius)
{
	// Actorローカル
	auto localPos = pos;
#if NGL_PROCEDURAL_MESH_USE_WORLD_SPACE_GEOMETRY
#else
	if ( auto actor = GetOwner())
	{
		localPos = actor->GetTransform().InverseTransformPosition(localPos);
	}
#endif

	obstacle_sphere_buffer_[flip_index_].Add( FVector4(localPos, radius));
}

void UComputeShaderMeshComponent::AddCurrentFrameDestructionSphere(const FVector& pos, float radius)
{
	// Actorローカル
	auto localPos = pos;
#if NGL_PROCEDURAL_MESH_USE_WORLD_SPACE_GEOMETRY
#else
	if (auto actor = GetOwner())
	{
		localPos = actor->GetTransform().InverseTransformPosition(localPos);
	}
#endif

	destruction_sphere_buffer_[flip_index_].Add(FVector4(localPos, radius));
}

void UComputeShaderMeshComponent::AddCurrentFrameEmissionBuffer(const FVector& pos, const FVector& dir, float length, float life_sec_init)
{
	// Actorローカル
	auto localPos = pos;
	auto localDir = dir;
#if NGL_PROCEDURAL_MESH_USE_WORLD_SPACE_GEOMETRY
#else
	if (auto actor = GetOwner())
	{
		localPos = actor->GetTransform().InverseTransformPosition(localPos);
		localDir = actor->GetTransform().GetRotation().UnrotateVector(localDir);
	}
#endif

	ngl::gpgpu::GrassEntityAppendInfo newObj;
	newObj.pos0 = FVector4f(localPos.X, localPos.Y, localPos.Z);
	newObj.base_dir = FVector4f(localDir.X, localDir.Y, localDir.Z, length);
	newObj.life_sec_init = life_sec_init;

	emission_buffer_[flip_index_].Add(newObj);
}

