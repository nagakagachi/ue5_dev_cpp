/*
	Engine のFPositionVertexBufferをコピーして改造
*/


#include "gpu_compute_shader_vertex_buffer.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "Components.h"

#include "StaticMeshVertexData.h"
#include "EngineUtils.h"
#include "RHIResourceUtils.h"

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
	void FComputeShaderIndexBuffer32::Init(const TArray<ElementType>& InIndices, bool bNeedsUAV, bool bNeedsCPUAccess)
	{
		num = InIndices.Num();
		NeedsUAV = bNeedsUAV;

		AllocateData( bNeedsCPUAccess );
		Indices->SetNumUninitialized(InIndices.Num());
		FMemory::Memcpy(Indices->GetData(), InIndices.GetData(), InIndices.Num() * sizeof(ElementType));
	}
	void FComputeShaderIndexBuffer32::InitRHI(FRHICommandListBase& RHICmdList)
	{
		EBufferUsageFlags usage = EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource;
		if (NeedsUAV)
			usage |= EBufferUsageFlags::UnorderedAccess;
		
		FRHIBufferCreateDesc CreateDesc =
			FRHIBufferCreateDesc::CreateIndex(TEXT("FComputeShaderIndexBuffer32"), sizeof(ElementType) * num, sizeof(ElementType))
			.AddUsage(usage).SetInitialState(rhi_state_);
		if (Indices)
		{
			CreateDesc.SetInitActionResourceArray(Indices);
		}

		IndexBufferRHI = RHICmdList.CreateBuffer(CreateDesc);
		
		srv = RHICmdList.CreateShaderResourceView(
			IndexBufferRHI, 
			FRHIViewDesc::CreateBufferSRV()
				.SetType(FRHIViewDesc::EBufferType::Typed)
				.SetFormat(PF_R32_UINT));
		
		if(NeedsUAV)
			uav = RHICmdList.CreateUnorderedAccessView(IndexBufferRHI,
				FRHIViewDesc::CreateBufferUAV()
				.SetType(FRHIViewDesc::EBufferType::Typed)
				.SetFormat(PF_R32_UINT));
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
		Indices = new TResourceArray<ElementType, INDEXBUFFER_ALIGNMENT>(bNeedsCPUAccess);
	}
	//=================================================================================================================================================
}
