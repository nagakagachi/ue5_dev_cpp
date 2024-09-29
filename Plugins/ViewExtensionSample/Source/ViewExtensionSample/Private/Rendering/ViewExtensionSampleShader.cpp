

#include "../../Public/Rendering/ViewExtensionSampleShader.h"

IMPLEMENT_GLOBAL_SHADER(FPostBasePassModifyGBufferVs, "/ViewExtensionSampleShaders/Private/ResolveHackShadingModel.usf", "Pass0_VS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FPostBasePassModifyGBufferPs, "/ViewExtensionSampleShaders/Private/ResolveHackShadingModel.usf", "Pass0_PS", SF_Pixel );
IMPLEMENT_GLOBAL_SHADER(FPrePostProcessToonVs, "/ViewExtensionSampleShaders/Private/ResolveHackShadingModel.usf", "Pass1_VS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FPrePostProcessToonPs, "/ViewExtensionSampleShaders/Private/ResolveHackShadingModel.usf", "Pass1_PS", SF_Pixel );


IMPLEMENT_GLOBAL_SHADER(FTestFinalCS, "/ViewExtensionSampleShaders/Private/TestCompute.usf", "Pass0_CS", SF_Compute );
IMPLEMENT_GLOBAL_SHADER(FImageProcessTestCS, "/ViewExtensionSampleShaders/Private/TestCompute.usf", "Pass1_CS", SF_Compute );
/*
*/
