#pragma once

/*
	MPMによる弾性体シミュレーション.
	SparseVoxel.
*/


#include "Math/IntVector.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"

#include <cstdint>
#include <assert.h>
#include <array>

#include "util/math_util.h"


namespace naga
{
	namespace mpm
	{
		struct Mtx3x3
		{
			// 列ベクトル.
			FVector column[3] = {};

			Mtx3x3()
			{
			}
			Mtx3x3(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22)
			{
				column[0] = FVector(m00, m10, m20);
				column[1] = FVector(m01, m11, m21);
				column[2] = FVector(m02, m12, m22);
			}
			Mtx3x3(const FVector& column0, const FVector& column1, const FVector& column2)
			{
				column[0] = column0;
				column[1] = column1;
				column[2] = column2;
			}

			static Mtx3x3 Identity()
			{
				return Mtx3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
			}
			static Mtx3x3 Zero()
			{
				return Mtx3x3(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
			}
		};

		// 転置.
		static Mtx3x3 TransposeMatrix3x3(const Mtx3x3& m)
		{
			return Mtx3x3(
				FVector(m.column[0].X, m.column[1].X, m.column[2].X),
				FVector(m.column[0].Y, m.column[1].Y, m.column[2].Y),
				FVector(m.column[0].Z, m.column[1].Z, m.column[2].Z)
			);
		};
		// 加算.
		static Mtx3x3 AddMatrix3x3(const Mtx3x3& m0, const Mtx3x3& m1)
		{
			return Mtx3x3(
				m0.column[0] + m1.column[0],
				m0.column[1] + m1.column[1],
				m0.column[2] + m1.column[2]
			);
		};
		// 減算.
		static Mtx3x3 SubtractMatrix3x3(const Mtx3x3& m0, const Mtx3x3& m1)
		{
			return Mtx3x3(
				m0.column[0] - m1.column[0],
				m0.column[1] - m1.column[1],
				m0.column[2] - m1.column[2]
			);
		};
		// 乗算.
		static Mtx3x3 MulMatrix3x3(const Mtx3x3& m0, float v)
		{
			return Mtx3x3(
				m0.column[0] * v,
				m0.column[1] * v,
				m0.column[2] * v
			);
		};
		// 乗算.
		static FVector MulMatrix3x3(const Mtx3x3& m0, const FVector& v)
		{
			return m0.column[0] * v.X + m0.column[1] * v.Y + m0.column[2] * v.Z;
		};
		// 乗算.
		static Mtx3x3 MulMatrix3x3(const Mtx3x3& m0, const Mtx3x3& m1)
		{
			return Mtx3x3(
				m0.column[0] * m1.column[0].X + m0.column[1] * m1.column[0].Y + m0.column[2] * m1.column[0].Z,
				m0.column[0] * m1.column[1].X + m0.column[1] * m1.column[1].Y + m0.column[2] * m1.column[1].Z,
				m0.column[0] * m1.column[2].X + m0.column[1] * m1.column[2].Y + m0.column[2] * m1.column[2].Z
			);
		};
		// トレース.
		static float TraceMatrix3x3(const Mtx3x3& m0)
		{
			return m0.column[0].X + m0.column[1].Y + m0.column[2].Z;
		};
		// 行列式.
		static float DeterminantMatrix3x3(const Mtx3x3& m0)
		{
			// 余因子版
			return
				m0.column[0].X * (m0.column[1].Y * m0.column[2].Z - m0.column[2].Y * m0.column[1].Z)
				+
				m0.column[0].Y * (m0.column[1].Z * m0.column[2].X - m0.column[2].Z * m0.column[1].X)
				+
				m0.column[0].Z * (m0.column[1].X * m0.column[2].Y - m0.column[2].X * m0.column[1].Y);
		}
		// 逆行列.
		static Mtx3x3 InverseMatrix3x3(const Mtx3x3& m0)
		{
			const auto c0 = FVector(
				(m0.column[1].Y * m0.column[2].Z - m0.column[2].Y * m0.column[1].Z),
				-(m0.column[0].Y * m0.column[2].Z - m0.column[2].Y * m0.column[0].Z),
				(m0.column[0].Y * m0.column[1].Z - m0.column[1].Y * m0.column[0].Z)
			);

			const auto c1 = FVector(
				-(m0.column[1].X * m0.column[2].Z - m0.column[2].X * m0.column[1].Z),
				(m0.column[0].X * m0.column[2].Z - m0.column[2].X * m0.column[0].Z),
				-(m0.column[0].X * m0.column[1].Z - m0.column[1].X * m0.column[0].Z)
			);

			const auto c2 = FVector(
				(m0.column[1].X * m0.column[2].Y - m0.column[2].X * m0.column[1].Y),
				-(m0.column[0].X * m0.column[2].Y - m0.column[2].X * m0.column[0].Y),
				(m0.column[0].X * m0.column[1].Y - m0.column[1].X * m0.column[0].Y)
			);

			// 余因子を使いまわして効率化.
			const auto det_inv = m0.column[0].X * c0.X + m0.column[0].Y * c1.X + m0.column[0].Z * c2.X;
			return Mtx3x3(c0 * det_inv, c1 * det_inv, c2 * det_inv);
		}

		FIntVector FloorIntVector(const FIntVector& v);
		FIntVector AbsIntVector(const FVector& v);
		FIntVector RightShiftIntVector(const FIntVector& v, const int shift);
		FIntVector LeftShiftIntVector(const FIntVector& v, const int shift);
		FIntVector AndMaskIntVector(const FIntVector& v, const uint32_t mask);

		FIntVector MinIntVector(const FIntVector& v0, const FIntVector& v1);
		FIntVector MaxIntVector(const FIntVector& v0, const FIntVector& v1);

		//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Nodeハンドル.
		struct SparseVoxelTreeNodeHandle
		{
			//using Type = uint64_t;
			using Type = uint32_t;

			static constexpr Type k_type_zero = Type(0u);
			static constexpr Type k_type_one = Type(1u);

			// 各情報のビットサイズ.
			static constexpr Type k_group_size_in_bits = 4;
			static constexpr Type k_level_size_in_bits = 6;
			static constexpr Type k_index_size_in_bits = 32 - (k_group_size_in_bits + k_level_size_in_bits);

			static constexpr Type k_index_offset_bits = 0;
			static constexpr Type k_level_offset_bits = k_index_size_in_bits;
			static constexpr Type k_group_offset_bits = k_level_offset_bits + k_level_size_in_bits;

			static constexpr Type k_group_mask = (k_type_one << k_group_size_in_bits) - 1;
			static constexpr Type k_level_mask = (k_type_one << k_level_size_in_bits) - 1;
			static constexpr Type k_index_mask = (k_type_one << k_index_size_in_bits) - 1;

			// 表現可能な最大値
			static constexpr size_t k_index_range_max = (size_t(1) << size_t(k_index_size_in_bits)) - 1;
			// 表現可能な最大値
			static constexpr size_t k_level_range_max = (size_t(1) << size_t(k_level_size_in_bits)) - 1;
			// 表現可能な最大値
			static constexpr size_t k_group_range_max = (size_t(1) << size_t(k_group_size_in_bits)) - 1;

			static constexpr Type k_invalid = ~Type(0);
			static constexpr Type Invalid()
			{
				return k_invalid;
			}
			
			static constexpr Type Encode(uint32_t group, uint32_t level, uint32_t index)
			{
				// インデックスがエンコード範囲に納まっているかチェック
				check(0 == (index & ~k_index_mask));

				return
					((Type(group) & k_group_mask) << k_group_offset_bits)
					| ((Type(level) & k_level_mask) << k_level_offset_bits)
					| ((Type(index) & k_index_mask) << k_index_offset_bits);
			}
			static constexpr uint32_t GetGroup(Type handle) { return uint32_t((handle >> k_group_offset_bits) & k_group_mask); }
			static constexpr uint32_t GetLevel(Type handle) { return uint32_t((handle >> k_level_offset_bits) & k_level_mask); }
			static constexpr uint32_t GetIndex(Type handle) { return uint32_t((handle >> k_index_offset_bits) & k_index_mask); }
		private:
		};
		using SparseVoxelTreeNodeHandleType = SparseVoxelTreeNodeHandle::Type;

		struct SparseVoxelTreeNodeHeader
		{
			// 子ノードフラグのビット配列の管理要素.
			using ChildBitmaskElementType = uint32_t;

			// このノードのVoxel範囲の基準Voxel位置(aabbのmin)
			FIntVector							base_voxel_ipos = FIntVector::ZeroValue;

			// ノードの値. Leafノードの場合はBrickへのハンドルを格納する予定.
			uint32_t							brick_handle = SparseVoxelTreeNodeHandle::Invalid();

			// parentノードへのハンドル.
			SparseVoxelTreeNodeHandleType	parent_handle = SparseVoxelTreeNodeHandle::Invalid();

			// childlistへのハンドル. 現在は常に最大数確保しているが, 徐々に拡張しつつアロケーションすることも検討.
			SparseVoxelTreeNodeHandleType	childlist_handle = SparseVoxelTreeNodeHandle::Invalid();

			// 現時点で確保しているchildlist_handleの指すchildlistの要素数,
			uint32_t							childlist_handle_count = 0;

			// ノードの所属レベル.
			uint8_t								level = 0;
			// マーカー. ノードの破棄チェックに利用.
			uint8_t								build_marker = 0;

			// このメンバは必ず構造体の末尾とする. 
			// childlistの各要素の使用状態bitを格納するuint32_t配列の先頭. 実際にこの構造体を確保する際には子の個数に応じて必要分uint32_tを含めたサイズで確保する.
			// そうすることで (&validchild_bitmask_head)[x] のようにアクセス可能.
			ChildBitmaskElementType	validchild_bitmask_head = 0;

		public:
			static constexpr uint32_t k_child_bitmask_elem_size_in_bit = sizeof(ChildBitmaskElementType) * 8;
			// uint32_t -> 5.
			static constexpr uint32_t k_child_bitmask_size_in_bits_log2 = (cexpr::TExpr<>::TMsb<k_child_bitmask_elem_size_in_bit>::v);
			// uint32_t -> 5.
			static constexpr uint32_t k_child_bitmask_local_index_mask = (1 << k_child_bitmask_size_in_bits_log2) - 1;
		};

		// Nodeプール.
		class SparseVoxelTreeNodePoolAllocator
		{
		public:
			~SparseVoxelTreeNodePoolAllocator()
			{
				Finalize();
			}
			bool Initialize()
			{
				return true;
			}
			void Finalize()
			{
				for (auto&& e : level_node_pool_)
				{
					check(nullptr != e);
					delete e;
					e = nullptr;
				}
				level_node_pool_.Empty();

				for (auto&& e : level_childlist_pool_)
				{
					check(nullptr != e);
					delete e;
					e = nullptr;
				}
				level_childlist_pool_.Empty();
			}

			// バケットの作成
			bool CreateLevelNodePool(int level, int node_resolution)
			{
				static constexpr auto page_size_log2 = 7;

				if (0 > level)
					return false;
				if (0 >= node_resolution)
					return false;

				const auto num_child = node_resolution * node_resolution * node_resolution;

				constexpr uint32_t bitarray_elem_byte_size = sizeof(SparseVoxelTreeNodeHeader::validchild_bitmask_head);
				constexpr uint32_t bitarray_elem_bit_size = 8 * bitarray_elem_byte_size;
				const uint32_t  bitarray_elem_count = (num_child + (bitarray_elem_bit_size - 1)) / bitarray_elem_bit_size;
				// 追加するサイズはchildlist分のビット配列サイズから基本の一個分を除いたもの.
				const uint32_t  node_body_append_byte_size = bitarray_elem_byte_size * (std::max(bitarray_elem_count, 1u) - 1);

				if (level_node_pool_.Num() <= level)
				{
					// 必要分に足りなければPool追加.
					for (int i = level_node_pool_.Num(); i <= level; ++i)
					{
						level_node_pool_.Push(nullptr);
					}
				}
				if (level_childlist_pool_.Num() <= level)
				{
					// 必要分に足りなければPool追加.
					for (int i = level_childlist_pool_.Num(); i <= level; ++i)
					{
						level_childlist_pool_.Push(nullptr);
					}
				}

				if (nullptr == level_node_pool_[level])
				{
					level_node_pool_[level] = new PagedMemoryAllocator();

					PagedMemoryAllocator::Desc node_pool_desc;
					// ノードプールの要素サイズは NodeHeaderの基本サイズにvalidchild_bitmask_headに続くビットフラグ配列分のサイズを足したもの.
					node_pool_desc.block_byte_size = sizeof(SparseVoxelTreeNodeHeader) + node_body_append_byte_size;
					node_pool_desc.block_count_per_page_log2 = page_size_log2;
					level_node_pool_[level]->Initialize(node_pool_desc);
				}

				if (nullptr == level_childlist_pool_[level])
				{
					level_childlist_pool_[level] = new PagedMemoryAllocator();
					PagedMemoryAllocator::Desc node_pool_desc;
					node_pool_desc.block_byte_size = sizeof(SparseVoxelTreeNodeHandleType) * num_child;
					node_pool_desc.block_count_per_page_log2 = page_size_log2;
					level_childlist_pool_[level]->Initialize(node_pool_desc);
				}
				return true;
			}

			// 指定したレベルのノードを確保.
			SparseVoxelTreeNodeHandleType AllocNode(uint32_t level)
			{
				check(level_node_pool_.Num() > static_cast<int>(level) && (nullptr != level_node_pool_[level]));
				// ノードはgroup0
				constexpr auto group = 0;

				auto handle = level_node_pool_[level]->Alloc();
				return SparseVoxelTreeNodeHandle::Encode(group, level, handle);
			}
			// 指定したレベルのノードのChildListを確保.
			SparseVoxelTreeNodeHandleType AllocChildList(uint32_t level)
			{
				check(level_childlist_pool_.Num() > static_cast<int>(level) && (nullptr != level_childlist_pool_[level]));
				// ノードはgroup1
				constexpr auto group = 1;

				auto handle = level_childlist_pool_[level]->Alloc();
				return SparseVoxelTreeNodeHandle::Encode(group, level, handle);
			}
			// ハンドルが指すメモリを解放. Node, ChildList共用.
			void Dealloc(SparseVoxelTreeNodeHandleType handle)
			{
				const auto g = SparseVoxelTreeNodeHandle::GetGroup(handle);
				const auto l = SparseVoxelTreeNodeHandle::GetLevel(handle);
				const auto i = SparseVoxelTreeNodeHandle::GetIndex(handle);

				check(2 > g);
				// group 0 はノード, group 1 はchildlist
				if (0 == g)
					level_node_pool_[l]->Dealloc(i);
				else
					level_childlist_pool_[l]->Dealloc(i);
			}

			// Node取得
			SparseVoxelTreeNodeHeader* GetNode(SparseVoxelTreeNodeHandleType handle)
			{
				check(0 == SparseVoxelTreeNodeHandle::GetGroup(handle));

				const auto l = SparseVoxelTreeNodeHandle::GetLevel(handle);
				const auto i = SparseVoxelTreeNodeHandle::GetIndex(handle);
				return (SparseVoxelTreeNodeHeader*)(level_node_pool_[l]->Get(i));
			}
			// Node取得
			const SparseVoxelTreeNodeHeader* GetNode(SparseVoxelTreeNodeHandleType handle) const
			{
				check(0 == SparseVoxelTreeNodeHandle::GetGroup(handle));

				const auto l = SparseVoxelTreeNodeHandle::GetLevel(handle);
				const auto i = SparseVoxelTreeNodeHandle::GetIndex(handle);
				return (SparseVoxelTreeNodeHeader*)(level_node_pool_[l]->Get(i));
			}

			// Childlist取得
			SparseVoxelTreeNodeHandleType* GetChildlist(SparseVoxelTreeNodeHandleType handle)
			{
				check(1 == SparseVoxelTreeNodeHandle::GetGroup(handle));

				const auto l = SparseVoxelTreeNodeHandle::GetLevel(handle);
				const auto i = SparseVoxelTreeNodeHandle::GetIndex(handle);
				return (SparseVoxelTreeNodeHandleType*)(level_childlist_pool_[l]->Get(i));
			}
			// Childlist取得
			const SparseVoxelTreeNodeHandleType* GetChildlist(SparseVoxelTreeNodeHandleType handle) const
			{
				check(1 == SparseVoxelTreeNodeHandle::GetGroup(handle));

				const auto l = SparseVoxelTreeNodeHandle::GetLevel(handle);
				const auto i = SparseVoxelTreeNodeHandle::GetIndex(handle);
				return (SparseVoxelTreeNodeHandleType*)(level_childlist_pool_[l]->Get(i));
			}

			// リセット. 確保メモリは維持.
			void Reset()
			{
				for (auto&& e : level_node_pool_)
				{
					check(nullptr != e);
					e->Reset();
				}

				for (auto&& e : level_childlist_pool_)
				{
					check(nullptr != e);
					e->Reset();
				}
			}

			// 直接アクセス用
			uint32_t NumLevelNodeMax(uint32_t level) const
			{
				check(static_cast<uint32_t>(level_node_pool_.Num()) > level && level_node_pool_[level]);
				return level_node_pool_[level]->NumHandle();
			}
			SparseVoxelTreeNodeHeader* GetLevelNodeDirect(uint32_t level, uint32_t index)
			{
				check(static_cast<uint32_t>(level_node_pool_.Num()) > level && level_node_pool_[level]);
				return (SparseVoxelTreeNodeHeader*)(level_node_pool_[level]->GetByIndex(index));
			}
			const SparseVoxelTreeNodeHeader* GetLevelNodeDirect(uint32_t level, uint32_t index) const
			{
				check(static_cast<uint32_t>(level_node_pool_.Num()) > level && level_node_pool_[level]);
				return (SparseVoxelTreeNodeHeader*)(level_node_pool_[level]->GetByIndex(index));
			}


			// 確保メモリサイズ取得用.
			size_t GetAllocatedMemorySize() const
			{
				size_t s = 0;
				for (auto&& e : level_node_pool_)
				{
					if (e)
						s += e->GetAllocatedMemorySize();
				}
				for (auto&& e : level_childlist_pool_)
				{
					if (e)
						s += e->GetAllocatedMemorySize();
				}
				return s;
			}

		private:
			TArray<PagedMemoryAllocator*> level_node_pool_;
			TArray<PagedMemoryAllocator*> level_childlist_pool_;
		};




		// Sparse Voxel MPM.
		/*
			// Particle data.
			TArray<FVector>	particle_position_;
			TArray<FVector>	particle_velocity_;
			TArray<naga::sparse_voxel_mpm::Mtx3x3> particle_affine_momentum_;
			TArray<naga::sparse_voxel_mpm::Mtx3x3> particle_deform_grad_;


			// --------------------------------
			// Initialize.
			desc = SparseVoxelTreeMpmSystem::GetDefaultDesc();
			desc.voxel_size = 100.0f;
			desc.debug_initial_density = 0.3f;
			desc.debug_debug_elastic_lambda = 16.0f;
			desc.debug_debug_elastic_mu = 2.5f;
			sgs_.Initialize(desc);

			// --------------------------------
			// Add New Particle.
			particle_position_.Push(pos);
			particle_velocity_.Push(vel);
			particle_affine_momentum_.Push(Mtx3x3::Zero());
			particle_deform_grad_.Push(Mtx3x3::Identity());

			// --------------------------------
			// Update.
			//	Rebuild Struct per frame.
			sgs_.Build(particle_position_);
			//	Clear Brick
			sgs_.ClearBrickData();
			//	Main update.
			sgs_.RasterizeAndUpdateGrid(sim_delta_sec, particle_position_, particle_velocity_, particle_affine_momentum_, particle_deform_grad_);


			MPM実装参考
			https://nialltl.neocities.org/articles/mpm_guide.html

			Sparse Voxel Octreeの漸進的ビルド等の実装参考
			https://people.csail.mit.edu/kuiwu/GVDB_FLIP/gvdb_flip.pdf

		*/
		class SparseVoxelTreeMpmSystem
		{
		public:
			struct LevelNodeInfo
			{
				// このLevelのNode一つのCell解像度のLog2. Leafレベルの場合はBrick一つに対応し,Brick解像度に一致.
				uint32_t	node_reso_log2 = 0u;
				// このLevelのNode一つのCell解像度. Leafレベルの場合はBrick一つに対応し,Brick解像度に一致.
				uint32_t	node_reso = 1u;

				// このLevelに所属するNodeの各軸の個数のLog2.
				uint32_t	level_reso_log2 = 0u;
				// このLevelに所属するNodeの各軸の個数.
				uint32_t	level_reso = 1u;

				uint32_t	node_child_count = 1;

				// このLevelのCell一つのワールド空間サイズ.
				float	cell_world_size = 100.0f;
				// このLevelのCell一つのワールド空間サイズの逆数.
				float	cell_world_size_inv = 1.0f / 100.0f;
			};

			struct Desc
			{
				uint32_t		num_level = 0;
				const uint32_t* level_node_resolution_log2_list = nullptr;
				uint32_t		brick_resolution_log2 = 0;
				float			voxel_size = 100.0f;

				float		debug_debug_elastic_mu = 2.5f;
				float		debug_debug_elastic_lambda = 16.0f;
				float		debug_initial_density = 0.3f;
			};
			// デフォルトである程度動作するDescを取得.
			static Desc GetDefaultDesc()
			{
				static const uint32_t level_reso_log2_list[] =
				{
					5, 4, 3
				};
				uint32_t brick_reso_log2 = 2;

				Desc desc{};
				desc.num_level = std::size(level_reso_log2_list);
				desc.level_node_resolution_log2_list = level_reso_log2_list;
				desc.brick_resolution_log2 = brick_reso_log2;

				return desc;
			}

		public:
			SparseVoxelTreeMpmSystem();
			~SparseVoxelTreeMpmSystem();

		public:
			bool Initialize(const Desc& desc);

			void Finalize();


		public:
			// 構造ビルド
			void Build(const TArray<FVector>& position_list);
			// Brickクリア
			void ClearBrickData();
			// ラスタライズ
			void RasterizeAndUpdateGrid(
				float delta_sec,
				TArray<FVector>& position_list, TArray<FVector>& velocity_list, TArray<Mtx3x3>& affine_momentum_list, TArray<Mtx3x3>& deform_grad_list
			);


		public:
			FVector	GetSystemAabbMin() const;
			// シミュレーション空間内でのVoxelIndexを計算.
			FIntVector	GetVoxelIndex3(const FVector& v) const;
			SparseVoxelTreeNodeHandleType FindNodeByVoxelIndex(const FIntVector& vindex) const;

			int NumLevel() const
			{
				return level_node_info_.Num();
			}
			const LevelNodeInfo& GetLevelInfo(int l) const
			{
				return level_node_info_[l];
			}
			const SparseVoxelTreeNodePoolAllocator& GetNodePool() const
			{
				return pool_;
			}
		private:
			SparseVoxelTreeNodeHandleType	AddNodeByVoxelIndex(const FIntVector& vindex);
			SparseVoxelTreeNodeHandleType	AddNodeByVoxelIndexWithoutRangeCheck(const FIntVector& vindex);

			// ノードを削除. 構造からの除去と関連する情報のDeallocをする.
			void								RemoveNode(const SparseVoxelTreeNodeHandleType handle);

			// VoxelIndexを指定したレベルでのノードのベースVoxelIndexに変換する(端数部をマスクしてFloor)
			FIntVector CalcLevelBaseVoxelIndex(const FIntVector& voxel_index, uint32_t level) const;

			// 親ノードの持つリスト上でのインデックスを計算する.
			uint32_t CalcChildIndex(SparseVoxelTreeNodeHeader* parent_node, SparseVoxelTreeNodeHeader* node) const;

		public:
			// Nodeプール
			SparseVoxelTreeNodePoolAllocator		pool_;
			// Brickプール. ブロックがbrick一つ分のメモリ. 実際には並列化を考慮してbrickサイズの両側にエプロン部1セルを拡張した分で確保している.
			// 4x4x4のbrickの場合、アロケーションは6x6x6セル分となる.
			PagedComponentAllocator<FVector4>	mass_brick_pool_;

			// ルートを含めた各レベルのノードの情報.
			// 各レベルのノードの解像度情報や,そのレベルに所属するノードの総数情報.
			TArray<LevelNodeInfo>				level_node_info_;

			// 最下層のVoxelLevelの各軸のCell個数のLog2.
			uint32_t	voxel_level_reso_log2_ = 0u;
			// 最下層のVoxelLevelの各軸のCell個数.
			uint32_t	voxel_level_reso_ = 1u;

			// brickのエプロン部を含んだ解像度.
			uint32_t	brick_reso_include_apron_ = 1;

			uint32_t	leaf_level_idx_ = 0;

			// ルートノードハンドル
			SparseVoxelTreeNodeHandleType	root_handle_ = SparseVoxelTreeNodeHandle::Invalid();

			bool	need_fullbuild_ = true;
			uint8_t	build_marker_ = 0;



			// パーティクル所属リーフVoxel位置xyzとパーティクル番号をエンコードしたuint64.
			// ビルド処理で構築と利用をし,ラスタライズでもリーフノード毎の近傍パーティクル探索に利用する.
			// パーティクル番号部の最上位ビットに既に登録済みかどうかを格納して後段のノード追加時のスキップに利用する.
			TArray<uint64_t> leaf_voxel_particle_id_list_;
			// leaf_voxel_particle_id_list_ の voxel位置ビット部のExclusiveScanリスト
			// ラスタライズで各リーフノードが自身のBrickへの影響パーティクルを検索するために利用する.
			TArray<int> leaf_voxel_particle_id_ex_scan_list_;

			// ExclusiveScanをもと作られた各LeafNode所属パーティクル範囲の開始インデックスリスト.
			// LeafNodeVoxel位置エンコード->対応LeafNode所属パーティクル開始インデックス.
			TArray<uint64_t> leaf_voxel_ex_scan_kind_list_;





			float elastic_lambda_ = 10.0f;
			float elastic_mu_ = 3.0f;
			float initial_density_ = 1.0f;
		};
		


		//--------------------------------------------------------------------------------------------------------------------------------------
		// MPM参考実装の検証.
		// https://nialltl.neocities.org/articles/mpm_guide.html
		//--------------------------------------------------------------------------------------------------------------------------------------
		// Moving Least Squares Material Point Method 実装
		// https://nialltl.neocities.org/articles/mpm_guide.html
		class MlsMpm2d_Base
		{
		public:
			struct Particle
			{
				FVector2D pos_;
				FVector2D vel_;
				FMatrix2x2	affine_momentum_;
				float	mass_;
			};
			struct Cell
			{
				FVector2D vel_;
				float	mass_;
				static Cell Zero()
				{
					Cell c;
					c.vel_ = FVector2D::ZeroVector;
					c.mass_ = 0.0f;
					return c;
				}
			};

			MlsMpm2d_Base() {}
			bool Initialize(int grid_reso_x, int grid_reso_y);

			void AddParticle(const FVector2D& pos, const FVector2D& vel);
			void AdvanceSimulation(float delta_sec);

			// テスト用のパーティクルセットアップ
			void SetupTestParticle();

			FIntVector	grid_reso_ = FIntVector(64, 64, 1);
			TArray<Cell> grid_param_;

			TArray<Particle> particle_;
		};

		// Moving Least Squares Material Point Method 実装
		// 初期体積計算のために更新開始前にすべてのパーティクルが追加済みである必要がある.
		// https://nialltl.neocities.org/articles/mpm_guide.html
		class MlsMpm2d_Elastic
		{
		public:
			struct Particle
			{
				FVector2D pos_;
				FVector2D vel_;
				FMatrix2x2	affine_momentum_;
				float	mass_;
				float	initial_volume_;
			};
			struct Cell
			{
				FVector2D vel_;
				float	mass_;
				static Cell Zero()
				{
					Cell c;
					c.vel_ = FVector2D::ZeroVector;
					c.mass_ = 0.0f;
					return c;
				}
			};

			MlsMpm2d_Elastic() {}
			bool Initialize(int grid_reso_x, int grid_reso_y);

			void AddParticle(const FVector2D& pos, const FVector2D& vel);
			void AdvanceSimulation(float delta_sec);

			// テスト用のパーティクルセットアップ
			void SetupTestParticle();

			FIntVector	grid_reso_ = FIntVector(64, 64, 1);
			TArray<Cell> grid_param_;

			TArray<Particle> particle_;
			TArray<FMatrix2x2> particle_deform_grad_;

			bool is_first_frame_ = true;
		};


		// Moving Least Squares Material Point Method 実装
		// 流体サンプル.
		// https://nialltl.neocities.org/articles/mpm_guide.html
		class MlsMpm2d_Fluid
		{
		public:
			struct Particle
			{
				FVector2D pos_;
				FVector2D vel_;
				FMatrix2x2	affine_momentum_;
				float	mass_;
			};
			struct Cell
			{
				FVector2D vel_;
				float	mass_;
				static Cell Zero()
				{
					Cell c;
					c.vel_ = FVector2D::ZeroVector;
					c.mass_ = 0.0f;
					return c;
				}
			};

			MlsMpm2d_Fluid() {}
			bool Initialize(int grid_reso_x, int grid_reso_y);

			void AddParticle(const FVector2D& pos, const FVector2D& vel);
			void AdvanceSimulation(float delta_sec);

			// テスト用のパーティクルセットアップ
			void SetupTestParticle();

			FIntVector	grid_reso_ = FIntVector(64, 64, 1);
			TArray<Cell> grid_param_;

			TArray<Particle> particle_;
			bool is_first_frame_ = true;
		};
	}
}
