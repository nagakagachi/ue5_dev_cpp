#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Subsystems/EngineSubsystem.h"
#include "Engine/EngineBaseTypes.h"



#include "ViewExtensionSampleSubsystem.generated.h"


class UViewExtensionSampleSubsystem;

// Subsystemへのパラメータ設定用Actor.
UCLASS()
class AViewExtensionSampleControlActor : public AActor
{
	GENERATED_BODY()
protected:
	virtual void BeginPlay() override;
	
public:
	AViewExtensionSampleControlActor();
	
	virtual bool ShouldTickIfViewportsOnly() const override { return true; };
	virtual void Tick(float DeltaSeconds) override;

	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="ViewExtensionSample")
	bool enable_history_test = false;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="ViewExtensionSample")
	bool enable_shadingmodel_only_filter = false;


	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="VoronoiEffectTest")
	bool enable_voronoi_test = true;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="VoronoiEffectTest")
	float depth_edge_coef = 0.02f;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="VoronoiEffectTest")
	bool enable_voronoi_tile_cell = true;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="VoronoiEffectTest", meta = (ClampMin = 0))
	int edge_debug_view = 0;

	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="AnisoKuwahara")
	bool enable_aniso_kuwahara = false;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="AnisoKuwahara")
	float aniso_kuwahara_aniso_control = 1.0f;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="AnisoKuwahara")
	float aniso_kuwahara_hardness = 8.0f;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="AnisoKuwahara")
	float aniso_kuwahara_sharpness = 8.0f;

	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="LensGhost")
	bool enable_lensh_ghost = false;
};

// ViewExtensionインスタンス管理用Subsystem.
UCLASS()
class UViewExtensionSampleSubsystem : public UEngineSubsystem /*UWorldSubsystem*/
{
	GENERATED_BODY()
public:

	// Subsystem Init/Deinit
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	TSharedPtr< class FViewExtensionSampleVe, ESPMode::ThreadSafe > p_view_extension;

public:
	friend class FViewExtensionSampleVe;

	bool enable_voronoi_test = false;
	float depth_edge_coef = 0.02f;
	bool enable_voronoi_tile_cell = true;
	int edge_debug_view = 0;

	bool enable_history_test = false;
	
	bool enable_shadingmodel_only_filter = false;

	bool enable_aniso_kuwahara = false;
	float aniso_kuwahara_aniso_control = 1.0f;
	float aniso_kuwahara_hardness = 8.0f;
	float aniso_kuwahara_sharpness = 8.0f;

	bool enable_lensh_ghost = true;
};