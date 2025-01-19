// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Containers/BitArray.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
// For Editor
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif


#include "spatial_structure/hierarchical_grid.h"


#include "actor_view_occupancygrid.generated.h"

// 現在のViewportのサイズとカメラ位置,カメラ姿勢を取得.
// EditorModeも対応.
static void GetCurrentViewportInfo(const UWorld* world, FVector2D& out_viewport_size, FVector& out_view_location, FQuat& out_view_quat, float& out_horizontal_fov_radian)
{
	// Get Current View Info (EditorMode, PlayMode).
	auto GetCurrentViewportInfo = [](const UWorld* world, FVector2D& out_viewport_size, FVector& out_view_location, FQuat& out_view_quat, float& out_horizontal_fov_radian)
	{
		if (!world)
			return;

#if WITH_EDITOR
		if (world->WorldType == EWorldType::Editor || world->WorldType == EWorldType::EditorPreview)
		{
			// EditorMode.
			// Use First Editor Viewport.
			for (FLevelEditorViewportClient* level_viewport_clients : GEditor->GetLevelViewportClients())
			{
				if (level_viewport_clients && level_viewport_clients->IsPerspective())
				{
					out_viewport_size = level_viewport_clients->Viewport->GetSizeXY();
					out_view_location = level_viewport_clients->GetViewLocation();
					out_view_quat = level_viewport_clients->GetViewRotation().Quaternion();

					//const auto fov_angle = level_viewport_clients->FOVAngle;
					out_horizontal_fov_radian = FMath::DegreesToRadians(level_viewport_clients->ViewFOV);

					break;
				}
			}
		}
		else
#endif
		{
			// Non EditorMode.
			// Use First PlayerCamera.
			if (auto* camera_manager = UGameplayStatics::GetPlayerCameraManager(world, 0))
			{
				GEngine->GameViewport->GetViewportSize(out_viewport_size);
				out_view_location = camera_manager->GetCameraLocation();
				out_view_quat = camera_manager->GetCameraRotation().Quaternion();

				out_horizontal_fov_radian = FMath::DegreesToRadians(camera_manager->GetFOVAngle());
			}
		}

		return;
	};

	GetCurrentViewportInfo(world, out_viewport_size, out_view_location, out_view_quat, out_horizontal_fov_radian);
}


// ViewからCollisionQueryで収集した情報からOccupancyGrid的な情報を構築するテスト.
UCLASS()
class NAGAEXPERIMENT_API AOccupancyGridTest : public AActor
{
	GENERATED_BODY()

public:
	AOccupancyGridTest();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;


public:
	// デバッグ表示用メッシュなど
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		class UStaticMesh*	testStaticMesh_ = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		class UMaterial*	testMaterial_ = nullptr;


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool debug_dcgrid_ = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool debug_dcgrid_cell_ = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool debug_dcgrid_ptcl_ = true;


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool debug_ocgrid_ = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool debug_draw_ocgrid_ = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool debug_draw_ocgrid_brick_ = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool debug_ocgrid_ptcl_ = false;

private:
	UPROPERTY()
	class UInstancedStaticMeshComponent* ismc_occupancygrid_ = nullptr;
	UPROPERTY()
	class UInstancedStaticMeshComponent* ismc_dcgrid_ = nullptr;

private:
	int debug_raster_index_ = 0;

	naga::SparseGridFluid<1> dcgrid_ = {};

	naga::HierarchicalOccupancyGrid ocgrid_ = {};
};


