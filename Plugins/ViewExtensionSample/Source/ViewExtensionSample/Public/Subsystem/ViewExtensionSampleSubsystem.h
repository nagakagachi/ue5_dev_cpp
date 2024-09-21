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
	bool enable_test_compute = true;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="ViewExtensionSample")
	bool enable_gbuffer_modify = false;

	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="ViewExtensionSample")
	float depth_edge_coef = 0.02f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="ViewExtensionSample")
	bool edge_debug_view = false;
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

	bool enable_test_compute = false;
	bool enable_gbuffer_modify = false;

	float depth_edge_coef = 0.02f;
	bool edge_debug_view = false;
};