// Fill out your copyright notice in the Description page of Project Settings.


#include "gpu_compute_shader_mesh_sample_component.h"

#include "Math/Vector2D.h"

#include "RHICommandList.h"
#include "PipelineStateCache.h"
// shader
#include "GlobalShader.h"

#include "RHIStaticStates.h"
#include "SceneUtils.h"
#include "SceneInterface.h"

#include "Materials/MaterialInterface.h"
#include "Rendering/ColorVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"

#include "gpu_compute_shader_vertex_buffer.h"

#include "Rendering/StaticMeshVertexDataInterface.h"

#include "PrimitiveSceneProxy.h"
#include "DynamicMeshBuilder.h"

namespace ngl::gpgpu
{
}

/** プロシージャルメッシュ用バッファ群 */
class FNagaSampleProceduralMeshBuffers
{
public:

	// RHI頂点バッファ
	ngl::gpgpu::FComputeShaderPositionVertexBufferT PositionVertexBuffer;

	// RHI頂点バッファ
	FColorVertexBuffer ColorVertexBuffer;

	// RHI頂点バッファ(TexCoordとTangent)
	FStaticMeshVertexBuffer StaticMeshVertexBuffer;

	// RHIインデックスバッファ
	FDynamicMeshIndexBuffer32 IndexBuffer;

	// 頂点ファクトリ
	FLocalVertexFactory VertexFactory;

	FNagaSampleProceduralMeshBuffers(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FNagaSampleProceduralMeshBuffers")
	{}

	~FNagaSampleProceduralMeshBuffers()
	{
		PositionVertexBuffer.ReleaseResource();
		StaticMeshVertexBuffer.ReleaseResource();
		ColorVertexBuffer.ReleaseResource();
		IndexBuffer.ReleaseResource();
		VertexFactory.ReleaseResource();
	}
};


// Shader
class FSampleComputeShaderMeshShader : public FGlobalShader
{
public:
	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// Useful when adding a permutation of a particular shader
		return true;
	}

	FSampleComputeShaderMeshShader() {}

	// シェーダパラメータと変数をバインド
	FSampleComputeShaderMeshShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FGlobalShader(Initializer)
	{
	}

};
class FSampleComputeShaderMeshShaderCS : public FSampleComputeShaderMeshShader
{
	DECLARE_SHADER_TYPE(FSampleComputeShaderMeshShaderCS, Global);

public:

	/** Default constructor. */
	FSampleComputeShaderMeshShaderCS() {}

	/** Initialization constructor. */
	FSampleComputeShaderMeshShaderCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FSampleComputeShaderMeshShader(Initializer)
	{
		// シェーダパラメータと変数をバインド
		uniformParameter0_.Bind(Initializer.ParameterMap, TEXT("uniformParameter0"));

		vtx_pos_buffer_.Bind(Initializer.ParameterMap, TEXT("vtx_pos_buffer"));
	}

	// シェーダパラメータ変数群
	LAYOUT_FIELD(FShaderParameter, uniformParameter0_);

	LAYOUT_FIELD(FShaderResourceParameter, vtx_pos_buffer_);
};
// シェーダコードとGlobalShaderクラスを関連付け
IMPLEMENT_SHADER_TYPE(, FSampleComputeShaderMeshShaderCS, TEXT("/Plugin/NagaExperiment/Private/sample_compute_mesh.usf"), TEXT("MainCS"), SF_Compute)


// プロシージャルメッシュプロキシ
class FSampleComputeShaderMeshProxy : public FPrimitiveSceneProxy
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
	FSampleComputeShaderMeshProxy(const USampleComputeShaderMeshComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
		, component_(InComponent)
		, material_(InComponent->material_)
		, buffers_(ERHIFeatureLevel::SM5)
	{

		{
			bool enable_uav = true;
			bool cpu_access = false;
			// 頂点データ
			TArray<FVector> initial_vtx_pos;
			float initial_plane_half_width = 250.0f;
			initial_vtx_pos.Add(FVector(-initial_plane_half_width, -initial_plane_half_width, 0));
			initial_vtx_pos.Add(FVector(-initial_plane_half_width, initial_plane_half_width, 0));
			initial_vtx_pos.Add(FVector(initial_plane_half_width, initial_plane_half_width, 0));
			initial_vtx_pos.Add(FVector(initial_plane_half_width, -initial_plane_half_width, 0));
			// Color
			TArray<FColor> initial_vtx_color;
			initial_vtx_color.Add(FColor::Red);
			initial_vtx_color.Add(FColor::Green);
			initial_vtx_color.Add(FColor::Blue);
			initial_vtx_color.Add(FColor::White);
			// UVとTangent
			TArray<FStaticMeshBuildVertex> initial_vtx_attr;
			initial_vtx_attr.SetNumUninitialized(initial_vtx_pos.Num());
			initial_vtx_attr[0].UVs[0] = FVector2f(0, 0);
			initial_vtx_attr[0].TangentX = FVector3f(1, 0, 0);
			initial_vtx_attr[0].TangentY = FVector3f(0, 1, 0);
			initial_vtx_attr[0].TangentZ = FVector3f(0, 0, 1);
			initial_vtx_attr[1].UVs[0] = FVector2f(1, 0);
			initial_vtx_attr[1].TangentX = FVector3f(1, 0, 0);
			initial_vtx_attr[1].TangentY = FVector3f(0, 1, 0);
			initial_vtx_attr[1].TangentZ = FVector3f(0, 0, 1);
			initial_vtx_attr[2].UVs[0] = FVector2f(1, 1);
			initial_vtx_attr[2].TangentX = FVector3f(1, 0, 0);
			initial_vtx_attr[2].TangentY = FVector3f(0, 1, 0);
			initial_vtx_attr[2].TangentZ = FVector3f(0, 0, 1);
			initial_vtx_attr[3].UVs[0] = FVector2f(0, 0);
			initial_vtx_attr[3].TangentX = FVector3f(1, 0, 0);
			initial_vtx_attr[3].TangentY = FVector3f(0, 1, 0);
			initial_vtx_attr[3].TangentZ = FVector3f(0, 0, 1);

			// バッファオブジェクトの初期データにセット
			//buffers_.PositionVertexBuffer.Init(initial_vtx_pos, enable_uav, cpu_access);
			buffers_.PositionVertexBuffer.Init(4, enable_uav);
			buffers_.ColorVertexBuffer.InitFromColorArray(initial_vtx_color.GetData(), initial_vtx_color.Num(), initial_vtx_color.GetTypeSize(), cpu_access);
			buffers_.StaticMeshVertexBuffer.Init(initial_vtx_attr, 1, cpu_access);

			// インデックス
			buffers_.IndexBuffer.Indices.Add(0);
			buffers_.IndexBuffer.Indices.Add(1);
			buffers_.IndexBuffer.Indices.Add(2);
			buffers_.IndexBuffer.Indices.Add(0);
			buffers_.IndexBuffer.Indices.Add(2);
			buffers_.IndexBuffer.Indices.Add(3);

			// RenderThreadで描画リソースを初期化するようにリクエスト
			FNagaSampleProceduralMeshBuffers* bufferHolder = &buffers_;
			uint32 LightMapIndex = 0;
			ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
				[bufferHolder, LightMapIndex](FRHICommandListImmediate& RHICmdList)
			{
				// 頂点バッファ初期化
				bufferHolder->PositionVertexBuffer.InitResource(RHICmdList);
				bufferHolder->StaticMeshVertexBuffer.InitResource(RHICmdList);
				bufferHolder->ColorVertexBuffer.InitResource(RHICmdList);

				// インデックスバッファの初期化
				bufferHolder->IndexBuffer.InitResource(RHICmdList);

				// 頂点ファクトリのセットアップ
				auto vtxFactoryPtr = &bufferHolder->VertexFactory;
				FLocalVertexFactory::FDataType Data;
				bufferHolder->PositionVertexBuffer.BindVertexBuffer(vtxFactoryPtr, Data);
				bufferHolder->ColorVertexBuffer.BindColorVertexBuffer(vtxFactoryPtr, Data);
				bufferHolder->StaticMeshVertexBuffer.BindTangentVertexBuffer(vtxFactoryPtr, Data);
				bufferHolder->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(vtxFactoryPtr, Data);
				bufferHolder->StaticMeshVertexBuffer.BindLightMapVertexBuffer(vtxFactoryPtr, Data, LightMapIndex);
				vtxFactoryPtr->SetData(RHICmdList, Data);
				// ファクトリ初期化
				vtxFactoryPtr->InitResource(RHICmdList);
			});
		}
	}
	~FSampleComputeShaderMeshProxy()
	{
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
				GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
				FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();

#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
				DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity);
#else
				DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);
#endif

				BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

				// 描画頂点の範囲情報等
				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = buffers_.IndexBuffer.Indices.Num() / 3;
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
		bool bVisible = true;
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = IsShadowCast(View);

		return Result;
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
		FSampleComputeShaderMeshProxy* proxy,
		ERHIFeatureLevel::Type FeatureLevel)
	{
		check(IsInRenderingThread());

#if WANTS_DRAW_MESH_EVENTS
		FString EventName("ProceduralMesh");
		//SCOPED_DRAW_EVENTF(RHICmdList, SceneCapture, TEXT("DispatchComputeShader_RenderThread %s"), *EventName);
#else
		//SCOPED_DRAW_EVENT(RHICmdList, DispatchComputeShader_RenderThread);
#endif

		auto* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FSampleComputeShaderMeshShaderCS> cs(GlobalShaderMap);

#if ((5<=ENGINE_MAJOR_VERSION) && (1<=ENGINE_MINOR_VERSION)) || (5<ENGINE_MAJOR_VERSION)
		SetComputePipelineState(RHICmdList, cs.GetComputeShader());
#else
		RHICmdList.SetComputeShader(cs.GetComputeShader());
#endif

#if ((5<=ENGINE_MAJOR_VERSION) && (3<=ENGINE_MINOR_VERSION))
		// UE5.3
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
		// set param
		SetShaderValue(BatchedParameters, cs->uniformParameter0_, FVector4f(proxy->component_->GetWorld()->TimeSeconds, 0, 0, 0));

		// set UAV
		SetUAVParameter(BatchedParameters, cs->vtx_pos_buffer_, proxy->buffers_.PositionVertexBuffer.GetUav());
	
		RHICmdList.SetBatchedShaderParameters(cs.GetComputeShader(), BatchedParameters);
#else
		// set param
		SetShaderValue(RHICmdList, cs.GetComputeShader(), cs->uniformParameter0_, FVector4f(proxy->component_->GetWorld()->TimeSeconds, 0, 0, 0));

		// set UAV
		SetUAVParameter(RHICmdList, cs.GetComputeShader(), cs->vtx_pos_buffer_, proxy->buffers_.PositionVertexBuffer.GetUav());
#endif
		DispatchComputeShader(RHICmdList, cs, 1, 1, 1);
	}

protected:
	const USampleComputeShaderMeshComponent* component_;
	UMaterialInterface *material_;

	FNagaSampleProceduralMeshBuffers buffers_;
};


USampleComputeShaderMeshComponent::USampleComputeShaderMeshComponent()
{
	// Tick有効
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
	// Tickグループ指定. 要検証.
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
}

FPrimitiveSceneProxy* USampleComputeShaderMeshComponent::CreateSceneProxy()
{
	// 自前のProxyを生成
	sceneProxy_ = new FSampleComputeShaderMeshProxy(this);
	return sceneProxy_;
}

// Proxy更新 EnqueueDispatchComputeShader();
void USampleComputeShaderMeshComponent::SendRenderDynamicData_Concurrent()
{
	if (sceneProxy_)
	{
		// Proxy側の描画用処理と描画コマンド発行
		sceneProxy_->EnqueueDispatchComputeShader();
	}
}
void USampleComputeShaderMeshComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// SendRenderDynamicData_Concurrent の呼び出しをリクエスト
	MarkRenderDynamicDataDirty();
}

UMaterialInterface* USampleComputeShaderMeshComponent::GetMaterial(int32 ElementIndex) const
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

void USampleComputeShaderMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (material_)
	{
		OutMaterials.Add(material_);
	}
}

int32 USampleComputeShaderMeshComponent::GetNumMaterials()const
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


FBoxSphereBounds USampleComputeShaderMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	float default_bound = 1000.0f;
	return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector(default_bound, default_bound, default_bound), default_bound);
}
