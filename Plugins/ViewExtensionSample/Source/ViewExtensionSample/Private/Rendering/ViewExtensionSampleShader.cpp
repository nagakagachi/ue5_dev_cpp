

#include "../../Public/Rendering/ViewExtensionSampleShader.h"

IMPLEMENT_GLOBAL_SHADER(FViewExtensionSampleShaderVs, "/ViewExtensionSampleShaders/Private/ViewExtensionSampleShader.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FViewExtensionSampleShaderPs, "/ViewExtensionSampleShaders/Private/ViewExtensionSampleShader.usf", "MainPS", SF_Pixel );



IMPLEMENT_GLOBAL_SHADER(FViewExtensionSamplePostBasePassShaderVs, "/ViewExtensionSampleShaders/Private/ViewExtensionSamplePostBasePassShader.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FViewExtensionSamplePostBasePassShaderPs, "/ViewExtensionSampleShaders/Private/ViewExtensionSamplePostBasePassShader.usf", "MainPS", SF_Pixel );