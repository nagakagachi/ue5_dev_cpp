
#include "Rendering/ViewExtensionSampleVe.h"

#include "DynamicResolutionState.h"
#include "FXRenderingUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "Rendering/ViewExtensionSampleShader.h"
#include "Subsystem/ViewExtensionSampleSubsystem.h"

#include "Rendering/NagaVoronoiJfaCompute.h"

#include <bit>

FViewExtensionSampleVe::FViewExtensionSampleVe(const FAutoRegister& AutoRegister, UViewExtensionSampleSubsystem* InWorldSubsystem)
	: FSceneViewExtensionBase(AutoRegister), ManageSubsystem(InWorldSubsystem)
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
/*
BEGIN_SHADER_PARAMETER_STRUCT(FViewExtensionSamplePreBasePassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()
*/

#define NAGA_SHADINGMODELID_MASK					0xF		// 4 bits reserved for ShadingModelID
// DeferredShadingCommon.ush
float Naga_EncodeShadingModelIdAndSelectiveOutputMask(uint32 ShadingModelId, uint32 SelectiveOutputMask)
{
	uint32 Value = (ShadingModelId & NAGA_SHADINGMODELID_MASK) | SelectiveOutputMask;
	return (float)Value / (float)0xFF;
}
void FViewExtensionSampleVe::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	if (!IsValid(ManageSubsystem))
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
	
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	
	// GBuffer操作.
	if(ManageSubsystem->enable_gbuffer_modify)
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
		
		// Copy GBuffer -> WorkBuffer.
		{
			FRHICopyTextureInfo copy_info{};
			AddCopyTexturePass(GraphBuilder, gb_tex_pbr_shadingmodel, new_gbuffer_tex_pbr_shadingmodel, copy_info);
			AddCopyTexturePass(GraphBuilder, gb_tex_bc_ao, new_gbuffer_tex_bc_ao, copy_info);
		}

		// もとのShadingModelを格納したコピーGBuffer参照を保存.
		FrameExtendGBuffer = new_gbuffer_tex_pbr_shadingmodel;
		
		{
			TShaderMapRef<FPostBasePassModifyGBufferVs> VertexShader(GlobalShaderMap);
			TShaderMapRef<FPostBasePassModifyGBufferPs> PixelShader(GlobalShaderMap);

			
			FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			
			FPostBasePassModifyGBufferPs::FParameters* Parameters = GraphBuilder.AllocParameters<FPostBasePassModifyGBufferPs::FParameters>();
			{
				Parameters->pass0_sampler_screen = PointClampSampler;

				Parameters->pass0_tex_gbuffer_b_custom = new_gbuffer_tex_pbr_shadingmodel;
				Parameters->pass0_tex_gbuffer_c = new_gbuffer_tex_bc_ao;

				// オリジナルのGBufferへ書き戻して修正する.
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
}
/**
 * Called right before Post Processing rendering begins
 */
void FViewExtensionSampleVe::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	if (!IsValid(ManageSubsystem))
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
	
	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	
	const auto& input_scene_textures = Inputs.SceneTextures->GetParameters();
	// ToonPass.
	if(ManageSubsystem->enable_gbuffer_modify)
	{
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

		// copy SceneColor->Work
		{
			FRHICopyTextureInfo copy_info{};
			AddCopyTexturePass(GraphBuilder, input_scene_textures->SceneColorTexture, WorkTexture, copy_info);
		}
		
		{
			FScreenPassRenderTarget SceneColorRenderTarget(input_scene_textures->SceneColorTexture, ERenderTargetLoadAction::ELoad);
			
			FRHIBlendState* BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_Zero, BO_Add, BF_Zero, BF_One>::GetRHI();

			TShaderMapRef<FPrePostProcessToonVs> VertexShader(GlobalShaderMap);
			TShaderMapRef<FPrePostProcessToonPs> PixelShader(GlobalShaderMap);

			FPrePostProcessToonPs::FParameters* Parameters = GraphBuilder.AllocParameters<FPrePostProcessToonPs::FParameters>();
			{
				Parameters->pass1_list_shadow_threshold = 0.499;

				Parameters->pass1_sampler_screen = PointClampSampler;
				
				Parameters->pass1_tex_scene_color = WorkTexture;
				Parameters->pass1_tex_gbuffer_a = input_scene_textures->GBufferATexture;
				Parameters->pass1_tex_gbuffer_b_custom = FrameExtendGBuffer;// PostBasePassで生成したRDGTextureを使用することをRDGに指示.
				Parameters->pass1_tex_gbuffer_c = input_scene_textures->GBufferCTexture;

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

	
	if(ManageSubsystem->enable_test_compute)
	{
		FUintVector2 WorkRect(PrimaryViewRect.Width(), PrimaryViewRect.Height());
		
		// Voronoi用Seedバッファ.
		FRDGTexture* VoronoiCellTexture{};
		FRDGTextureDesc VoronoiWorkUavTexDesc = {};;
		{
			{
				// Voronoi Diagram計算のピクセル座標格納のため, 1024以上の精度を要求. 16bit float推奨.
				const EPixelFormat work_format = PF_FloatRGBA;//16bit float.
				const auto scene_color_desc = SceneColor.Texture->Desc;
				// R16B16_Floatが望ましいがUEで選択できないため R32G32B32_Float 等としている. Shader側RWTextureの型と合わせることに注意.
				VoronoiWorkUavTexDesc = FRDGTextureDesc::Create2D(
					scene_color_desc.Extent, work_format, scene_color_desc.ClearValue,
					ETextureCreateFlags::ShaderResource|ETextureCreateFlags::UAV
					);
			}
			VoronoiCellTexture = GraphBuilder.CreateTexture(VoronoiWorkUavTexDesc, TEXT("NagaWorkTexture0"));
		}

		// Generate Voronoi Cell Seed (Edge detection and other).
		{
			FRDGTextureUAVRef WorkUav = GraphBuilder.CreateUAV(VoronoiCellTexture);
			FImageProcessTestCS::FParameters* Parameters = GraphBuilder.AllocParameters<FImageProcessTestCS::FParameters>();
			{
				Parameters->View = View.ViewUniformBuffer;// DeviceDepth to ViewDepth変換等のため.
				
				Parameters->SceneDepthTexture = input_scene_textures->SceneDepthTexture;
				Parameters->SourceDimensions = WorkRect;
				Parameters->SourceSampler = PointClampSampler;

				Parameters->OutputTexture = WorkUav;
				Parameters->OutputDimensions = WorkRect;

				Parameters->DepthEdgeCoef = ManageSubsystem->depth_edge_coef;
				Parameters->EnableTileCell = ManageSubsystem->enable_voronoi_tile_cell;
			}
		
			TShaderMapRef<FImageProcessTestCS> cs(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			FIntVector DispatchGroupSize = FIntVector(FMath::DivideAndRoundUp(WorkRect.X, cs->THREADGROUPSIZE_X),
					FMath::DivideAndRoundUp(WorkRect.Y, cs->THREADGROUPSIZE_Y), 1);
		
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("NagaTestCompute"), ERDGPassFlags::Compute, cs, Parameters, DispatchGroupSize);
		}

		// Generate Voronoi.
		FRDGTexture* voronoi_texture = FNagaVoronoiJfaCompute::Execute(GraphBuilder, VoronoiCellTexture, WorkRect);
		
		{
			// SceneColor をコピーするための Work Textureを作成.
			FRDGTextureDesc WorkSceneColorDesc = {};;
			{
				const auto scene_color_desc = SceneColor.Texture->Desc;
				WorkSceneColorDesc = FRDGTextureDesc::Create2D(
					scene_color_desc.Extent, scene_color_desc.Format, scene_color_desc.ClearValue,
					ETextureCreateFlags::ShaderResource|ETextureCreateFlags::RenderTargetable
					);
			}
			FRDGTexture* WorkSceneColorTexture = GraphBuilder.CreateTexture(WorkSceneColorDesc, TEXT("NagaWorkTexture"));
			// SceneColorを Workへコピー.
			{
				FRHICopyTextureInfo copy_info{};
				AddCopyTexturePass(GraphBuilder, SceneColor.Texture, WorkSceneColorTexture, copy_info);
			}

			FRDGTextureUAVRef SceneColorUav = GraphBuilder.CreateUAV(SceneColor.Texture);
			FTestCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTestCS::FParameters>();
			{
				Parameters->SourceTexture = WorkSceneColorTexture;
				Parameters->VoronoiWorkTexture = voronoi_texture;
				Parameters->SourceSampler = PointClampSampler;
				Parameters->SourceDimensions = WorkRect;

				Parameters->OutputTexture = SceneColorUav;
				Parameters->SourceDimensions = WorkRect;
				
				Parameters->VisualizeMode = ManageSubsystem->edge_debug_view;
			}
		
			TShaderMapRef<FTestCS> cs(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			FIntVector DispatchGroupSize = FIntVector(FMath::DivideAndRoundUp(WorkRect.X, cs->THREADGROUPSIZE_X),
					FMath::DivideAndRoundUp(WorkRect.Y, cs->THREADGROUPSIZE_Y), 1);
		
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("NagaTestCompute"), ERDGPassFlags::Compute, cs, Parameters, DispatchGroupSize);
		}
	}
}


