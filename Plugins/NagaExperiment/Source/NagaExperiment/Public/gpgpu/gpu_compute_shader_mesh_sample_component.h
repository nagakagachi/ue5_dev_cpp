// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "gpu_compute_shader_mesh_sample_component.generated.h"

namespace naga::gpgpu
{
}
/**
 *
 */
UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent))
class USampleComputeShaderMeshComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	USampleComputeShaderMeshComponent();

	// 描画側におけるこのクラスの分身であるProxtを生成
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	// マテリアル関係
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const override;
	virtual int32 GetNumMaterials() const override;

	// バウンディングボリューム関係
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	// Proxy更新
	virtual void SendRenderDynamicData_Concurrent() override;



	// マテリアルを設定できるようにしておく
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Material)
	UMaterialInterface* material_;

private:
	class FSampleComputeShaderMeshProxy* sceneProxy_ = nullptr;
};
