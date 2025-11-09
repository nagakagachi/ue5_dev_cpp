// @author: @nagakagachi

#pragma once
/*
*/

#include "RenderResource.h"
#include "RenderUtils.h"
#include "Containers/DynamicRHIResourceArray.h"

namespace naga::gpgpu
{
	// 構造化バッファのCPU側リソース
	template<typename ElementType>
	class TStructuredBufferData
	{
		TResourceArray<ElementType, DEFAULT_ALIGNMENT> Data;

	public:

		/**
		* Constructor
		* @param InNeedsCPUAccess - true if resource array data should be CPU accessible
		*/
		TStructuredBufferData(bool InNeedsCPUAccess = false)
			: Data(InNeedsCPUAccess)
		{
		}
		virtual ~TStructuredBufferData()
		{
		}

		/**
		* Resizes the vertex data buffer, discarding any data which no longer fits.
		*
		* @param num - The number of vertices to allocate the buffer for.
		* @param BufferFlags - Flags to define the expected behavior of the buffer
		*/
		void ResizeBuffer(uint32 num, EResizeBufferFlags BufferFlags = EResizeBufferFlags::None)
		{
			if ((uint32)Data.Num() < num)
			{
				// Enlarge the array.
				if (!EnumHasAnyFlags(BufferFlags, EResizeBufferFlags::AllowSlackOnGrow))
				{
					Data.Reserve(num);
				}

				Data.AddUninitialized(num - Data.Num());
			}
			else if ((uint32)Data.Num() > num)
			{
				// Shrink the array.
				//bool AllowShinking = !EnumHasAnyFlags(BufferFlags, EResizeBufferFlags::AllowSlackOnReduce);
				//Data.RemoveAt(num, Data.Num() - num, AllowShinking);
				auto AllowShinking = (!EnumHasAnyFlags(BufferFlags, EResizeBufferFlags::AllowSlackOnReduce))? EAllowShrinking::Yes : EAllowShrinking::No;
				Data.RemoveAt(num, Data.Num() - num, AllowShinking);
			}
		}

		void Empty(uint32 num)
		{
			Data.Empty(num);
		}

		bool IsValidIndex(uint32 Index)
		{
			return Data.IsValidIndex(Index);
		}

		/**
		* @return stride of the vertex type stored in the resource data array
		*/
		uint32 GetStride() const
		{
			return sizeof(ElementType);
		}
		/**
		* @return uint8 pointer to the resource data array
		*/
		uint8* GetDataPointer()
		{
			return (uint8*)Data.GetData();
		}

		/**
		* @return resource array interface access
		*/
		FResourceArrayInterface* GetResourceArray()
		{
			return &Data;
		}
		/**
		* Serializer for this class
		*
		* @param Ar - archive to serialize to
		* @param B - data to serialize
		*/
		void Serialize(FArchive& Ar)
		{
			Data.TResourceArray<ElementType, DEFAULT_ALIGNMENT>::BulkSerialize(Ar);
		}
		/**
		* Assignment. This is currently the only method which allows for
		* modifying an existing resource array
		*/
		void Assign(const TArray<ElementType>& Other)
		{
			ResizeBuffer(Other.Num());
			if (Other.Num())
			{
				memcpy(GetDataPointer(), &Other[0], Other.Num() * sizeof(ElementType));
			}
		}

		/**
		* Helper function to return the amount of memory allocated by this
		* container.
		*
		* @returns Number of bytes allocated by this container.
		*/
		SIZE_T GetResourceSize() const
		{
			return Data.GetAllocatedSize();
		}

		/**
		* Helper function to return the number of elements by this
		* container.
		*
		* @returns Number of elements allocated by this container.
		*/
		virtual int32 Num() const
		{
			return Data.Num();
		}

		bool GetAllowCPUAccess() const
		{
			return Data.GetAllowCPUAccess();
		}
	};

	// 構造化バッファリソース
	template<typename ElementType>
	class FStructuredBufferResource : public FRenderResource
	{
	public:
		FStructuredBufferResource()
		{
		}
		virtual ~FStructuredBufferResource()
		{
			CleanUp();
		}

		void CleanUp()
		{
			if (bufferData_)
			{
				delete bufferData_;
				bufferData_ = NULL;
			}
		}

		// 初期リソース無し初期化. ゼロクリア.
		void Init(uint32 num, bool enable_uav = false, bool enable_cpu_access = false)
		{
			num_ = num;
			enable_uav_ = enable_uav;
			enable_cpu_access_ = enable_cpu_access;

			AllocateData(enable_cpu_access_);

			bufferData_->ResizeBuffer(num_);

			// ゼロクリア
			FMemory::Memset(bufferData_->GetDataPointer(), 0x00, stride_ * num_);
		}
		// CPUリソース設定して初期値として利用
		void Init(const TArray<ElementType>& data, bool enable_uav = false, bool enable_cpu_access = false)
		{
			num_ = data.Num();
			enable_uav_ = enable_uav;
			enable_cpu_access_ = enable_cpu_access;
			if (num_)
			{
				AllocateData(enable_cpu_access_);
				check(stride_ == data.GetTypeSize());
				bufferData_->ResizeBuffer(num_);
				FMemory::Memcpy(bufferData_->GetDataPointer(), data.GetData(), stride_ * num_);
			}
		}
		// RnderThreadから呼ばれるRHIリソース初期化
		void InitRHI(FRHICommandListBase& RHICmdList)
		{
			check(bufferData_);
			FResourceArrayInterface* ResourceArray = bufferData_->GetResourceArray();
			if (ResourceArray->GetResourceDataSize())
			{

				EBufferUsageFlags usage = EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource;
				if (enable_uav_)
					usage |= EBufferUsageFlags::UnorderedAccess;

				rhi_state_ = ERHIAccess::UAVCompute;
				
				const FRHIBufferCreateDesc CreateDesc =
					FRHIBufferCreateDesc::CreateStructured(TEXT("FStructuredBufferResource"), num_ * stride_, stride_)
					//.SetUsage(usage)
					.AddUsage(usage)
					.SetInitActionResourceArray(ResourceArray)
					.SetInitialState(rhi_state_);
				
				buffer_ = RHICmdList.CreateBuffer(CreateDesc);

				srv_ = RHICmdList.CreateShaderResourceView(buffer_,
					FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(stride_));

				// UAVアクセスしたい場合は生成
				if (enable_uav_)
					uav_ = RHICmdList.CreateUnorderedAccessView(buffer_, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(stride_));
				
				/*
				// Create the vertex buffer.
				FRHIResourceCreateInfo CreateInfo(_T("FStructuredBufferResource"), ResourceArray);

				EBufferUsageFlags usage = BUF_Static | BUF_ShaderResource;
				if (enable_uav_)
					usage |= BUF_UnorderedAccess;

				buffer_ = RHICmdList.CreateStructuredBuffer(stride_, num_ * stride_, usage, CreateInfo);

				srv_ = RHICmdList.CreateShaderResourceView(buffer_);

				// UAVアクセスしたい場合は生成
				if (enable_uav_)
					uav_ = RHICmdList.CreateUnorderedAccessView(buffer_, false, false);
				*/
			}
		}
		// FRenderResource interface.
		virtual void ReleaseRHI() override
		{
			srv_.SafeRelease();
			uav_.SafeRelease();
			buffer_.SafeRelease();
		}

		// Get Current RHI State.
		ERHIAccess GetCurrentRhiState() const { return rhi_state_; }
		// Transition State and Return NextState.
		ERHIAccess TransitionRhiState(ERHIAccess next) 
		{
			rhi_state_ = next;
			return rhi_state_;
		}


		// getter
		int NumElement() const { return num_; }
		int Stride() const { return stride_; }
	
		//FStructuredBufferRHIRef GetBuffer() { return buffer_; }
		FBufferRHIRef GetBuffer() { return buffer_; }

		FShaderResourceViewRHIRef GetSrv() { return srv_; }
		FUnorderedAccessViewRHIRef GetUav() { return uav_; }

	private:
		void AllocateData(bool bInNeedsCPUAccess)
		{
			CleanUp();

			bufferData_ = new TStructuredBufferData<ElementType>(bInNeedsCPUAccess);
			stride_ = bufferData_->GetStride();
		}


		//FStructuredBufferRHIRef buffer_;
		FBufferRHIRef buffer_;

		FShaderResourceViewRHIRef srv_;
		FUnorderedAccessViewRHIRef uav_;

		// 現在のRHIステート. 現状では考える必要は無いが, マルチスレッドレンダリングで共有する場合はCommandListをマージする際の整合性に注意.
		ERHIAccess rhi_state_ = ERHIAccess::Unknown;

		bool	enable_uav_ = false;
		bool	enable_cpu_access_ = false;
		int		num_ = 0;
		int		stride_ = 0;
		TStructuredBufferData<ElementType>* bufferData_ = nullptr;
	};
}