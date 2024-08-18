#pragma once



#include "GlobalShader.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "MaterialShader.h"
#include "ShaderParameterStruct.h"



// 基底.
class FSamplePrePostProcess : public FGlobalShader
{
public:
	// ------------------------------------------------------------------------------------------
	// Declare Constructors for Shader. and bind local "FParameters" to ShaderParameter.
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderInnerParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float,						ViewExtensionSample_FloatParam) // カスタムパラメータバインド.
		RENDER_TARGET_BINDING_SLOTS()	// RenderTargetバインド.
	END_SHADER_PARAMETER_STRUCT()
	using FParameters = FShaderInnerParameters;
	
	// LEGACYではない SHADER_USE_PARAMETER_STRUCT はこのシェーダ自体で完全なパラメータバインディングをしないとエラーチェックに引っかかるため, エンジンコードでもまだLEGACYが使われている.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FSamplePrePostProcess, FGlobalShader);
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
class FSamplePrePostProcessVs : public FSamplePrePostProcess
{
public:
	DECLARE_GLOBAL_SHADER(FSamplePrePostProcessVs);
};

// PS実装.
class FSamplePrePostProcessPs : public FSamplePrePostProcess
{
public:
	DECLARE_GLOBAL_SHADER(FSamplePrePostProcessPs);
};


// 基底.
class FSamplePostBasePassWriteGBuffer : public FGlobalShader
{
public:
	// ------------------------------------------------------------------------------------------
	// Declare Constructors for Shader. and bind local "FParameters" to ShaderParameter.
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderInnerParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float,						ViewExtensionSample_FloatParam) // カスタムパラメータバインド.
		RENDER_TARGET_BINDING_SLOTS()	// RenderTargetバインド.
	END_SHADER_PARAMETER_STRUCT()
	using FParameters = FShaderInnerParameters;
	
	// LEGACYではない SHADER_USE_PARAMETER_STRUCT はこのシェーダ自体で完全なパラメータバインディングをしないとエラーチェックに引っかかるため, エンジンコードでもまだLEGACYが使われている.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FSamplePostBasePassWriteGBuffer, FGlobalShader);
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
class FSamplePostBasePassWriteGBufferVs : public FSamplePostBasePassWriteGBuffer
{
public:
	DECLARE_GLOBAL_SHADER(FSamplePostBasePassWriteGBufferVs);
};

// SampleShaderのPS実装.
class FSamplePostBasePassWriteGBufferPs : public FSamplePostBasePassWriteGBuffer
{
public:
	DECLARE_GLOBAL_SHADER(FSamplePostBasePassWriteGBufferPs);
};


// 基底.
class FSamplePostBasePassReadGBuffer : public FGlobalShader
{
public:
	// ------------------------------------------------------------------------------------------
	// Declare Constructors for Shader. and bind local "FParameters" to ShaderParameter.
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderInnerParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float,						ViewExtensionSample_FloatParam) // カスタムパラメータバインド.	

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		tex_screen_gbuffer_c)
		SHADER_PARAMETER_SAMPLER(SamplerState,		tex_screen_gbuffer_c_Sampler)
	
		RENDER_TARGET_BINDING_SLOTS()	// RenderTargetバインド.
	END_SHADER_PARAMETER_STRUCT()
	using FParameters = FShaderInnerParameters;
	
	// LEGACYではない SHADER_USE_PARAMETER_STRUCT はこのシェーダ自体で完全なパラメータバインディングをしないとエラーチェックに引っかかるため, エンジンコードでもまだLEGACYが使われている.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FSamplePostBasePassReadGBuffer, FGlobalShader);
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
class FSamplePostBasePassReadGBufferVs : public FSamplePostBasePassReadGBuffer
{
public:
	DECLARE_GLOBAL_SHADER(FSamplePostBasePassReadGBufferVs);
};

// SampleShaderのPS実装.
class FSamplePostBasePassReadGBufferPs : public FSamplePostBasePassReadGBuffer
{
public:
	DECLARE_GLOBAL_SHADER(FSamplePostBasePassReadGBufferPs);
};




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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D,		tex_gbuffer_b_custom)
	
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