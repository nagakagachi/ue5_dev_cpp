

#include "../../Public/Rendering/ViewExtensionSampleShader.h"

IMPLEMENT_GLOBAL_SHADER(FSamplePrePostProcessVs, "/ViewExtensionSampleShaders/Private/SamplePrePostProcess.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FSamplePrePostProcessPs, "/ViewExtensionSampleShaders/Private/SamplePrePostProcess.usf", "MainPS", SF_Pixel );



IMPLEMENT_GLOBAL_SHADER(FSamplePostBasePassWriteGBufferVs, "/ViewExtensionSampleShaders/Private/SamplePostBasePassWriteGBuffer.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FSamplePostBasePassWriteGBufferPs, "/ViewExtensionSampleShaders/Private/SamplePostBasePassWriteGBuffer.usf", "MainPS", SF_Pixel );