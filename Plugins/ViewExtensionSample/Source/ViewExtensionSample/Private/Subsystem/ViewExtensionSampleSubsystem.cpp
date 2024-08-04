

#include "Subsystem/ViewExtensionSampleSubsystem.h"
#include "Rendering/ViewExtensionSampleVe.h"

void UViewExtensionSampleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	p_view_extension = FSceneViewExtensions::NewExtension<FViewExtensionSampleVe>(this);

	UE_LOG(LogTemp, Log, TEXT("[UViewExtensionSampleSubsystem] Initialize\n"));
}

void UViewExtensionSampleSubsystem::Deinitialize()
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

	// Finish all rendering commands before cleaning up actors.
	FlushRenderingCommands();
	
	Super::Deinitialize();
}
