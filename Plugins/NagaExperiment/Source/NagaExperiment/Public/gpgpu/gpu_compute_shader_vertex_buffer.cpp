/*
	Engine のFPositionVertexBufferをコピーして改造
*/


#include "gpu_compute_shader_vertex_buffer.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "Components.h"

#include "StaticMeshVertexData.h"
#include "EngineUtils.h"

namespace naga::gpgpu
{
	//=================================================================================================================================================
	FComputeShaderIndexBuffer32::~FComputeShaderIndexBuffer32()
	{
		CleanUp();
	}
	void FComputeShaderIndexBuffer32::CleanUp()
	{
		if (Indices)
		{
			delete Indices;
			Indices = NULL;
		}
	}
	void FComputeShaderIndexBuffer32::Init(uint32 InNumIndices, bool bNeedsUAV, bool bNeedsCPUAccess)
	{
		num = InNumIndices;
		NeedsUAV = bNeedsUAV;
		CleanUp();
	}
	void FComputeShaderIndexBuffer32::Init(const TArray<uint32>& InIndices, bool bNeedsUAV, bool bNeedsCPUAccess)
	{
		num = InIndices.Num();
		NeedsUAV = bNeedsUAV;

		AllocateData( bNeedsCPUAccess );
		Indices->SetNumUninitialized(InIndices.Num());
		FMemory::Memcpy(Indices->GetData(), InIndices.GetData(), InIndices.Num() * sizeof(uint32));
	}
	void FComputeShaderIndexBuffer32::InitRHI(FRHICommandListBase& RHICmdList)
	{
		FRHIResourceCreateInfo CreateInfo(_T("FComputeShaderIndexBuffer32"), Indices);
		EBufferUsageFlags usage = BUF_Static | BUF_ShaderResource;
		if (NeedsUAV)
			usage |= BUF_UnorderedAccess;

		IndexBufferRHI = RHICmdList.CreateIndexBuffer(sizeof(uint32), num * sizeof(uint32), usage, CreateInfo);

		srv = RHICmdList.CreateShaderResourceView(IndexBufferRHI, sizeof(uint32), PF_R32_UINT);
		if(NeedsUAV)
			uav = RHICmdList.CreateUnorderedAccessView(IndexBufferRHI, PF_R32_UINT);
	}
	void FComputeShaderIndexBuffer32::ReleaseRHI()
	{
		srv.SafeRelease();
		uav.SafeRelease();
		FIndexBuffer::ReleaseRHI();
	}

	void FComputeShaderIndexBuffer32::AllocateData(bool bNeedsCPUAccess)
	{
		// Clear any old VertexData before allocating.
		CleanUp();
		Indices = new TResourceArray<uint32, INDEXBUFFER_ALIGNMENT>(bNeedsCPUAccess);
	}
	//=================================================================================================================================================
}
