// Copyright Epic Games, Inc. All Rights Reserved.

#include "NagaExperiment.h"

#define LOCTEXT_NAMESPACE "FNagaExperimentModule"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

void FNagaExperimentModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	// Pluguin側Shaderディレクトリをマッピング.
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NagaExperiment"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/NagaExperiment/Shaders"), PluginShaderDir);
}

void FNagaExperimentModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FNagaExperimentModule, NagaExperiment)