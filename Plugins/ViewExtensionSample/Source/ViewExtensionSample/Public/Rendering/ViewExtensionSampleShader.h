#pragma once



#include "GlobalShader.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "MaterialShader.h"
#include "ShaderParameterStruct.h"



// 基底.
class FViewExtensionSampleShader : public FGlobalShader
{
public:
	// ------------------------------------------------------------------------------------------
	// Declare Constructors for Shader. and bind local "FParameters" to ShaderParameter.
	BEGIN_SHADER_PARAMETER_STRUCT(FViewExtensionSampleShaderGlobalParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float,						ViewExtensionSample_FloatParam) // カスタムパラメータバインド.
		RENDER_TARGET_BINDING_SLOTS()	// RenderTargetバインド.
	END_SHADER_PARAMETER_STRUCT()
	using FParameters = FViewExtensionSampleShaderGlobalParameters;
	
	// LEGACYではない SHADER_USE_PARAMETER_STRUCT はこのシェーダ自体で完全なパラメータバインディングをしないとエラーチェックに引っかかるため, エンジンコードでもまだLEGACYが使われている.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FViewExtensionSampleShader, FGlobalShader);
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
class FViewExtensionSampleShaderVs : public FViewExtensionSampleShader
{
public:
	DECLARE_GLOBAL_SHADER(FViewExtensionSampleShaderVs);
};

// PS実装.
class FViewExtensionSampleShaderPs : public FViewExtensionSampleShader
{
public:
	DECLARE_GLOBAL_SHADER(FViewExtensionSampleShaderVs);
};


// 基底.
class FViewExtensionSamplePostBasePassShader : public FGlobalShader
{
public:
	// ------------------------------------------------------------------------------------------
	// Declare Constructors for Shader. and bind local "FParameters" to ShaderParameter.
	BEGIN_SHADER_PARAMETER_STRUCT(FViewExtensionSampleShaderGlobalParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float,						ViewExtensionSample_FloatParam) // カスタムパラメータバインド.
		RENDER_TARGET_BINDING_SLOTS()	// RenderTargetバインド.
	END_SHADER_PARAMETER_STRUCT()
	using FParameters = FViewExtensionSampleShaderGlobalParameters;
	
	// LEGACYではない SHADER_USE_PARAMETER_STRUCT はこのシェーダ自体で完全なパラメータバインディングをしないとエラーチェックに引っかかるため, エンジンコードでもまだLEGACYが使われている.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FViewExtensionSamplePostBasePassShader, FGlobalShader);
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
class FViewExtensionSamplePostBasePassShaderVs : public FViewExtensionSamplePostBasePassShader
{
public:
	DECLARE_GLOBAL_SHADER(FViewExtensionSamplePostBasePassShaderVs);
};

// SampleShaderのPS実装.
class FViewExtensionSamplePostBasePassShaderPs : public FViewExtensionSamplePostBasePassShader
{
public:
	DECLARE_GLOBAL_SHADER(FViewExtensionSamplePostBasePassShaderVs);
};

