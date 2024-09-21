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
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float,						ViewExtensionSample_FloatParam) // カスタムパラメータバインド.	

		SHADER_PARAMETER_SAMPLER(SamplerState,		sampler_screen)
	
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		tex_gbuffer_b_custom)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		tex_gbuffer_c)
	
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
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float,						list_shadow_threshold)
	
		SHADER_PARAMETER_SAMPLER(SamplerState,		sampler_screen)
	
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		tex_scene_color)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		tex_gbuffer_a)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		tex_gbuffer_b_custom)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		tex_gbuffer_c)
	
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



class FTestCS : public FGlobalShader
{
public:
	static constexpr uint32 THREADGROUPSIZE_X = 16;
	static constexpr uint32 THREADGROUPSIZE_Y = 16;
	
public:
	DECLARE_GLOBAL_SHADER(FTestCS);
	SHADER_USE_PARAMETER_STRUCT(FTestCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VoronoiWorkTexture)
		SHADER_PARAMETER(FUintVector2, SourceDimensions)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER(FUintVector2, OutputDimensions)

		SHADER_PARAMETER(uint32, VisualizeMode)
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
		
		OutEnvironment.SetDefine(TEXT("SHADER_ENTRY_POINT_MODE"), 0);// エントリポイント指定.
		
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
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER(FUintVector2, SourceDimensions)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
	
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, OutputTexture)
		SHADER_PARAMETER(FUintVector2, OutputDimensions)

		SHADER_PARAMETER(float, DepthEdgeCoef)
	
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
		
		OutEnvironment.SetDefine(TEXT("SHADER_ENTRY_POINT_MODE"), 1);// エントリポイント指定.
		
		// Thread Group数マクロ指定.
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), THREADGROUPSIZE_X);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), THREADGROUPSIZE_Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Z"), 1);
	}
};

class FVoronoiJumpFloodingCS : public FGlobalShader
{
public:
	static constexpr uint32 THREADGROUPSIZE_X = 16;
	static constexpr uint32 THREADGROUPSIZE_Y = 16;
	
public:
	DECLARE_GLOBAL_SHADER(FVoronoiJumpFloodingCS);
	SHADER_USE_PARAMETER_STRUCT(FVoronoiJumpFloodingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VoronoiWorkTexture)
		SHADER_PARAMETER(FUintVector2, VoronoiWorkTextureDimensions)
		SHADER_PARAMETER_SAMPLER(SamplerState, VoronoiWorkTextureSampler)
	
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, OutputTexture)
		SHADER_PARAMETER(FUintVector2, OutputDimensions)

		SHADER_PARAMETER(uint32, IsJumpFloodingFirstPass)
		SHADER_PARAMETER(uint32, JumpFloodingStepSize)
	
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
		
		OutEnvironment.SetDefine(TEXT("SHADER_ENTRY_POINT_MODE"), 2);// エントリポイント指定.
		
		// Thread Group数マクロ指定.
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), THREADGROUPSIZE_X);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), THREADGROUPSIZE_Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Z"), 1);
	}
};
