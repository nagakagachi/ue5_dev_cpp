

#include "Subsystem/ViewExtensionSampleSubsystem.h"

#include "K2Node_GetSubsystem.h"
#include "Rendering/ViewExtensionSampleVe.h"

AViewExtensionSampleControlActor::AViewExtensionSampleControlActor()
{
	PrimaryActorTick.bCanEverTick = true;
}
void AViewExtensionSampleControlActor::BeginPlay()
{
	Super::BeginPlay();
}
void AViewExtensionSampleControlActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	{
		auto* subsystem = GEngine->GetEngineSubsystem<UViewExtensionSampleSubsystem>();
		if(subsystem)
		{
			subsystem->enable_gbuffer_modify = enable_gbuffer_modify;
		}
	}
}


void UViewExtensionSampleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	p_view_extension = FSceneViewExtensions::NewExtension<FViewExtensionSampleVe>(this);

	UE_LOG(LogTemp, Log, TEXT("[UViewExtensionSampleSubsystem] Initialize %p\n"), this);
}

void UViewExtensionSampleSubsystem::Deinitialize()
{
	UE_LOG(LogTemp, Log, TEXT("[UViewExtensionSampleSubsystem] Deinitialize %p\n"), this);

	if(p_view_extension)
	{
		// Prevent this SVE from being gathered, in case it is kept alive by a strong reference somewhere else.
		{
			p_view_extension->IsActiveThisFrameFunctions.Empty();

			FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
			IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
			{
				return TOptional<bool>(false);
			};

			p_view_extension->IsActiveThisFrameFunctions.Add(IsActiveFunctor);
		}

		ENQUEUE_RENDER_COMMAND(ReleaseSVE)([this](FRHICommandListImmediate& RHICmdList)
		{
			// Prevent this SVE from being gathered, in case it is kept alive by a strong reference somewhere else.
			{
				p_view_extension->IsActiveThisFrameFunctions.Empty();

				FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
				IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
				{
					return TOptional<bool>(false);
				};

				p_view_extension->IsActiveThisFrameFunctions.Add(IsActiveFunctor);
			}

			p_view_extension.Reset();
			p_view_extension = nullptr;
		});
	}
	
	// Finish all rendering commands before cleaning up actors.
	FlushRenderingCommands();
	
	Super::Deinitialize();
}
