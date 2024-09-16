

#include "../../Public/Rendering/ViewExtensionSampleShader.h"

IMPLEMENT_GLOBAL_SHADER(FPostBasePassModifyGBufferVs, "/ViewExtensionSampleShaders/Private/PostBasePassModifyGBuffer.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FPostBasePassModifyGBufferPs, "/ViewExtensionSampleShaders/Private/PostBasePassModifyGBuffer.usf", "MainPS", SF_Pixel );

IMPLEMENT_GLOBAL_SHADER(FPrePostProcessToonVs, "/ViewExtensionSampleShaders/Private/PrePostProcessToon.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FPrePostProcessToonPs, "/ViewExtensionSampleShaders/Private/PrePostProcessToon.usf", "MainPS", SF_Pixel );


IMPLEMENT_GLOBAL_SHADER(FTestCS, "/ViewExtensionSampleShaders/Private/TestCompute.usf", "MainCS", SF_Compute );
IMPLEMENT_GLOBAL_SHADER(FImageProcessTestCS, "/ViewExtensionSampleShaders/Private/TestCompute.usf", "MainCS", SF_Compute );
