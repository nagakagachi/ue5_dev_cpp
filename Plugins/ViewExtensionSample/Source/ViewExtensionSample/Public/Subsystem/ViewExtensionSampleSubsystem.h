#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Subsystems/EngineSubsystem.h"
#include "Engine/EngineBaseTypes.h"



#include "ViewExtensionSampleSubsystem.generated.h"

UCLASS()
class UViewExtensionSampleSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:

	// Subsystem Init/Deinit
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
public:

private:
	TSharedPtr< class FViewExtensionSampleVe, ESPMode::ThreadSafe > p_view_extension;

public:
	friend class FViewExtensionSampleVe;
};