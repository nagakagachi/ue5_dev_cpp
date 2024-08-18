
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
		GBufferB.a のShadingModelIDは Naga_EncodeShadingModelIdAndSelectiveOutputMask(1, 0)) で生成する.	
	*/

	constexpr int RT_SceneColor = 0;
	constexpr int RT_GB_Normal = 1;
	constexpr int RT_GB_PbrShadingModel = 2;
	constexpr int RT_GB_BcAo = 3;
	constexpr int RT_GB_CustomData = 4;
	constexpr int RT_GB_PreShadow = 5;
	
	static float tmp_counter = 0.0f;
	tmp_counter += 1.0f / 60.0f;
	if(65535.0f < tmp_counter) tmp_counter = 0.0f;
	
#if 0
	// GBufferクリアテスト.
	// GBUfferB をクリアしてaチャンネルのShadingModelIDを書き換えるテスト.
	constexpr uint32 ShadingModelID_Unlit = 0;
	constexpr uint32 OverrideShadingModelID = ShadingModelID_Unlit;
	AddClearRenderTargetPass(GraphBuilder, RenderTargets[RT_GB_PbrShadingModel].GetTexture(), FLinearColor(1,0,0.5, Naga_EncodeShadingModelIdAndSelectiveOutputMask(OverrideShadingModelID, 0)));
#endif

	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	
#if 0
	// GBufferへの書き込みテスト.
	{
		TShaderMapRef<FSamplePostBasePassWriteGBufferVs> VertexShader(GlobalShaderMap);
		TShaderMapRef<FSamplePostBasePassWriteGBufferPs> PixelShader(GlobalShaderMap);
		
		FSamplePostBasePassWriteGBufferPs::FParameters* Parameters = GraphBuilder.AllocParameters<FSamplePostBasePassWriteGBufferPs::FParameters>();
		Parameters->ViewExtensionSample_FloatParam = cos(tmp_counter*2.0f)*0.5f + 0.5f;
		Parameters->RenderTargets[0] = RenderTargets[RT_GB_BcAo];
		
		const FIntRect PrimaryViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(InView);
		FRHIBlendState* BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
		
		const FScreenPassTextureViewport RegionViewport(GetAsTexture(RenderTargets[RT_GB_BcAo].GetTexture()), PrimaryViewRect);
		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("Naga_GBufferWriteTest"),
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
	
#if 0
	// GBufferを読み取り用にワークバッファへコピーして読み取りしてGBuffer書き込みをするテスト.
	//	GBuffer->ワークバッファへCopy->ワークバッファを読み取り利用してGBuffer書き込み.
	{
		// PostBasePassではSceneTexturesのGBuffer参照は空なのでRenderTargets側のGBufferを利用する.
		FRDGTextureRef target_gbuffer_texture = RenderTargets[RT_GB_BcAo].GetTexture();

		// Create WorkBuffer.
		FRDGTextureRef new_gbuffer_texture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				target_gbuffer_texture->Desc.Extent ,
				target_gbuffer_texture->Desc.Format,
				{},//target_gbuffer_texture->Desc.ClearValue,
				ETextureCreateFlags::ShaderResource | ETextureCreateFlags::RenderTargetable
			), TEXT("Naga_GBufferTexture"));
		
		// GBuffer -> WorkBuffer.
		{
			FRHICopyTextureInfo copy_info{};
			AddCopyTexturePass(GraphBuilder, target_gbuffer_texture, new_gbuffer_texture, copy_info);
		}
		
		{
			TShaderMapRef<FSamplePostBasePassReadGBufferVs> VertexShader(GlobalShaderMap);
			TShaderMapRef<FSamplePostBasePassReadGBufferPs> PixelShader(GlobalShaderMap);

			
			FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			
			FSamplePostBasePassReadGBufferPs::FParameters* Parameters = GraphBuilder.AllocParameters<FSamplePostBasePassReadGBufferPs::FParameters>();
			Parameters->ViewExtensionSample_FloatParam = cos(tmp_counter*2.0f)*0.5f + 0.5f;
			Parameters->tex_screen_gbuffer_c = new_gbuffer_texture;
			Parameters->tex_screen_gbuffer_c_Sampler = PointClampSampler;
			Parameters->RenderTargets[0] = RenderTargets[RT_GB_BcAo];
			
			const FIntRect PrimaryViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(InView);
			FRHIBlendState* BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			
			const FScreenPassTextureViewport RegionViewport(GetAsTexture(RenderTargets[RT_GB_BcAo].GetTexture()), PrimaryViewRect);
			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("Naga_GBufferReadTest"),
				InView,
				RegionViewport,
				RegionViewport,
				VertexShader,
				PixelShader,
				BlendState,
				Parameters
			);
		}
	}
#endif

	
#if 1
	// GBuffer操作.
	{
		// PostBasePassではSceneTexturesのGBuffer参照は空なのでRenderTargets側のGBufferを利用する.
		FRDGTextureRef gb_tex_pbr_shadingmodel = RenderTargets[RT_GB_PbrShadingModel].GetTexture();
		FRDGTextureRef gb_tex_bc_ao = RenderTargets[RT_GB_BcAo].GetTexture();

		// Create WorkBuffer.
		FRDGTextureRef new_gbuffer_tex_pbr_shadingmodel = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				gb_tex_pbr_shadingmodel->Desc.Extent ,
				gb_tex_pbr_shadingmodel->Desc.Format,
				{},
				ETextureCreateFlags::ShaderResource | ETextureCreateFlags::RenderTargetable
			), TEXT("Naga_GBuffer_C_Work"));
		
		FRDGTextureRef new_gbuffer_tex_bc_ao = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				gb_tex_bc_ao->Desc.Extent ,
				gb_tex_bc_ao->Desc.Format,
				{},
				ETextureCreateFlags::ShaderResource | ETextureCreateFlags::RenderTargetable
			), TEXT("Naga_GBuffer_C_Work"));
		
		// GBuffer -> WorkBuffer.
		{
			FRHICopyTextureInfo copy_info{};
			AddCopyTexturePass(GraphBuilder, gb_tex_pbr_shadingmodel, new_gbuffer_tex_pbr_shadingmodel, copy_info);
			AddCopyTexturePass(GraphBuilder, gb_tex_bc_ao, new_gbuffer_tex_bc_ao, copy_info);
		}

		FrameExtendGBuffer = new_gbuffer_tex_pbr_shadingmodel;
		
		{
			TShaderMapRef<FPostBasePassModifyGBufferVs> VertexShader(GlobalShaderMap);
			TShaderMapRef<FPostBasePassModifyGBufferPs> PixelShader(GlobalShaderMap);

			
			FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			
			FPostBasePassModifyGBufferPs::FParameters* Parameters = GraphBuilder.AllocParameters<FPostBasePassModifyGBufferPs::FParameters>();
			{
				Parameters->ViewExtensionSample_FloatParam = cos(tmp_counter*2.0f)*0.5f + 0.5f;
				Parameters->sampler_screen = PointClampSampler;

				Parameters->tex_gbuffer_b_custom = new_gbuffer_tex_pbr_shadingmodel;
				Parameters->tex_gbuffer_c = new_gbuffer_tex_bc_ao;

				Parameters->RenderTargets[0] = RenderTargets[RT_GB_PbrShadingModel];
				Parameters->RenderTargets[1] = RenderTargets[RT_GB_BcAo];
			}
			
			const FIntRect PrimaryViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(InView);
			FRHIBlendState* BlendState =
				TStaticBlendState<
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero
				>::GetRHI();
			
			const FScreenPassTextureViewport RegionViewport(GetAsTexture(RenderTargets[RT_GB_PbrShadingModel].GetTexture()), PrimaryViewRect);
			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("Naga_GBufferModify"),
				InView,
				RegionViewport,
				RegionViewport,
				VertexShader,
				PixelShader,
				BlendState,
				Parameters
			);
		}
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

	// Getting material data for the current view.
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	
#if 0
	// PostProsess前パスでScreenPassとして任意VSPSを動作させるテスト.
	{
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
				RDG_EVENT_NAME("Naga_PrePostProcessTest"),
				View,
				RegionViewport,
				RegionViewport,
				VertexShader,
				PixelShader,
				BlendState,
				Parameters
			);
		}
	}
#endif
	
#if 1
	// ToonPass.
	{
		FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		
		const auto& input_scene_textures = Inputs.SceneTextures->GetParameters();

		// Reusing the same output description for our back buffer as SceneColor
		FRDGTextureDesc WorkOutputDesc = {};;
		{
			const auto scene_color_desc = SceneColor.Texture->Desc;
			
			WorkOutputDesc = FRDGTextureDesc::Create2D(
				scene_color_desc.Extent, scene_color_desc.Format, scene_color_desc.ClearValue,
				ETextureCreateFlags::ShaderResource|ETextureCreateFlags::RenderTargetable
				);
		}
		FRDGTexture* WorkTexture = GraphBuilder.CreateTexture(WorkOutputDesc, TEXT("NagaViewExtensionPrePostProcessWorkTexture"));
		FScreenPassRenderTarget WorkTextureRenderTarget = FScreenPassRenderTarget(WorkTexture, SceneColor.ViewRect, ERenderTargetLoadAction::ENoAction);

		// SceneColor->Work
		{
			FRHICopyTextureInfo copy_info{};
			AddCopyTexturePass(GraphBuilder, input_scene_textures->SceneColorTexture, WorkTexture, copy_info);
		}
		
		
		{
			FScreenPassRenderTarget SceneColorRenderTarget(input_scene_textures->SceneColorTexture, ERenderTargetLoadAction::ELoad);
			
			FRHIBlendState* BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

			TShaderMapRef<FPrePostProcessToonVs> VertexShader(GlobalShaderMap);
			TShaderMapRef<FPrePostProcessToonPs> PixelShader(GlobalShaderMap);

			FPrePostProcessToonPs::FParameters* Parameters = GraphBuilder.AllocParameters<FPrePostProcessToonPs::FParameters>();
			{
				Parameters->list_shadow_threshold = 0.2;

				Parameters->sampler_screen = PointClampSampler;
				
				Parameters->tex_scene_color = WorkTexture;
				//Parameters->tex_gbuffer_b = input_scene_textures->GBufferBTexture;
				Parameters->tex_gbuffer_b_custom = FrameExtendGBuffer;// PostBasePassで生成したRDGTextureを使用することをRDGに指示.

				Parameters->RenderTargets[0] = SceneColorRenderTarget.GetRenderTargetBinding();
			}
			const FScreenPassTextureViewport RegionViewport(SceneColor.Texture, PrimaryViewRect);
			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("Naga_PrePostProcessToon"),
				View,
				RegionViewport,
				RegionViewport,
				VertexShader,
				PixelShader,
				BlendState,
				Parameters
			);
		}
	}
#endif
}


