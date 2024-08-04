
#include "Rendering/ViewExtensionSampleVe.h"

#include "DynamicResolutionState.h"
#include "FXRenderingUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "Rendering/ViewExtensionSampleShader.h"

#include "Subsystem/ViewExtensionSampleSubsystem.h"

FViewExtensionSampleVe::FViewExtensionSampleVe(const FAutoRegister& AutoRegister, UViewExtensionSampleSubsystem* InWorldSubsystem)
	: FSceneViewExtensionBase(AutoRegister), WorldSubsystem(InWorldSubsystem)
{
	
}

void FViewExtensionSampleVe::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	// 特になし.
}
void FViewExtensionSampleVe::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	// 特になし.
}
void FViewExtensionSampleVe::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	// 特になし.
}
	
void FViewExtensionSampleVe::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	
	if (IsValid(WorldSubsystem))
	{
	}
	else
	{
		return;
	}

	// チェック.
	Inputs.Validate();

	const FSceneViewFamily& ViewFamily = *View.Family;

	// We need to make sure to take Windows and Scene scale into account.
	float ScreenPercentage = ViewFamily.SecondaryViewFraction;

	if (ViewFamily.GetScreenPercentageInterface())
	{
		DynamicRenderScaling::TMap<float> UpperBounds = ViewFamily.GetScreenPercentageInterface()->GetResolutionFractionsUpperBound();
		ScreenPercentage *= UpperBounds[GDynamicPrimaryResolutionFraction];
	}

	const FIntRect PrimaryViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(View);

	FScreenPassTexture SceneColor((*Inputs.SceneTextures)->SceneColorTexture, PrimaryViewRect);

	if (!SceneColor.IsValid())
	{
		return;
	}

	{
		// Getting material data for the current view.
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		// Reusing the same output description for our back buffer as SceneColor
		FRDGTextureDesc TestOutputDesc = SceneColor.Texture->Desc;

		TestOutputDesc.Format = PF_FloatRGBA;
		FLinearColor ClearColor(0., 0., 0., 0.);
		TestOutputDesc.ClearValue = FClearValueBinding(ClearColor);

		FRDGTexture* OutputRenderTargetTexture = GraphBuilder.CreateTexture(TestOutputDesc, TEXT("ViewExtensionSampleTestOutput"));
		
		//FScreenPassRenderTarget BackBufferRenderTarget = FScreenPassRenderTarget(OutputRenderTargetTexture, SceneColor.ViewRect, ERenderTargetLoadAction::EClear);
		FScreenPassRenderTarget SceneColorRenderTarget(SceneColor, ERenderTargetLoadAction::ELoad);
		const FScreenPassTextureViewport SceneColorTextureViewport(SceneColor);


		RDG_EVENT_SCOPE(GraphBuilder, "ViewExtensionSampleVe %dx%d", SceneColorTextureViewport.Rect.Width(), SceneColorTextureViewport.Rect.Height());
#if 1
		{
			FRHIBlendState* BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

			static float tmp_counter = 0.0f;
			tmp_counter += 1.0f / 120.0f;;
			
			TShaderMapRef<FViewExtensionSampleShaderVs> VertexShader(GlobalShaderMap);
			TShaderMapRef<FViewExtensionSampleShaderPs> PixelShader(GlobalShaderMap);

			FViewExtensionSampleShaderPs::FParameters* Parameters = GraphBuilder.AllocParameters<FViewExtensionSampleShaderPs::FParameters>();
			Parameters->ViewExtensionSample_FloatParam = tmp_counter - floor(tmp_counter);
			Parameters->RenderTargets[0] = SceneColorRenderTarget.GetRenderTargetBinding();

			const FScreenPassTextureViewport RegionViewport(SceneColor.Texture, PrimaryViewRect);
			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("NagaViewExtensionPass00"),
				View,
				RegionViewport,
				RegionViewport,
				VertexShader,
				PixelShader,
				BlendState,
				Parameters
			);
		}
#else
		// Clearテスト.
		AddClearRenderTargetPass(GraphBuilder, SceneColor.Texture, FLinearColor::Blue);
#endif
		
		// Because we are not using proxy material, but plain global shader, we need to setup Scene textures ourselves.
		// We don't need to do this per region.
		//FSceneTextureShaderParameters SceneTextures = CreateSceneTextureShaderParameters(GraphBuilder, View, ESceneTextureSetupMode::All);
	}
}


