// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "gpu_compute_shader_grass_component.generated.h"

namespace ngl::gpgpu
{
	// 追加時用の構造体
	struct GrassEntityAppendInfo
	{
		FVector4f base_dir = FVector4f(0, 0, 1, 1);; // 位置0から子への基準向きと長さ
		FVector4f pos0 = FVector4f(0, 0, 0, 0);; // 位置
		float life_sec_init = 0.0f;

		uint32 pad[3]; // 16byte padding
	};
}

/**
 *
 */
UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent))
class UComputeShaderMeshComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UComputeShaderMeshComponent();

	// 描画側におけるこのクラスの分身であるProxtを生成
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	// マテリアル関係
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const override;
	virtual int32 GetNumMaterials() const override;

	virtual void SetMaterial(int32 ElementIndex, class UMaterialInterface* Material) override;
	virtual void SetMaterialByName(FName MaterialSlotName, class UMaterialInterface* Material) override;
	/** Accesses the scene relevance information for the materials applied to the mesh. Valid from game thread only. */
	FMaterialRelevance GetMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const;

	// バウンディングボリューム関係
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	// Proxy更新
	virtual void SendRenderDynamicData_Concurrent() override;

	// マテリアル
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ProceduralGrass)
	UMaterialInterface* material_;

	// 最大エンティティ要求数. この数値以上の最小の2の冪が採用される.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ProceduralGrass)
	int grass_max_count_ = 1000;

	// シミュレーション空間の1セルのサイズ. 流体シミュレーションなどの近傍探索時に空間がこのサイズの立方体に分割される.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ProceduralGrass)
	float grass_simulation_space_cell_width_ = 250.0f;

	UFUNCTION(BlueprintCallable)
	void AddCurrentFrameObstacleSphere( const FVector& pos, float radius );
	UFUNCTION(BlueprintCallable)
	void AddCurrentFrameDestructionSphere(const FVector& pos, float radius);

	const TArray<FVector4>* GetCurrentFrameObstacleSphereBuffer(bool is_render_thread = false) const 
	{
		return &obstacle_sphere_buffer_[is_render_thread ? (1 - flip_index_) : (flip_index_)];
	}
	const TArray<FVector4>* GetCurrentFrameDestructionSphereBuffer(bool is_render_thread = false) const
	{
		return &destruction_sphere_buffer_[is_render_thread ? (1 - flip_index_) : (flip_index_)];
	}

	UFUNCTION(BlueprintCallable)
	void AddCurrentFrameEmissionBuffer(const FVector& pos, const FVector& dir, float length = 100.0f, float life_sec_init = 1.0f);

	const TArray<ngl::gpgpu::GrassEntityAppendInfo>* GetCurrentFrameEmissionBuffer(bool is_render_thread = false) const
	{
		return &emission_buffer_[is_render_thread ? (1 - flip_index_) : (flip_index_)];
	}

	float* GetCurrentFrameDeltaSec(bool is_render_thread = false)
	{
		return &delta_sec_[is_render_thread ? (1 - flip_index_) : (flip_index_)];
	}
	const float* GetCurrentFrameDeltaSec(bool is_render_thread = false) const
	{
		return &delta_sec_[is_render_thread ? (1 - flip_index_) : (flip_index_)];
	}

private:
	class FComputeShaderMeshProxy* sceneProxy_ = nullptr;

	// ゲーム情報
	int flip_index_ = 0;

	// デルタ時間
	float				delta_sec_[2];

	// 障害物球の位置と半径
	TArray<FVector4>	obstacle_sphere_buffer_[2];
	TArray<FVector4>	destruction_sphere_buffer_[2];
	// エミッタ用
	TArray<ngl::gpgpu::GrassEntityAppendInfo>	emission_buffer_[2];
};
