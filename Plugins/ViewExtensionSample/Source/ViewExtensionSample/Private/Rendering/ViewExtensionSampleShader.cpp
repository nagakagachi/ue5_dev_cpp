

#include "../../Public/Rendering/ViewExtensionSampleShader.h"

IMPLEMENT_GLOBAL_SHADER(FSamplePrePostProcessVs, "/ViewExtensionSampleShaders/Private/SamplePrePostProcess.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FSamplePrePostProcessPs, "/ViewExtensionSampleShaders/Private/SamplePrePostProcess.usf", "MainPS", SF_Pixel );

IMPLEMENT_GLOBAL_SHADER(FSamplePostBasePassWriteGBufferVs, "/ViewExtensionSampleShaders/Private/SamplePostBasePassWriteGBuffer.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FSamplePostBasePassWriteGBufferPs, "/ViewExtensionSampleShaders/Private/SamplePostBasePassWriteGBuffer.usf", "MainPS", SF_Pixel );


IMPLEMENT_GLOBAL_SHADER(FSamplePostBasePassReadGBufferVs, "/ViewExtensionSampleShaders/Private/SamplePostBasePassReadGBuffer.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FSamplePostBasePassReadGBufferPs, "/ViewExtensionSampleShaders/Private/SamplePostBasePassReadGBuffer.usf", "MainPS", SF_Pixel );


IMPLEMENT_GLOBAL_SHADER(FPostBasePassModifyGBufferVs, "/ViewExtensionSampleShaders/Private/PostBasePassModifyGBuffer.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FPostBasePassModifyGBufferPs, "/ViewExtensionSampleShaders/Private/PostBasePassModifyGBuffer.usf", "MainPS", SF_Pixel );

IMPLEMENT_GLOBAL_SHADER(FPrePostProcessToonVs, "/ViewExtensionSampleShaders/Private/PrePostProcessToon.usf", "MainVS", SF_Vertex );
IMPLEMENT_GLOBAL_SHADER(FPrePostProcessToonPs, "/ViewExtensionSampleShaders/Private/PrePostProcessToon.usf", "MainPS", SF_Pixel );