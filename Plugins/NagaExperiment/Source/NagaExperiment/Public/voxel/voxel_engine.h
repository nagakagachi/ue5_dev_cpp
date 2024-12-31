// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Containers/BitArray.h"

#include <assert.h>
#include <array>

#include "util/entity_buffer.h"
#include "util/async_task.h"

#include "voxel_engine.generated.h"


class ANglVoxelEngine;

namespace naga
{
	struct VoxelChunkState
	{
		enum Type : int
		{
			// 無効状態
			Empty,
			// メモリ確保中 
			Allocating,
			// ロード中
			Loading,
			// アクティブ状態
			Active,
			// アンロード中
			Unloading,
			// 破棄可能
			Deletable,
		};
	};
	
	// LOD情報. 1Voxelオーバーラップ有り
	template<unsigned int COUNT_X, unsigned int COUNT_Y, unsigned int COUNT_Z>
	struct CalcLodOverlapedVoixelInfo
	{
		template<unsigned int COUNT>
		struct CalcDiv2
		{
			static constexpr unsigned int eval()
			{
				return COUNT / 2u;
			}
		};
		template<>
		struct CalcDiv2<1>
		{
			static constexpr unsigned int eval()
			{
				return 1;
			}
		};
		template<>
		struct CalcDiv2<0>
		{
			static constexpr unsigned int eval()
			{
				return 1;
			}
		};
		template<unsigned int COUNT_X, unsigned int COUNT_Y, unsigned int COUNT_Z>
		struct CalcLodVoixelCount
		{
			static constexpr unsigned int LodCount()
			{
				return 1 + CalcLodVoixelCount<CalcDiv2<COUNT_X>::eval(), CalcDiv2<COUNT_Y>::eval(), CalcDiv2<COUNT_Z>::eval()>::LodCount();
			}
			static constexpr unsigned int TotalElementCount()
			{
				// 両端で1Voxelオーバーラップするため +2 している.
				return ((COUNT_X + 2)*(COUNT_Y + 2)*(COUNT_Z + 2)) + CalcLodVoixelCount<CalcDiv2<COUNT_X>::eval(), CalcDiv2<COUNT_Y>::eval(), CalcDiv2<COUNT_Z>::eval()>::TotalElementCount();
			}
		};
		template<>
		struct CalcLodVoixelCount<1, 1, 1>
		{
			static constexpr unsigned int LodCount()
			{
				return 1;
			}
			static constexpr unsigned int TotalElementCount()
			{
				return 1;
			}
		};

		// LOD数.
		static constexpr unsigned int LodCount()
		{
			return 1 + CalcLodVoixelCount<CalcDiv2<COUNT_X>::eval(), CalcDiv2<COUNT_Y>::eval(), CalcDiv2<COUNT_Z>::eval()>::LodCount();
		}
		// LODを含めた全要素数.
		static constexpr unsigned int TotalElementCount()
		{
			// 両端で1Voxelオーバーラップするため +2 している.
			return ((COUNT_X + 2)*(COUNT_Y + 2)*(COUNT_Z + 2)) + CalcLodVoixelCount<CalcDiv2<COUNT_X>::eval(), CalcDiv2<COUNT_Y>::eval(), CalcDiv2<COUNT_Z>::eval()>::TotalElementCount();
		}

		CalcLodOverlapedVoixelInfo()
		{
			unsigned int count = 0;
			for (unsigned int i = 0; i < std::size(offsets_); ++i)
			{
				offsets_[i] = count;
				resolution_x_[i] = std::max(1u, COUNT_X >> i);
				resolution_y_[i] = std::max(1u, COUNT_Y >> i);
				resolution_z_[i] = std::max(1u, COUNT_Z >> i);
				resolution_x_overlap_[i] = resolution_x_[i] + 2;
				resolution_y_overlap_[i] = resolution_y_[i] + 2;
				resolution_z_overlap_[i] = resolution_z_[i] + 2;

				// カウントにはオーバーラップ分を追加.
				counts_[i] = ((resolution_x_overlap_[i]) * (resolution_y_overlap_[i]) * (resolution_z_overlap_[i]));
				count += counts_[i];
			}
		}

		// LOD毎のオフセット
		unsigned int offsets_[LodCount()];
		// LOD毎の要素数
		unsigned int counts_[LodCount()];

		// オーバーラップ無しのLOD毎の解像度数
		unsigned int resolution_x_overlap_[LodCount()];
		unsigned int resolution_y_overlap_[LodCount()];
		unsigned int resolution_z_overlap_[LodCount()];

		// オーバーラップ無しのLOD毎の解像度数
		unsigned int resolution_x_[LodCount()];
		unsigned int resolution_y_[LodCount()];
		unsigned int resolution_z_[LodCount()];
	};


	// シンプル実装
	// 隣接Chunkと1Voxelオーバーラップする実装. 両端オーバーラップ1Voxel方式となる.
	// +側の辺端2Voxelのほうがアクセスする近傍チャンクが半分になる他オーバーラップフェイス生成のLODパターンが減るので有利と思われるが、まずは両端1Voxelで実装する
	// バッファ自体はオーバーラップ込で確保しているため実際のChunk固有部とオーバーラップ部の扱いに注意.
	template<typename DATA_TYPE, unsigned int RESOLUTION = 16, bool DEBUG_RANGE_CHECK = false>
	struct SimpleOverlapBothVoxelChunkT
	{
		// LOD情報. X軸に関しては圧縮表現している.
		using LodInfoType = CalcLodOverlapedVoixelInfo<RESOLUTION, RESOLUTION, RESOLUTION>;
		static const LodInfoType lod_info_;

		static const unsigned int CHUNK_RESOLUTION_BASE = RESOLUTION;

		// オーバーラップを含まないチャンク基準解像度
		static constexpr unsigned int CHUNK_RESOLUTION(unsigned int lod = 0)
		{
			if (DEBUG_RANGE_CHECK)
			{
				if (LOD_COUNT() <= lod)
				{
					assert(LOD_COUNT() > lod);
					return 0;
				}
			}
			return RESOLUTION >> lod;
		}

		// オーバーラップを含むチャンク基準解像度
		static constexpr unsigned int CHUNK_RESOLUTION_WITH_OVERLAP(unsigned int lod = 0)
		{
			// 前後のオーバーラップ分を含む.
			return CHUNK_RESOLUTION(lod) + 2;
		}


		// Lod数
		static constexpr unsigned int LOD_COUNT()
		{
			return LodInfoType::LodCount();
		}
		// 最大LODインデックス
		static constexpr unsigned int LOD_MAX_INDEX()
		{
			return LOD_COUNT() - 1;
		}

		SimpleOverlapBothVoxelChunkT()
		{
		}
		~SimpleOverlapBothVoxelChunkT()
		{
			cells_.Empty();
		}
		// 確保.
		void Allocate()
		{
			// LOD込の総数を静的に計算.
			constexpr auto alloc_element_count = LodInfoType::TotalElementCount();
			cells_.SetNumUninitialized(alloc_element_count);

		}
		// 確保サイズ
		unsigned int GetAllocatedSize() const
		{
			return cells_.GetAllocatedSize();
		}
		// 値で埋める
		void Fill(bool v)
		{
			memset(cells_.GetData(), (v) ? ~0u : 0u, cells_.GetAllocatedSize());
		}

		// オーバラップ込みのX軸Row先頭を取得
		DATA_TYPE* GetXRowWithOverlap(unsigned int y, unsigned int z, unsigned int lod = 0)
		{
			const int index = lod_info_.resolution_x_overlap_[lod] * y + lod_info_.resolution_x_overlap_[lod] * lod_info_.resolution_y_overlap_[lod] * z;
			return &cells_.GetData()[index + lod_info_.offsets_[lod]];
		}
		const DATA_TYPE* GetXRowWithOverlap(unsigned int y, unsigned int z, unsigned int lod = 0) const 
		{
			const int index = lod_info_.resolution_x_overlap_[lod] * y + lod_info_.resolution_x_overlap_[lod] * lod_info_.resolution_y_overlap_[lod] * z;
			return &cells_.GetData()[index + lod_info_.offsets_[lod]];
		}
		// オーバラップを含まないX軸Row先頭を取得
		// -1でオーバーラップ部へアクセスするために符号付き引数としている.
		DATA_TYPE* GetXRow(int y, int z, unsigned int lod = 0)
		{
			// オーバーラップ部をスキップするオフセットを加算する
			const unsigned int index = 1 + (lod_info_.resolution_x_overlap_[lod] * (y + 1) + lod_info_.resolution_x_overlap_[lod] * lod_info_.resolution_y_overlap_[lod] * (z + 1));
			return &cells_.GetData()[index + lod_info_.offsets_[lod]];
		}
		const DATA_TYPE* GetXRow(int y, int z, unsigned int lod = 0) const
		{
			// オーバーラップ部をスキップするオフセットを加算する
			const unsigned int index = 1 + (lod_info_.resolution_x_overlap_[lod] * (y + 1) + lod_info_.resolution_x_overlap_[lod] * lod_info_.resolution_y_overlap_[lod] * (z + 1));
			return &cells_.GetData()[index + lod_info_.offsets_[lod]];
		}

		VoxelChunkState::Type GetState() const
		{
			return state_.load(std::memory_order_acquire);
		}
		void SetState(VoxelChunkState::Type v)
		{
			state_.store(v, std::memory_order_release);
		};

		FIntVector GetId() const
		{
			return id_;
		}
		void SetId(const FIntVector& v)
		{
			id_ = v;
		}

		unsigned int GetCurrentLodLevel() const
		{
			return current_lod_level_;
		}
		void SetCurrentLodLevel(const unsigned int v)
		{
			current_lod_level_ = FMath::Min(v, LOD_MAX_INDEX());
		}

		// -----------------------------------------------------------------------------
		void SetAnyVoxelChangeFlag(bool v)
		{
			any_voxel_changed_ = v;
		}
		bool GetAnyVoxelChangeFlag() const
		{
			return any_voxel_changed_;
		}
		// -----------------------------------------------------------------------------
		// クリア
		uint32_t GetEdgeKindBit(int dir_x, int dir_y, int dir_z) const
		{
			const auto x_shift = std::min(std::max(dir_x + 1, 0), 2);
			const auto y_shift = std::min(std::max(dir_y + 1, 0), 2) * (3);
			const auto z_shift = std::min(std::max(dir_z + 1, 0), 2) * (3 * 3);
			const auto shift = x_shift + y_shift + z_shift;
			return (0x01 << shift);
		}

		void ClearEdgeVoxelChangeFlag(unsigned int v = 0)
		{
			auto max_bit = GetEdgeKindBit(1, 1, 1);
			// 0,0,0 に対応するbitは無効.
			auto unused_bit = GetEdgeKindBit(0, 0, 0);
			// 利用する最大ビット範囲と無効ビット(0,0,0 に対応)は常に0になるようにクリア.
			auto enable_mask = ((max_bit << 1) - 1) & (~unused_bit);
			edge_voxel_changed_ = v & enable_mask;
		}
		// このチャンク自体の各方向のエッジ部変更状態
		// dir_x: [-1, 0, +1], dir_y: [-1, 0, +1], dir_z: [-1, 0, +1]
		void SetEdgeVoxelChangeFlag(int dir_x, int dir_y, int dir_z)
		{
			edge_voxel_changed_ |= (GetEdgeKindBit(dir_x, dir_y, dir_z));
		}
		// このチャンク自体の各方向のエッジ部変更状態
		// dir_x: [-1, 0, +1], dir_y: [-1, 0, +1], dir_z: [-1, 0, +1]
		bool GetEdgeVoxelChangeFlag(int dir_x, int dir_y, int dir_z) const
		{
			return (edge_voxel_changed_ & (GetEdgeKindBit(dir_x, dir_y, dir_z)));
		}
		// このチャンク自体の各方向のエッジ部変更状態
		bool GetAnyEdgeVoxelChangeFlag() const
		{
			return 0 != edge_voxel_changed_;
		}

		// クリア
		void ClearNeighborChunkChangeFlag(unsigned int v = 0)
		{
			auto max_bit = GetEdgeKindBit(1, 1, 1);
			// 0,0,0 に対応するbitは無効.
			auto unused_bit = GetEdgeKindBit(0, 0, 0);
			// 利用する最大ビット範囲と無効ビット(0,0,0 に対応)は常に0になるようにクリア.
			auto enable_mask = ((max_bit << 1) - 1) & (~unused_bit);
			neighbor_changed_ = v & enable_mask;
		}
		// このチャンクからみて各方向の近傍チャンクのオーバーラップ部変更状態
		// dir_x: [-1, 0, +1], dir_y: [-1, 0, +1], dir_z: [-1, 0, +1]
		void SetNeighborChunkChangeFlag(int dir_x, int dir_y, int dir_z)
		{
			neighbor_changed_ |= (GetEdgeKindBit(dir_x, dir_y, dir_z));
		}
		// このチャンクからみて各方向の近傍チャンクのオーバーラップ部変更状態
		// dir_x: [-1, 0, +1], dir_y: [-1, 0, +1], dir_z: [-1, 0, +1]
		bool GetNeighborChunkChangeFlag(int dir_x, int dir_y, int dir_z) const
		{
			return (neighbor_changed_ & (GetEdgeKindBit(dir_x, dir_y, dir_z)));
		}
		// このチャンクからみて各方向の近傍チャンクのオーバーラップ部変更状態
		bool GetAnyNeighborChunkChangeFlag() const
		{
			return 0 != neighbor_changed_;
		}
		// -----------------------------------------------------------------------------


		// -----------------------------------------------------------------------------
		// face_sign:	false	-> 自身の-X面へsrcの+X面をコピー
		//				true	-> 自身の+X面へsrcの-X面をコピー
		template<bool FACE_SIGN>
		void CopyOverlapFromSrcEdgeX(const SimpleOverlapBothVoxelChunkT* src)
		{
			assert(src);
			for (auto k = 0; k < CHUNK_RESOLUTION_BASE; ++k)
			{
				for (auto j = 0; j < CHUNK_RESOLUTION_BASE; ++j)
				{
					auto dst_row = GetXRowWithOverlap(j + 1, k + 1, 0);
					const auto src_row = src->GetXRow(j, k, 0);
					if (FACE_SIGN)
					{
						dst_row[CHUNK_RESOLUTION_BASE + 1] = src_row[0];
					}
					else
					{
						dst_row[0] = src_row[CHUNK_RESOLUTION_BASE - 1];
					}
				}
			}
		}
		// face_sign:	false	-> 自身の-Y面へsrcの+Y面をコピー
		//				true	-> 自身の+Y面へsrcの-Y面をコピー
		template<bool FACE_SIGN>
		void CopyOverlapFromSrcEdgeY(const SimpleOverlapBothVoxelChunkT* src)
		{
			assert(src);
			for (auto k = 0; k < CHUNK_RESOLUTION_BASE; ++k)
			{
				const auto dst_j = (FACE_SIGN) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_j = (FACE_SIGN) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				auto dst_row = GetXRowWithOverlap(dst_j, k + 1, 0);
				const auto src_row = src->GetXRow(src_j, k, 0);

				// コピー
				memcpy(dst_row + 1, src_row, sizeof(DATA_TYPE) * CHUNK_RESOLUTION_BASE);
			}
		}
		// face_sign:	false	-> 自身の-Z面へsrcの+Z面をコピー
		//				true	-> 自身の+Z面へsrcの-Z面をコピー
		template<bool FACE_SIGN>
		void CopyOverlapFromSrcEdgeZ(const SimpleOverlapBothVoxelChunkT* src)
		{
			assert(src);
			for (auto j = 0; j < CHUNK_RESOLUTION_BASE; ++j)
			{
				const auto dst_k = (FACE_SIGN) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_k = (FACE_SIGN) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				auto dst_row = GetXRowWithOverlap(j + 1, dst_k, 0);
				const auto src_row = src->GetXRow(j, src_k, 0);

				// コピー
				memcpy(dst_row + 1, src_row, sizeof(DATA_TYPE) * CHUNK_RESOLUTION_BASE);
			}
		}


		// FACE_SIGN: 自身のどのエッジにコピーするかをエッジ自身からみたエッジの符号で指定
		template<bool FACE_SIGNX, bool FACE_SIGNY>
		void CopyOverlapFromSrcEdgeXY(const SimpleOverlapBothVoxelChunkT* src)
		{
			assert(src);
			for (auto k = 0; k < CHUNK_RESOLUTION_BASE; ++k)
			{
				const auto dst_j = (FACE_SIGNY) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_j = (FACE_SIGNY) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				const auto dst_i = (FACE_SIGNX) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_i = (FACE_SIGNX) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				{
					auto dst_row = GetXRowWithOverlap(dst_j, k + 1, 0);
					const auto src_row = src->GetXRow(src_j, k, 0);
				
					dst_row[dst_i] = src_row[src_i];
				}
			}
		}
		// FACE_SIGN: 自身のどのエッジにコピーするかをエッジ自身からみたエッジの符号で指定
		template<bool FACE_SIGNX, bool FACE_SIGNZ>
		void CopyOverlapFromSrcEdgeXZ(const SimpleOverlapBothVoxelChunkT* src)
		{
			assert(src);
			for (auto j = 0; j < CHUNK_RESOLUTION_BASE; ++j)
			{
				const auto dst_k = (FACE_SIGNZ) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_k = (FACE_SIGNZ) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				const auto dst_i = (FACE_SIGNX) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_i = (FACE_SIGNX) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				{
					auto dst_row = GetXRowWithOverlap(j + 1, dst_k, 0);
					const auto src_row = src->GetXRow(j, src_k, 0);

					dst_row[dst_i] = src_row[src_i];
				}
			}
		}
		// FACE_SIGN: 自身のどのエッジにコピーするかをエッジ自身からみたエッジの符号で指定
		template<bool FACE_SIGNY, bool FACE_SIGNZ>
		void CopyOverlapFromSrcEdgeYZ(const SimpleOverlapBothVoxelChunkT* src)
		{
			assert(src);
			{
				const auto dst_k = (FACE_SIGNZ) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_k = (FACE_SIGNZ) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				const auto dst_j = (FACE_SIGNY) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_j = (FACE_SIGNY) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				{
					auto dst_row = GetXRowWithOverlap(dst_j, dst_k, 0);
					const auto src_row = src->GetXRow(src_j, src_k, 0);

					memcpy(dst_row + 1, src_row, sizeof(DATA_TYPE) * CHUNK_RESOLUTION_BASE);
				}
			}
		}
		// FACE_SIGN: 自身のどのエッジにコピーするかをエッジ自身からみたエッジの符号で指定
		template<bool FACE_SIGNX, bool FACE_SIGNY, bool FACE_SIGNZ>
		void CopyOverlapFromSrcEdgeXYZ(const SimpleOverlapBothVoxelChunkT* src)
		{
			assert(src);
			{
				const auto dst_k = (FACE_SIGNZ) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_k = (FACE_SIGNZ) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				const auto dst_j = (FACE_SIGNY) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_j = (FACE_SIGNY) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				const auto dst_i = (FACE_SIGNX) ? CHUNK_RESOLUTION_BASE + 1 : 0;
				const auto src_i = (FACE_SIGNX) ? 0 : CHUNK_RESOLUTION_BASE - 1;

				{
					auto dst_row = GetXRowWithOverlap(dst_j, dst_k, 0);
					const auto src_row = src->GetXRow(src_j, src_k, 0);
					dst_row[dst_i] = src_row[src_i];
				}
			}
		}
		// -----------------------------------------------------------------------------

	private:
		//	全セル情報. オーバーラップVoxel分を含む.
		//		LOD0,LOD1...LODMax まで格納.
		TArray<DATA_TYPE>		cells_;

		// 識別ID
		FIntVector				id_ = FIntVector::ZeroValue;

		unsigned int			current_lod_level_ = 0;

		// 近傍Chunkとオーバーラップするエッジ部のDirtyフラグ
		uint32_t				edge_voxel_changed_ = 0;

		// 各方向の近傍Chunkの変更フラグ. 
		// 変更があったChunkが自身のエッジ部の変更をチェックしてその方向の近傍Chunkのこのフラグへ変更通知をセットする.
		// 各チャンクは近傍からオーバーラップ部をコピーしてくる.
		uint32_t				neighbor_changed_ = 0;

		// ChunkのVoxel自体が変更されたか
		bool					any_voxel_changed_ = false;

		// ステート
		std::atomic < VoxelChunkState::Type> state_ = VoxelChunkState::Empty;
	};

	// LOD情報
	template<typename DATA_TYPE, unsigned int RESOLUTION, bool DEBUG_RANGE_CHECK>
	const CalcLodOverlapedVoixelInfo<RESOLUTION, RESOLUTION, RESOLUTION> SimpleOverlapBothVoxelChunkT<DATA_TYPE, RESOLUTION, DEBUG_RANGE_CHECK>::lod_info_ = {};
	
}


// 非同期タスク.
//	寿命はオーナーのANglVoxelEngineが管理する.
class NglVoxelEngineAsyncTask : public naga::FAsyncTaskBase
{
public:
	NglVoxelEngineAsyncTask();
	~NglVoxelEngineAsyncTask();

	bool Initialize(ANglVoxelEngine* owner);
	void Finalize();

private:
	// 非同期実行関数
	void AsyncUpdate() override;
private:
	ANglVoxelEngine* owner_ = nullptr;
};

// Actor.
UCLASS()
class ANglVoxelEngine : public AActor
{
	GENERATED_BODY()
	
	friend class NglVoxelEngineAsyncTask;

	// 1byteボクセル. 可能なら1bitvoxelまで圧縮できると良いかも.
	using ChunkType = naga::SimpleOverlapBothVoxelChunkT<unsigned char, 16, true>;

public:
	ANglVoxelEngine();
	~ANglVoxelEngine();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type reason) override;

public:
	virtual void OnConstruction(const FTransform& transform) override;
	virtual void Tick(float DeltaTime) override;

	// EditorMode Tick Enable.
	virtual bool ShouldTickIfViewportsOnly() const override;

	float GetVoxelSize(unsigned int lod = 0) const;
	FVector CalcChunkVoxelMinPosition(const FIntVector& chunk, const FIntVector& pos, unsigned int lod = 0) const;
	FVector CalcChunkVoxelCenterPosition(const FIntVector& chunk, const FIntVector& pos, unsigned int lod = 0) const;

private:
	// 完全に破棄
	void FinalizeVoxel();

	UInstancedStaticMeshComponent* GetChunkMeshComponentFromPool();
	void RestoreChunkMeshComponentToPool(UInstancedStaticMeshComponent* comp);


	//void UpdateStreamInChunk(const TArray<FIntVector>& chunk_id_array);
	//void UpdateStreamOutChunk(const TArray<FIntVector>& chunk_id_array);
	void UpdateRenderChunk(const TArray<FIntVector>& render_dirty_chunk_id_array);

	// デバッグ用のキューブ描画.
	void UpdateRenderChunkDebugCube(const TArray<FIntVector>& render_dirty_chunk_id_array);
	// SurfaceNetsによるポリゴン生成.
	void UpdateRenderChunkSurfaceNets_NaiveVoxel(const TArray<FIntVector>& render_dirty_chunk_id_array);

	void SyncUpdate();
	void AsyncUpdate();

private:
	void GenerateChunkFromNoise(ChunkType& out_chunk, float default_chunk_gen_noise_scale, int noise_octave_count) const;

private:
	// メインスレッドで変更してAsyncへ読み取るパラメータ
	struct Main2AsyncParam
	{
		FVector									important_position_ = FVector::ZeroVector;
		FVector									important_position_prev_ = FVector::ZeroVector;
	};
	Main2AsyncParam main2AsyncParam_[2];

	long long									async_micro_sec_ = {};

	float										async_fast_terminate_sec_ = (1.0f / 60.0f) * 0.8f;// 非同期処理を適当な時間内に切り上げる
	//float										async_fast_terminate_sec_ = 1000.0f;// 非同期処理が完全に終了するまで走らせる.

public:
	// どれくらい離れたチャンクまでストリームインするか(水平面)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int										stream_in_chunk_range_horizontal		= 3;
	// どれくらい離れたチャンクまでストリームインするか(高さ方向)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int										stream_in_chunk_range_vertical			= 2;

	// どれくらい離れたチャンクをストリームアウトするか
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int									stream_out_chunk_range					= 4;

	// LOD0のVoxelサイズ.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float									voxel_size_ = 100.0f;

private:

	// メインのHashMap
	TMap<FIntVector, ChunkType*>			voxel_chunk_map_;

	// ------------------------------------------------------------------------------------------------------------------------------------------
	// InstancedMeshで可視化する場合
	UPROPERTY()
	UStaticMesh*										voxel_mesh_ = nullptr; // デバッグ表示用メッシュアセット

	// マテリアル
	UPROPERTY(EditAnywhere)
	UMaterialInterface*									material_;

	UPROPERTY()
	TMap<FIntVector, UInstancedStaticMeshComponent*>	chunk_mesh_component_map_;

	UPROPERTY()
	TArray<UInstancedStaticMeshComponent*>				chunk_mesh_component_pool_;
	// ------------------------------------------------------------------------------------------------------------------------------------------
	// ProceduralMeshで可視化する場合
	UPROPERTY()
	TMap<FIntVector, class UProceduralMeshComponent*>	chunk_proc_mesh_component_map_;
	// ------------------------------------------------------------------------------------------------------------------------------------------




public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int													min_lod_level_ = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float												default_chunk_noise_scale_ = 0.0006f;

	// 非同期タスク
	NglVoxelEngineAsyncTask									async_task_;

	TArray<FIntVector>										stream_out_chunk_array_[2];
	TArray<FIntVector>										stream_in_chunk_array_[2];

	TArray<FIntVector>										stream_out_chunk_complete_array_;
	TArray<FIntVector>										stream_in_chunk_complete_array_;


	// 描画更新が必要なチャンクのID
	TArray<FIntVector>										render_dirty_chunk_id_array_;

};