#pragma once



#include "GlobalShader.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "MaterialShader.h"
#include "ShaderParameterStruct.h"

// 基底.
class FPostBasePassModifyGBuffer : public FGlobalShader
{
public:
	// ------------------------------------------------------------------------------------------
	// Declare Constructors for Shader. and bind local "FParameters" to ShaderParameter.
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderInnerParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, pass0_View)

		SHADER_PARAMETER_SAMPLER(SamplerState,		pass0_sampler_screen)
	
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		pass0_tex_gbuffer_b_custom)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		pass0_tex_gbuffer_c)
	
		RENDER_TARGET_BINDING_SLOTS()	// RenderTargetバインド.
	END_SHADER_PARAMETER_STRUCT()
	using FParameters = FShaderInnerParameters;
	
	// LEGACYではない SHADER_USE_PARAMETER_STRUCT はこのシェーダ自体で完全なパラメータバインディングをしないとエラーチェックに引っかかるため, エンジンコードでもまだLEGACYが使われている.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FPostBasePassModifyGBuffer, FGlobalShader);
	// ------------------------------------------------------------------------------------------
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsMobilePlatform(Parameters.Platform);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
// SampleShaderのVS実装.
class FPostBasePassModifyGBufferVs : public FPostBasePassModifyGBuffer
{
public:
	DECLARE_GLOBAL_SHADER(FPostBasePassModifyGBufferVs);
};

// SampleShaderのPS実装.
class FPostBasePassModifyGBufferPs : public FPostBasePassModifyGBuffer
{
public:
	DECLARE_GLOBAL_SHADER(FPostBasePassModifyGBufferPs);
};


// 基底.
class FPrePostProcessToon : public FGlobalShader
{
public:
	// ------------------------------------------------------------------------------------------
	// Declare Constructors for Shader. and bind local "FParameters" to ShaderParameter.
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderInnerParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, pass1_View)
		SHADER_PARAMETER(float,						pass1_list_shadow_threshold)
	
		SHADER_PARAMETER_SAMPLER(SamplerState,		pass1_sampler_screen)
	
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		pass1_tex_scene_color)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		pass1_tex_gbuffer_a)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		pass1_tex_gbuffer_b_custom)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		pass1_tex_gbuffer_c)
		SHADER_PARAMETER(FUintVector2, pass1_tex_dimensions)
	
		RENDER_TARGET_BINDING_SLOTS()	// RenderTargetバインド.
	
	END_SHADER_PARAMETER_STRUCT()
	using FParameters = FShaderInnerParameters;
	
	// LEGACYではない SHADER_USE_PARAMETER_STRUCT はこのシェーダ自体で完全なパラメータバインディングをしないとエラーチェックに引っかかるため, エンジンコードでもまだLEGACYが使われている.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FPrePostProcessToon, FGlobalShader);
	// ------------------------------------------------------------------------------------------
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsMobilePlatform(Parameters.Platform);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
// VS実装.
class FPrePostProcessToonVs : public FPrePostProcessToon
{
public:
	DECLARE_GLOBAL_SHADER(FPrePostProcessToonVs);
};

// PS実装.
class FPrePostProcessToonPs : public FPrePostProcessToon
{
public:
	DECLARE_GLOBAL_SHADER(FPrePostProcessToonPs);
};



class FTestFinalCS : public FGlobalShader
{
public:
	static constexpr uint32 THREADGROUPSIZE_X = 16;
	static constexpr uint32 THREADGROUPSIZE_Y = 16;
	
public:
	DECLARE_GLOBAL_SHADER(FTestFinalCS);
	SHADER_USE_PARAMETER_STRUCT(FTestFinalCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, pass0_SourceTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, pass0_VoronoiWorkTexture)
		SHADER_PARAMETER(FUintVector2, pass0_SourceDimensions)
		SHADER_PARAMETER_SAMPLER(SamplerState, pass0_SourceSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, pass0_HistoryTexture)
	
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, pass0_OutputTexture)
		SHADER_PARAMETER(FUintVector2, pass0_OutputDimensions)

		SHADER_PARAMETER(uint32, pass0_VisualizeMode)
	END_SHADER_PARAMETER_STRUCT()

	//Called by the engine to determine which permutations to compile for this shader
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		//return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		return !IsMobilePlatform(Parameters.Platform);
	}
	//Modifies the compilations environment of the shader
	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		
		// Thread Group数マクロ指定.
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), THREADGROUPSIZE_X);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), THREADGROUPSIZE_Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Z"), 1);
	}
};

class FImageProcessTestCS : public FGlobalShader
{
public:
	static constexpr uint32 THREADGROUPSIZE_X = 16;
	static constexpr uint32 THREADGROUPSIZE_Y = 16;
	
public:
	DECLARE_GLOBAL_SHADER(FImageProcessTestCS);
	SHADER_USE_PARAMETER_STRUCT(FImageProcessTestCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, pass1_View)
	
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, pass1_SceneDepthTexture)
		SHADER_PARAMETER(FUintVector2, pass1_SourceDimensions)
		SHADER_PARAMETER_SAMPLER(SamplerState, pass1_SourceSampler)
	
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, pass1_OutputTexture)
		SHADER_PARAMETER(FUintVector2, pass1_OutputDimensions)

		SHADER_PARAMETER(float, pass1_DepthEdgeCoef)
		SHADER_PARAMETER(uint32, pass1_EnableTileCell)
	
	END_SHADER_PARAMETER_STRUCT()

	//Called by the engine to determine which permutations to compile for this shader
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		//return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		return !IsMobilePlatform(Parameters.Platform);
	}
	//Modifies the compilations environment of the shader
	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		
		// Thread Group数マクロ指定.
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), THREADGROUPSIZE_X);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), THREADGROUPSIZE_Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Z"), 1);
	}
};
