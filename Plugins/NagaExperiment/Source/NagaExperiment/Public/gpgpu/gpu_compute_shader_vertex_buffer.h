#pragma once
/*
	Engine のFPositionVertexBufferをコピーして改造
*/

#include "RenderResource.h"
#include "RenderUtils.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "StaticMeshVertexData.h"
#include "PackedNormal.h"
#include "Components.h"

namespace naga::gpgpu
{
	/*
		CPUアクセスしないCompute用頂点バッファ
	
		ElementType		: Vector3f, Vector2f, packed Uint
		BufferMaxCount	: default is 1. set other if use multi uv.
	*/
	template<typename ElementType, EPixelFormat SrvFormat, int SrvStride, int BufferMaxCount = 1>
	class FComputeShaderVertexBufferBase : public FVertexBuffer
	{
	public:

		/** Default constructor. */
		FComputeShaderVertexBufferBase()
		{
		}

		/** Destructor. */
		virtual ~FComputeShaderVertexBufferBase()
		{
		}

		/** Delete existing resources */
		void CleanUp()
		{
		}
		// 現状は初期化はサイズ指定のみ
		// 今後初期データ指定版も用意するかも
		virtual void Init(uint32 NumVertices, bool bInNeedsUav = false)
		{
			num_element_ = NumVertices;
			//stride_ = sizeof(ElementType);
			need_uav_ = bInNeedsUav;
		}

		static constexpr uint32 GetStrideStatic()
		{
			return sizeof(ElementType);
		}
		FORCEINLINE constexpr uint32 GetStride() const
		{
			return GetStrideStatic();
		}
		FORCEINLINE uint32 GetNumVertices() const
		{
			return num_element_;
		}

		static constexpr uint32 GetBufferMaxCountStatic()
		{
			return BufferMaxCount;
		}
		FORCEINLINE constexpr uint32 GetBufferMaxCount() const
		{
			return GetBufferMaxCountStatic();
		}

		// FRenderResource interface.
		virtual void InitRHI(FRHICommandListBase& RHICmdList) override
		{
			// Create the vertex buffer.
			FRHIResourceCreateInfo CreateInfo(_T("FComputeShaderVertexBufferBase"));

			EBufferUsageFlags usage = BUF_Static | BUF_ShaderResource;
			if (need_uav_)
				usage |= BUF_UnorderedAccess;

			VertexBufferRHI = RHICmdList.CreateVertexBuffer( this->GetStride() * GetNumVertices() * BufferMaxCount, usage, CreateInfo);

			// we have decide to create the SRV based on GMaxRHIShaderPlatform because this is created once and shared between feature levels for editor preview.
			if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
				this->srv_ = RHICmdList.CreateShaderResourceView(VertexBufferRHI, SrvStride, SrvFormat);

			// UAVアクセスしたい場合は生成
			if (need_uav_)
				uav_ = RHICmdList.CreateUnorderedAccessView(VertexBufferRHI, SrvFormat);
		}
		virtual void ReleaseRHI() override
		{
			this->srv_.SafeRelease();
			uav_.SafeRelease();

			FVertexBuffer::ReleaseRHI();
		}
		virtual FString GetFriendlyName() const override { return TEXT("FComputeShaderVertexBufferBase"); }

		// SRV取得
		FRHIShaderResourceView* GetSrv() const
		{
			return this->srv_;
		}
		// UAV取得
		FRHIUnorderedAccessView* GetUav() const
		{
			return uav_;
		}

		// Get Current RHI State.
		ERHIAccess GetCurrentRhiState() const { return rhi_state_; }
		// Transition State and Return NextState.
		ERHIAccess TransitionRhiState(ERHIAccess next)
		{
			rhi_state_ = next;
			return rhi_state_;
		}

	protected:
		FShaderResourceViewRHIRef srv_;
		FUnorderedAccessViewRHIRef uav_;
		uint32 num_element_ = 0;
		bool need_uav_ = false;

		// 現在のRHIステート. 現状では考える必要は無いが, マルチスレッドレンダリングで共有する場合はCommandListをマージする際の整合性に注意.
		ERHIAccess rhi_state_ = ERHIAccess::VertexOrIndexBuffer;
	};

	//=================================================================================================================================================
	// 頂点バッファPosition
	// UE_4.23\Engine\Source\Runtime\Engine\Public\Rendering\PositionVertexBuffer.h
	class FComputeShaderPositionVertexBufferT : public FComputeShaderVertexBufferBase<FVector3f, PF_R32_FLOAT, 4>
	{
	public:
		FComputeShaderPositionVertexBufferT()
		{
		}
		virtual ~FComputeShaderPositionVertexBufferT()
		{
		}

		virtual FString GetFriendlyName() const override { return TEXT("FComputeShaderPositionVertexBuffer"); }

		// need override
		void BindVertexBuffer(const class FVertexFactory* VertexFactory, struct FStaticMeshDataType& Data) const
		{
			Data.PositionComponent = FVertexStreamComponent(
				this,
				0,
				this->GetStride(),
				VET_Float3
			);
			Data.PositionComponentSRV = this->srv_;
		}
	protected:
	};

	//=================================================================================================================================================
	// 頂点バッファColor
	// UE_4.23\Engine\Source\Runtime\Engine\Public\Rendering\ColorVertexBuffer.h
	class FComputeShaderColorVertexBufferT : public FComputeShaderVertexBufferBase<FColor, PF_R8G8B8A8, 4>
	{
	public:
		FComputeShaderColorVertexBufferT()
		{
		}
		virtual ~FComputeShaderColorVertexBufferT()
		{
		}

		virtual FString GetFriendlyName() const override { return TEXT("FComputeShaderColorVertexBufferT"); }

		// need override
		void BindVertexBuffer(const class FVertexFactory* VertexFactory, struct FStaticMeshDataType& Data) const
		{
			Data.ColorComponent = FVertexStreamComponent(
				this,
				0,
				this->GetStride(),
				VET_Color,
				EVertexStreamUsage::ManualFetch
			);
			Data.ColorComponentsSRV = this->srv_;
			Data.ColorIndexMask = ~0u;
		}
	protected:
	};

	// ----------------------------------------------------------------------------------------------------------------------------
	// 頂点バッファTangentNormal
	// UE_4.22\Engine\Source\Runtime\Engine\Public\Rendering\StaticMeshVertexBuffer.h
	enum class EComputeShaderVertexTangentBasisType
	{
		Default,
		HighPrecision,
	};
	template<EComputeShaderVertexTangentBasisType TangentBasisType>
	struct TComputeShaderVertexTangentTypeSelector
	{
	};
	template<>
	struct TComputeShaderVertexTangentTypeSelector<EComputeShaderVertexTangentBasisType::Default>
	{
		typedef FPackedNormal TangentTypeT;
		static const EVertexElementType VertexElementType = VET_PackedNormal;
		static const EPixelFormat SrvFormatType = PF_R8G8B8A8_SNORM;
		static const int SrvElementStride = 4;
	};
	template<>
	struct TComputeShaderVertexTangentTypeSelector<EComputeShaderVertexTangentBasisType::HighPrecision>
	{
		typedef FPackedRGBA16N TangentTypeT;
		static const EVertexElementType VertexElementType = VET_Short4N;
		static const EPixelFormat SrvFormatType = PF_R16G16B16A16_SNORM; // PF_G32R32F;
		static const int SrvElementStride = 8;
	};

	class FStaticMeshVertexDataInterface;
	template<typename TangentTypeT>
	struct TComputeShaderVertexTangentDatum
	{
		TangentTypeT TangentX;
		TangentTypeT TangentZ;

		FORCEINLINE FVector GetTangentX() const
		{
			return TangentX.ToFVector();
		}

		FORCEINLINE FVector4 GetTangentZ() const
		{
			return TangentZ.ToFVector4();
		}

		FORCEINLINE FVector GetTangentY() const
		{
			return GenerateYAxis(TangentX, TangentZ);
		}

		FORCEINLINE void SetTangents(FVector X, FVector Y, FVector Z)
		{
			TangentX = X;
			TangentZ = FVector4(Z, GetBasisDeterminantSign(X, Y, Z));
		}
		friend FArchive& operator<<(FArchive& Ar, TComputeShaderVertexTangentDatum& Vertex)
		{
			Ar << Vertex.TangentX;
			Ar << Vertex.TangentZ;
			return Ar;
		}
	};

	// Name shortening
	template< EComputeShaderVertexTangentBasisType TangentDataType >
	using TComputeShaderVertexTangentDatumT = TComputeShaderVertexTangentDatum<typename TComputeShaderVertexTangentTypeSelector<TangentDataType>::TangentTypeT>;

	template< EComputeShaderVertexTangentBasisType TangentDataType = EComputeShaderVertexTangentBasisType::Default >
	class FComputeShaderTangentsVertexBufferT : 
		public FComputeShaderVertexBufferBase< TComputeShaderVertexTangentDatumT<TangentDataType> , TComputeShaderVertexTangentTypeSelector<TangentDataType>::SrvFormatType, TComputeShaderVertexTangentTypeSelector<TangentDataType>::SrvElementStride>
	{
	public:
		FComputeShaderTangentsVertexBufferT()
		{
		}
		virtual ~FComputeShaderTangentsVertexBufferT()
		{
		}

		virtual FString GetFriendlyName() const override { return TEXT("FComputeShaderTangentsVertexBufferT"); }

		// need override
		void BindVertexBuffer(const class FVertexFactory* VertexFactory, struct FStaticMeshDataType& Data) const
		{
			{
				Data.TangentsSRV = this->srv_;
			}
			{
				uint32 TangentSizeInBytes = 0;
				uint32 TangentXOffset = 0;
				uint32 TangentZOffset = 0;
				EVertexElementType TangentElemType = VET_None;

				typedef TComputeShaderVertexTangentDatumT<TangentDataType> TangentType;

				// SRVのストライドと違ってこちらはTangents1要素のサイズ( Pakcedなら 4byte+4byte = 8byte )
				TangentElemType = TComputeShaderVertexTangentTypeSelector<TangentDataType>::VertexElementType;
				TangentXOffset = STRUCT_OFFSET(TangentType, TangentX);
				TangentZOffset = STRUCT_OFFSET(TangentType, TangentZ);
				//TangentSizeInBytes = sizeof(TangentType);
				TangentSizeInBytes = this->GetStride();

				Data.TangentBasisComponents[0] = FVertexStreamComponent(
					this,
					TangentXOffset,
					TangentSizeInBytes,
					TangentElemType,
					EVertexStreamUsage::ManualFetch
				);

				Data.TangentBasisComponents[1] = FVertexStreamComponent(
					this,
					TangentZOffset,
					TangentSizeInBytes,
					TangentElemType,
					EVertexStreamUsage::ManualFetch
				);
			}
		}
	protected:
	};

	// ----------------------------------------------------------------------------------------------------------------------------
	// 頂点バッファTexcoord
	// UE_4.22\Engine\Source\Runtime\Engine\Public\Rendering\StaticMeshVertexBuffer.h
	enum class EComputeShaderVertexTexcoordType
	{
		LowPrecision,
		Default,
	};
	template<typename TexcoordTypeT>
	struct TComputeShaderVertexTexcoordDatum
	{
		TexcoordTypeT UV;

		FORCEINLINE FVector2D GetTexcoord() const
		{
			return UV;
		}

		FORCEINLINE void SetTexcoord(FVector2D Texcoord)
		{
			UV = Texcoord;
		}

		/**
		* Serializer
		*
		* @param Ar - archive to serialize with
		* @param V - vertex to serialize
		* @return archive that was used
		*/
		friend FArchive& operator<<(FArchive& Ar, TComputeShaderVertexTexcoordDatum& Vertex)
		{
			Ar << Vertex.UV;
			return Ar;
		}
	};
	template<EComputeShaderVertexTexcoordType TexcoordType>
	struct TComputeShaderVertexTexcoordTypeSelector
	{
	};
	template<>
	struct TComputeShaderVertexTexcoordTypeSelector<EComputeShaderVertexTexcoordType::LowPrecision>
	{
		typedef FVector2DHalf TexcoordTypeT;
		static const EVertexElementType VertexElementType = VET_Half2;
		static const EPixelFormat SrvFormatType = PF_G16R16F;
		static const int SrvElementStride = 4;
	};

	template<>
	struct TComputeShaderVertexTexcoordTypeSelector<EComputeShaderVertexTexcoordType::Default>
	{
		typedef FVector2f TexcoordTypeT;
		static const EVertexElementType VertexElementType = VET_Float2;
		static const EPixelFormat SrvFormatType = PF_G32R32F;
		static const int SrvElementStride = 8;
	};

	template<EComputeShaderVertexTexcoordType TexcoordDataType>
	using TComputeShaderVertexTexcoordDatumT = TComputeShaderVertexTexcoordDatum<typename TComputeShaderVertexTexcoordTypeSelector<TexcoordDataType>::TexcoordTypeT>;

	template< EComputeShaderVertexTexcoordType TexcoordDataType = EComputeShaderVertexTexcoordType::Default, int TexcoordMaxCount = 1 >
	class FComputeShaderTexcoordVertexBufferT :
		public FComputeShaderVertexBufferBase< TComputeShaderVertexTexcoordDatumT<TexcoordDataType>, TComputeShaderVertexTexcoordTypeSelector<TexcoordDataType>::SrvFormatType, TComputeShaderVertexTexcoordTypeSelector<TexcoordDataType>::SrvElementStride, TexcoordMaxCount>
	{
	public:
		FComputeShaderTexcoordVertexBufferT()
		{
		}
		virtual ~FComputeShaderTexcoordVertexBufferT()
		{
		}

		virtual FString GetFriendlyName() const override { return TEXT("FComputeShaderTexcoordVertexBufferT"); }

		// UVバインド
		void BindVertexBuffer(const class FVertexFactory* VertexFactory, struct FStaticMeshDataType& Data, int NumBindBuffer = 1) const
		{
			Data.TextureCoordinates.Empty();
			Data.NumTexCoords = this->GetBufferMaxCount();
			{
				Data.TextureCoordinatesSRV = this->srv_;
			}
			{
				EVertexElementType UVVertexElementType = TComputeShaderVertexTexcoordTypeSelector<TexcoordDataType>::VertexElementType;
				uint32 UVSizeInBytes = this->GetStride();
				uint32 UvStride = UVSizeInBytes * this->GetBufferMaxCount();

				if (NumBindBuffer > -1)
				{
					NumBindBuffer = FMath::Min<uint32>(this->GetBufferMaxCount(), MAX_TEXCOORDS);
				}
				else
				{
					NumBindBuffer = this->GetBufferMaxCount();
				}
				check(NumBindBuffer >= 0);
				for (uint32 UVIndex = 0; UVIndex < (uint32)NumBindBuffer; UVIndex++)
				{
					Data.TextureCoordinates.Add(FVertexStreamComponent(
						this,
						UVSizeInBytes * UVIndex,
						UvStride,
						UVVertexElementType,
						EVertexStreamUsage::ManualFetch
					));
				}
			}
		}
		// LightMapUVバインド
		void BindVertexBufferAsLightMap(const class FVertexFactory* VertexFactory, struct FStaticMeshDataType& Data, int LightMapCoordinateIndex) const
		{
			LightMapCoordinateIndex = LightMapCoordinateIndex < (int32)this->GetBufferMaxCount() ? LightMapCoordinateIndex : (int32)this->GetBufferMaxCount() - 1;
			check(LightMapCoordinateIndex >= 0);

			Data.LightMapCoordinateIndex = LightMapCoordinateIndex;
			Data.NumTexCoords = this->GetBufferMaxCount();
			{
				Data.TextureCoordinatesSRV = this->srv_;
			}
			{
				EVertexElementType UVVertexElementType = TComputeShaderVertexTexcoordTypeSelector<TexcoordDataType>::VertexElementType;
				uint32 UVSizeInBytes = this->GetStride();
				uint32 UvStride = UVSizeInBytes * this->GetBufferMaxCount();


				if (LightMapCoordinateIndex >= 0 && (uint32)LightMapCoordinateIndex < this->GetBufferMaxCount())
				{
					Data.LightMapCoordinateComponent = FVertexStreamComponent(
						this,
						UVSizeInBytes * LightMapCoordinateIndex,
						UvStride,
						UVVertexElementType,
						EVertexStreamUsage::ManualFetch
					);
				}
			}
		}
	protected:
	};



	//=================================================================================================================================================
	// FDynamicMeshIndexBuffer32
	class FComputeShaderIndexBuffer32 : public FIndexBuffer
	{
	public:

		~FComputeShaderIndexBuffer32();
		void CleanUp();

		void Init(uint32 InNumVertices, bool bNeedsUAV = false, bool bNeedsCPUAccess = false);
		void Init(const TArray<uint32>& InVertices, bool bNeedsUAV = false, bool bNeedsCPUAccess = false);

		virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
		virtual void ReleaseRHI() override;

		FRHIShaderResourceView* GetSrv() const { return srv; }
		FRHIUnorderedAccessView* GetUav() const { return uav; }

		int NumIndex() const { return num; }

		// Get Current RHI State.
		ERHIAccess GetCurrentRhiState() const { return rhi_state_; }
		// Transition State and Return NextState.
		ERHIAccess TransitionRhiState(ERHIAccess next)
		{
			rhi_state_ = next;
			return rhi_state_;
		}

	protected:
		TResourceArray<uint32, INDEXBUFFER_ALIGNMENT>* Indices = nullptr;

		int	num = 0;
		bool NeedsUAV = false;

		FShaderResourceViewRHIRef srv;
		FUnorderedAccessViewRHIRef uav;

		// 現在のRHIステート. 現状では考える必要は無いが, マルチスレッドレンダリングで共有する場合はCommandListをマージする際の整合性に注意.
		ERHIAccess rhi_state_ = ERHIAccess::VertexOrIndexBuffer;

		void AllocateData(bool bNeedsCPUAccess = true);
	};
}
