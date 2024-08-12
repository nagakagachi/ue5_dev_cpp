
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
	
/**
 * Called right after Base Pass rendering finished when using the deferred renderer.
 */

BEGIN_SHADER_PARAMETER_STRUCT(FViewExtensionSamplePreBasePassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


#define NAGA_SHADINGMODELID_MASK					0xF		// 4 bits reserved for ShadingModelID
// DeferredShadingCommon.ush
float Naga_EncodeShadingModelIdAndSelectiveOutputMask(uint32 ShadingModelId, uint32 SelectiveOutputMask)
{
	uint32 Value = (ShadingModelId & NAGA_SHADINGMODELID_MASK) | SelectiveOutputMask;
	return (float)Value / (float)0xFF;
}
void FViewExtensionSampleVe::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	if (!IsValid(WorldSubsystem))
	{
		return;
	}

	FScreenPassTexture ScreenPassTex(SceneTextures->GetParameters()->GBufferATexture);
	RDG_EVENT_SCOPE(GraphBuilder, "FViewExtensionSampleVe::PostRenderBasePassDeferred %dx%d", ScreenPassTex.ViewRect.Width(), ScreenPassTex.ViewRect.Height());
	
	/*
	GBUfferレイアウト -> DeferredShadingCommon.ush EncodeGBuffer()
	ShadingModelID -> ShadingCommon.ush


	OutGBufferA.rgb = EncodeNormal( GBuffer.WorldNormal );
	OutGBufferA.a = GBuffer.PerObjectGBufferData;
	
	OutGBufferB.r = GBuffer.Metallic;
	OutGBufferB.g = GBuffer.Specular;
	OutGBufferB.b = GBuffer.Roughness;
	OutGBufferB.a = EncodeShadingModelIdAndSelectiveOutputMask(GBuffer.ShadingModelID, GBuffer.SelectiveOutputMask);

	OutGBufferC.rgb = EncodeBaseColor( GBuffer.BaseColor );
	OutGBufferC.a = GBuffer.GBufferAO;
	
	OutGBufferD = GBuffer.CustomData;
	OutGBufferE = GBuffer.PrecomputedShadowFactors;
	
	*/

	/*
	RenderTargets の内訳.
	
	RenderTargets[0] : SceneColor
	RenderTargets[1] : GBUfferA
	RenderTargets[2] : GBUfferB
	RenderTargets[3] : GBUfferC
	RenderTargets[4] : GBUfferD
	RenderTargets[5] : GBUfferE
	*/
	/*
	 GBufferB.a のShadingModelIDは Naga_EncodeShadingModelIdAndSelectiveOutputMask(1, 0)) で生成する.	
	*/
	
	// GBufferクリアテスト.
	// GBUfferB をクリアしてaチャンネルのShadingModelIDを書き換えるテスト. 成功.
#if 0
	constexpr uint32 ShadingModelID_Unlit = 0;
	constexpr uint32 OverrideShadingModelID = ShadingModelID_Unlit;
	AddClearRenderTargetPass(GraphBuilder, RenderTargets[2].GetTexture(), FLinearColor(1,0,0.5, Naga_EncodeShadingModelIdAndSelectiveOutputMask(OverrideShadingModelID, 0)));
#endif

	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
#if 1
	{
		TShaderMapRef<FSamplePrePostProcessVs> VertexShader(GlobalShaderMap);
		TShaderMapRef<FSamplePostBasePassWriteGBufferPs> PixelShader(GlobalShaderMap);

		static float tmp_counter = 0.0f;
		tmp_counter += 1.0f / 60.0f;
		if(65535.0f < tmp_counter) tmp_counter = 0.0f;
		
		FSamplePostBasePassWriteGBufferPs::FParameters* Parameters = GraphBuilder.AllocParameters<FSamplePostBasePassWriteGBufferPs::FParameters>();
		Parameters->ViewExtensionSample_FloatParam = cos(tmp_counter*2.0f)*0.5f + 0.5f;
		Parameters->RenderTargets[0] = RenderTargets[3];
		
		const FIntRect PrimaryViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(InView);
		FRHIBlendState* BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
		
		const FScreenPassTextureViewport RegionViewport(GetAsTexture(RenderTargets[3].GetTexture()), PrimaryViewRect);
		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("NagaViewExtensionPass01"),
			InView,
			RegionViewport,
			RegionViewport,
			VertexShader,
			PixelShader,
			BlendState,
			Parameters
		);
	}
#endif
}
/**
 * Called right before Post Processing rendering begins
 */
void FViewExtensionSampleVe::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	if (!IsValid(WorldSubsystem))
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
	const FScreenPassTextureViewport SceneColorTextureViewport(SceneColor);

	if (!SceneColor.IsValid())
	{
		return;
	}
	
	RDG_EVENT_SCOPE(GraphBuilder, "FViewExtensionSampleVe::PrePostProcessPass %dx%d", SceneColorTextureViewport.Rect.Width(), SceneColorTextureViewport.Rect.Height());

	// PostProsess前パスでScreenPassとして任意VSPSを動作させるテスト.
	{
		// Getting material data for the current view.
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		//FScreenPassRenderTarget BackBufferRenderTarget = FScreenPassRenderTarget(OutputRenderTargetTexture, SceneColor.ViewRect, ERenderTargetLoadAction::EClear);
		FScreenPassRenderTarget SceneColorRenderTarget(SceneColor, ERenderTargetLoadAction::ELoad);
		{
			FRHIBlendState* BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

			static float tmp_counter = 0.0f;
			tmp_counter += 1.0f / 120.0f;;
			
			TShaderMapRef<FSamplePrePostProcessVs> VertexShader(GlobalShaderMap);
			TShaderMapRef<FSamplePrePostProcessPs> PixelShader(GlobalShaderMap);

			FSamplePrePostProcessPs::FParameters* Parameters = GraphBuilder.AllocParameters<FSamplePrePostProcessPs::FParameters>();
			Parameters->ViewExtensionSample_FloatParam = (tmp_counter - floor(tmp_counter))*0.2f;
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
		
		// Because we are not using proxy material, but plain global shader, we need to setup Scene textures ourselves.
		// We don't need to do this per region.
		//FSceneTextureShaderParameters SceneTextures = CreateSceneTextureShaderParameters(GraphBuilder, View, ESceneTextureSetupMode::All);
	}
}


