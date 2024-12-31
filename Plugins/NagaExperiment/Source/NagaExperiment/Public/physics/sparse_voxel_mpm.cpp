
#include "sparse_voxel_mpm.h"

#include <chrono>
// 並列処理用.
#include "Runtime/Core/Public/Async/ParallelFor.h"


namespace naga
{
	namespace mpm
	{
		static const bool k_debug_log = false;



		FIntVector AbsIntVector(const FIntVector& v)
		{
			return FIntVector(FMath::Abs(v.X), FMath::Abs(v.Y), FMath::Abs(v.Z));
		}
		FIntVector FloorIntVector(const FVector& v)
		{
			return FIntVector(floorf(v.X), floorf(v.Y), floorf(v.Z));
		}
		FIntVector RightShiftIntVector(const FIntVector& v, const int shift)
		{
			return FIntVector(v.X >> shift, v.Y >> shift, v.Z >> shift);
		}
		FIntVector LeftShiftIntVector(const FIntVector& v, const int shift)
		{
			return FIntVector(v.X << shift, v.Y << shift, v.Z << shift);
		}
		FIntVector AndMaskIntVector(const FIntVector& v, const uint32_t mask)
		{
			return FIntVector(v.X & mask, v.Y & mask, v.Z & mask);
		}

		FIntVector MinIntVector(const FIntVector& v0, const FIntVector& v1)
		{
			return FIntVector(FMath::Min(v0.X, v1.X), FMath::Min(v0.Y, v1.Y), FMath::Min(v0.Z, v1.Z));
		}
		FIntVector MaxIntVector(const FIntVector& v0, const FIntVector& v1)
		{
			return FIntVector(FMath::Max(v0.X, v1.X), FMath::Max(v0.Y, v1.Y), FMath::Max(v0.Z, v1.Z));
		}



		// ------------------------------------------------------------------------------------------------------------------------
		// ------------------------------------------------------------------------------------------------------------------------
		SparseVoxelTreeMpmSystem::SparseVoxelTreeMpmSystem()
		{
		}
		SparseVoxelTreeMpmSystem::~SparseVoxelTreeMpmSystem()
		{
		}
		bool SparseVoxelTreeMpmSystem::Initialize(const Desc& desc)
		{
			const auto total_level_count = desc.num_level + 1;
			leaf_level_idx_ = total_level_count - 1;

			level_node_info_.SetNumUninitialized(total_level_count);

			// ルートレベルのノードは単一なので0
			uint32_t total_reso_log2 = 0;
			// ルートからリーフのひとつ上までトップダウン処理.
			for (uint32_t i = 0; i < leaf_level_idx_; ++i)
			{
				level_node_info_[i].node_reso_log2 = desc.level_node_resolution_log2_list[i];
				level_node_info_[i].node_reso = 1 << level_node_info_[i].node_reso_log2;

				level_node_info_[i].level_reso_log2 = total_reso_log2;
				level_node_info_[i].level_reso = 1 << level_node_info_[i].level_reso_log2;

				level_node_info_[i].node_child_count = level_node_info_[i].node_reso * level_node_info_[i].node_reso * level_node_info_[i].node_reso;

				total_reso_log2 += level_node_info_[i].node_reso_log2;
			}
			// リーフ情報.
			level_node_info_[leaf_level_idx_].node_reso_log2 = desc.brick_resolution_log2;
			level_node_info_[leaf_level_idx_].node_reso = 1 << level_node_info_[leaf_level_idx_].node_reso_log2;
			level_node_info_[leaf_level_idx_].level_reso_log2 = total_reso_log2;
			level_node_info_[leaf_level_idx_].level_reso = 1 << level_node_info_[leaf_level_idx_].level_reso_log2;
			level_node_info_[leaf_level_idx_].node_child_count = level_node_info_[leaf_level_idx_].node_reso * level_node_info_[leaf_level_idx_].node_reso * level_node_info_[leaf_level_idx_].node_reso;
			level_node_info_[leaf_level_idx_].cell_world_size = desc.voxel_size;
			level_node_info_[leaf_level_idx_].cell_world_size_inv = 1.0f / level_node_info_[leaf_level_idx_].cell_world_size;

			// リーフ以外のレベルをボトムアップ処理.
			for (uint32_t i = 0; i < leaf_level_idx_; ++i)
			{
				const auto i_rev = leaf_level_idx_ - 1 - i;

				level_node_info_[i_rev].cell_world_size = level_node_info_[i_rev + 1].node_reso * level_node_info_[i_rev + 1].cell_world_size;
				level_node_info_[i_rev].cell_world_size_inv = 1.0f / level_node_info_[i_rev].cell_world_size;
			}

			// Voxelレベルの情報
			{
				voxel_level_reso_log2_ = level_node_info_[leaf_level_idx_].level_reso_log2 + desc.brick_resolution_log2;
				voxel_level_reso_ = 1 << voxel_level_reso_log2_;
			}

			{
				// brickのエプロン部込の解像度. Brickの各面を1セルずつ拡張(エプロン)する.
				brick_reso_include_apron_ = (1 << desc.brick_resolution_log2) + 2;
			}

			// Node用のPool作成
			for (uint32_t i = 0; i <= leaf_level_idx_; ++i)
			{
				pool_.CreateLevelNodePool(i, level_node_info_[i].node_reso);
			}

			// BrickPool作成
			{

				decltype(mass_brick_pool_)::Desc brick_pool_desc = {};

				// 1ブロックはBrickサイズ^3 + エプロン拡張分
				//brick_pool_desc.block_size = 1 << (level_node_info_[leaf_level_idx_].node_reso_log2 * 3);
				// Brickアロケータは1brickにつきエプロン部を考慮したメモリを確保.
				brick_pool_desc.block_size = brick_reso_include_apron_ * brick_reso_include_apron_ * brick_reso_include_apron_;

				// ページサイズは 2^5 = 32 ブロックとしておく.
				brick_pool_desc.block_count_per_page_log2 = 5;

				mass_brick_pool_.Initialize(brick_pool_desc);
			}

			root_handle_ = SparseVoxelTreeNodeHandle::k_invalid;
			need_fullbuild_ = true;
			build_marker_ = 0;



			{
				elastic_lambda_ = desc.debug_debug_elastic_lambda;
				elastic_mu_ = desc.debug_debug_elastic_mu;
				initial_density_ = desc.debug_initial_density;
			}

			return true;
		}

		void SparseVoxelTreeMpmSystem::Finalize()
		{
			pool_.Finalize();
			mass_brick_pool_.Finalize();
		}
		FVector	SparseVoxelTreeMpmSystem::GetSystemAabbMin() const
		{
			// 現時点では中心がワールド原点としているが, オフセットすることもも可能.
			const auto aabb_min = -0.5f * (level_node_info_[0].cell_world_size * level_node_info_[0].node_reso);
			return FVector(aabb_min);
		}
		FIntVector	SparseVoxelTreeMpmSystem::GetVoxelIndex3(const FVector& v) const
		{
			const auto aabb_min = GetSystemAabbMin();
			return FloorIntVector((v - aabb_min) * level_node_info_[leaf_level_idx_].cell_world_size_inv);
		}




		// ------------------------------------------------------------------------------------------------------------------------
		// ノードヘッダに対する操作ヘルパ
		struct SparseVoxelTreeNodeHeaderHelper
		{
			// 子ノードリストビット配列をクリア.
			static void ClearChildBit(SparseVoxelTreeNodeHeader* node, uint32_t child_count_max)
			{
				const auto cnt = (SparseVoxelTreeNodeHeader::k_child_bitmask_elem_size_in_bit <= child_count_max) ? (child_count_max >> SparseVoxelTreeNodeHeader::k_child_bitmask_size_in_bits_log2) : 1;
				memset((&node->validchild_bitmask_head), 0, sizeof(SparseVoxelTreeNodeHeader::ChildBitmaskElementType) * cnt);
			}

			// ノードヘッダの情報をリセット.
			static void ResetNode(SparseVoxelTreeNodeHeader* node, uint32_t child_count_max)
			{
				*node = {};
				// 子ノードリストビット配列を別途クリア
				ClearChildBit(node, child_count_max);
			}

			// 子ノードリストビット配列の指定インデックスの真偽値を取得.
			static bool GetChildBit(const SparseVoxelTreeNodeHeader* node, uint32_t i)
			{
				const auto elem_i = SparseVoxelTreeNodeHeader::ChildBitmaskElementType(i) >> SparseVoxelTreeNodeHeader::k_child_bitmask_size_in_bits_log2;
				const auto local_i = SparseVoxelTreeNodeHeader::ChildBitmaskElementType(1) << (SparseVoxelTreeNodeHeader::ChildBitmaskElementType(i) & SparseVoxelTreeNodeHeader::k_child_bitmask_local_index_mask);

				return 0 != ((&node->validchild_bitmask_head)[elem_i] & local_i);
			}
			// 子ノードリストビット配列の指定インデックスの真偽値を設定.
			static void SetChildBit(SparseVoxelTreeNodeHeader* node, uint32_t i, bool v)
			{
				const auto elem_i = SparseVoxelTreeNodeHeader::ChildBitmaskElementType(i) >> SparseVoxelTreeNodeHeader::k_child_bitmask_size_in_bits_log2;
				const auto local_i = SparseVoxelTreeNodeHeader::ChildBitmaskElementType(1) << (SparseVoxelTreeNodeHeader::ChildBitmaskElementType(i) & SparseVoxelTreeNodeHeader::k_child_bitmask_local_index_mask);
				if (v)
					(&node->validchild_bitmask_head)[elem_i] |= local_i;
				else
					(&node->validchild_bitmask_head)[elem_i] &= ~local_i;
			}

			// 子ノードリストビット配列に非ゼロ要素があれば真を返す.
			static bool AnyChildBit(const SparseVoxelTreeNodeHeader* node, uint32_t child_count_max)
			{
				const auto cnt = (SparseVoxelTreeNodeHeader::k_child_bitmask_elem_size_in_bit <= child_count_max) ? (child_count_max >> SparseVoxelTreeNodeHeader::k_child_bitmask_size_in_bits_log2) : 1;
				uint32_t i = 0u;
				for (; i < cnt && 0 == (&node->validchild_bitmask_head)[i]; ++i)
				{
				}
				return (i != cnt);
			}

			// ------------------------------------------------------------------------------------------------------------------------
		};


		constexpr uint64_t encoded_particle_id_bitwidth = 19;
		constexpr uint64_t encoded_registered_bit_mask = 1 << (encoded_particle_id_bitwidth - 1);
		constexpr uint64_t encoded_voxel_id_mask = ~uint64_t((1 << encoded_particle_id_bitwidth) - 1);
		constexpr uint32_t encoded_particle_id_mask_without_registerdbit = ((1 << static_cast<uint32_t>(encoded_particle_id_bitwidth)) - 1) >> 1;

		// ノード探索 VoxelIndex版.
		SparseVoxelTreeNodeHandleType SparseVoxelTreeMpmSystem::FindNodeByVoxelIndex(const FIntVector& vindex) const
		{
			auto root = pool_.GetNode(root_handle_);

			if (0 > vindex.X || 0 > vindex.Y || 0 > vindex.Z ||
				voxel_level_reso_ <= static_cast<uint32_t>(vindex.X) || voxel_level_reso_ <= static_cast<uint32_t>(vindex.Y) || voxel_level_reso_ <= static_cast<uint32_t>(vindex.Z))
			{
				// 範囲外キャンセル.
				return SparseVoxelTreeNodeHandle::k_invalid;
			}

			auto cur_handle = root_handle_;
			auto cur_node = root;
			// ルートの子から開始のために l = 1.
			for (uint32_t l = 1; l <= leaf_level_idx_; ++l)
			{
				const auto reso_log2 = level_node_info_[l - 1].node_reso_log2;
				// レベル l 空間でのVoxel座標
				const auto voxel_local_pos = vindex - cur_node->base_voxel_ipos;
				const auto lx_ipos = RightShiftIntVector(voxel_local_pos, voxel_level_reso_log2_ - level_node_info_[l].level_reso_log2);

				// 親ノードのchildlist内インデックスを計算.
				const uint32_t local_cell_index = lx_ipos.X + (lx_ipos.Y << reso_log2) + (lx_ipos.Z << (reso_log2 + reso_log2));
				if (SparseVoxelTreeNodeHeaderHelper::GetChildBit(cur_node, local_cell_index))
				{
					// 子ノード取得
					auto childlist = pool_.GetChildlist(cur_node->childlist_handle);
					cur_handle = childlist[local_cell_index];
					cur_node = pool_.GetNode(cur_handle);
				}
				else
				{
					break;
				}
			}
			return cur_handle;
		}
		// Voxelインデックスでノード追加. VoxelインデックスはGetVoxelIndex3()でワールド座標から変換する.
		SparseVoxelTreeNodeHandleType SparseVoxelTreeMpmSystem::AddNodeByVoxelIndex(const FIntVector& vindex)
		{
			if (0 > vindex.X || 0 > vindex.Y || 0 > vindex.Z ||
				voxel_level_reso_ <= static_cast<uint32_t>(vindex.X) || voxel_level_reso_ <= static_cast<uint32_t>(vindex.Y) || voxel_level_reso_ <= static_cast<uint32_t>(vindex.Z))
			{
				// 範囲外キャンセル.
				return SparseVoxelTreeNodeHandle::k_invalid;
			}
			// 範囲チェックしてから処理.
			return AddNodeByVoxelIndexWithoutRangeCheck(vindex);
		}
		SparseVoxelTreeNodeHandleType	SparseVoxelTreeMpmSystem::AddNodeByVoxelIndexWithoutRangeCheck(const FIntVector& vindex)
		{
			auto root = pool_.GetNode(root_handle_);
			auto parent_handle = root_handle_;
			auto parent_node = root;
			// ルートの子から開始のために l = 1.
			for (uint32_t l = 1; l <= leaf_level_idx_; ++l)
			{
				const auto reso_log2 = level_node_info_[l - 1].node_reso_log2;

				// レベル l 空間でのVoxel座標
				const auto voxel_local_pos = vindex - parent_node->base_voxel_ipos;
				// Voxelレベル解像度と自身のレベルの解像度の差分だけ右シフトしてこのレベル解像度でのインデックスを計算.
				const auto lx_ipos = RightShiftIntVector(voxel_local_pos, voxel_level_reso_log2_ - level_node_info_[l].level_reso_log2);

				// 親ノードのchildlist内インデックスを計算.
				const uint32_t local_cell_index = lx_ipos.X + (lx_ipos.Y << reso_log2) + (lx_ipos.Z << (reso_log2 + reso_log2));

				if (SparseVoxelTreeNodeHeaderHelper::GetChildBit(parent_node, local_cell_index))
				{
					// 子ノード取得
					auto childlist = pool_.GetChildlist(parent_node->childlist_handle);

					parent_handle = childlist[local_cell_index];
					parent_node = pool_.GetNode(parent_handle);
				}
				else
				{
					// 親の子ノードリストが十分でなければ確保
					if (SparseVoxelTreeNodeHandle::k_invalid == parent_node->childlist_handle || parent_node->childlist_handle_count <= local_cell_index)
					{
						const auto num_child_max = level_node_info_[l - 1].node_child_count;

						// 親ノード用に子ノードリスト確保. 現実装では子の最大数分確保.
						const auto new_childlist_handle = pool_.AllocChildList(l - 1);

						// 必要なら古いリストからコピー.
						if (SparseVoxelTreeNodeHandle::k_invalid != parent_node->childlist_handle)
						{
							const auto old_size = parent_node->childlist_handle_count;
							const auto old_childlist = pool_.GetChildlist(parent_node->childlist_handle);
							auto new_childlist = pool_.GetChildlist(new_childlist_handle);

							// コピー
							memcpy(new_childlist, old_childlist, sizeof(SparseVoxelTreeNodeHandleType) * old_size);

							// 解放
							pool_.Dealloc(parent_node->childlist_handle);
							parent_node->childlist_handle = SparseVoxelTreeNodeHandle::k_invalid;
						}
						// 子ノードリストハンドルセット
						parent_node->childlist_handle = new_childlist_handle;
						// 確保分へ更新
						parent_node->childlist_handle_count = num_child_max;
					}

					// 新規ノード生成.
					const auto new_node_handle = pool_.AllocNode(l);
					auto new_node = pool_.GetNode(new_node_handle);
					// 新規ノードセットアップ
					{
						// リセット.
						SparseVoxelTreeNodeHeaderHelper::ResetNode(new_node, level_node_info_[l].node_child_count);
						new_node->level = l;
						new_node->parent_handle = parent_handle;
						new_node->build_marker = build_marker_;// 最新のビルドマーカーを設定.
						new_node->base_voxel_ipos = CalcLevelBaseVoxelIndex(vindex, l);

						// Leafの場合はBrickを割り当て
						if (l == leaf_level_idx_)
							new_node->brick_handle = mass_brick_pool_.Alloc();

						// 親のリストに自身を登録.
						auto parent_childlist = pool_.GetChildlist(parent_node->childlist_handle);
						parent_childlist[local_cell_index] = new_node_handle;
						// 親の子ノードリストビット配列を更新.
						SparseVoxelTreeNodeHeaderHelper::SetChildBit(parent_node, local_cell_index, true);
					}

					parent_handle = new_node_handle;
					parent_node = new_node;
				}
			}
			return parent_handle;
		}

		// ノードを削除. 構造からの除去と関連する情報のDeallocをする.
		void SparseVoxelTreeMpmSystem::RemoveNode(const SparseVoxelTreeNodeHandleType node_handle)
		{
			auto p_node = pool_.GetNode(node_handle);
			check(p_node);

			// 親のリストから除去
			if (SparseVoxelTreeNodeHandle::k_invalid != p_node->parent_handle)
			{
				auto parent_node = pool_.GetNode(p_node->parent_handle);
				check(parent_node);
				check(SparseVoxelTreeNodeHandle::k_invalid != parent_node->childlist_handle);
				auto parent_child_list = pool_.GetChildlist(parent_node->childlist_handle);

				const auto local_cell_index = CalcChildIndex(parent_node, p_node);
				check(parent_child_list[local_cell_index] == node_handle);
				check(SparseVoxelTreeNodeHeaderHelper::GetChildBit(parent_node, local_cell_index));

				// 子ノードリストの該当インデックスを無効化
				SparseVoxelTreeNodeHeaderHelper::SetChildBit(parent_node, local_cell_index, false);

				// 一応無効ハンドルを入れておく? ChildBitで管理されているので不要だが.
				parent_child_list[local_cell_index] = SparseVoxelTreeNodeHandle::k_invalid;
			}

			// 子ノードリストを削除
			if (SparseVoxelTreeNodeHandle::k_invalid != p_node->childlist_handle)
			{
				pool_.Dealloc(p_node->childlist_handle);
			}

			if (SparseVoxelTreeNodeHandle::k_invalid != p_node->brick_handle)
			{
				// Brickを確保している場合はその解放.
				mass_brick_pool_.Dealloc(p_node->brick_handle);
			}

			// 破棄.
			pool_.Dealloc(node_handle);
		}

		// VoxelIndexを指定したレベルでのノードのベースVoxelIndexに変換する(端数部をマスクしてFloor)
		FIntVector SparseVoxelTreeMpmSystem::CalcLevelBaseVoxelIndex(const FIntVector& voxel_index, uint32_t level) const
		{
			// ノードのAABBの基準Voxel座標を計算するためのマスク.
			const uint32_t base_voxel_mask = (1u << (voxel_level_reso_log2_ - level_node_info_[level].level_reso_log2)) - 1u;
			// このノードがカバーするVoxel領域の基準座標(aabbのmin)
			return AndMaskIntVector(voxel_index, ~(base_voxel_mask));
		}

		// 親ノードの持つリスト上でのインデックスを計算する.
		uint32_t SparseVoxelTreeMpmSystem::CalcChildIndex(SparseVoxelTreeNodeHeader* parent_node, SparseVoxelTreeNodeHeader* node) const
		{
			const auto reso_log2 = level_node_info_[node->level - 1].node_reso_log2;
			// レベル l 空間でのVoxel座標
			const auto voxel_local_pos = node->base_voxel_ipos - parent_node->base_voxel_ipos;
			const auto lx_ipos = RightShiftIntVector(voxel_local_pos, voxel_level_reso_log2_ - level_node_info_[node->level].level_reso_log2);
			// 親ノードのchildlist内インデックスを計算.
			return lx_ipos.X + (lx_ipos.Y << reso_log2) + (lx_ipos.Z << (reso_log2 + reso_log2));
		}

		void SparseVoxelTreeMpmSystem::Build(const TArray<FVector>& position_list)
		{
			// コーナー位置用のテーブル.
			const FIntVector corner_offsets[] =
			{
				FIntVector(-1, -1, -1),   FIntVector(+1, -1, -1),   FIntVector(-1, +1, -1),   FIntVector(+1, +1, -1),
				FIntVector(-1, -1, +1),   FIntVector(+1, -1, +1),   FIntVector(-1, +1, +1),   FIntVector(+1, +1, +1),
			};

			// ノード追加.
			auto AddNode = [](SparseVoxelTreeMpmSystem& sys, const FVector& pos)
			{
				auto root = sys.pool_.GetNode(sys.root_handle_);
				// ルートのセルサイズとルートセル数の半分でワールドAABBのmin座標計算.
				const auto aabb_min = sys.GetSystemAabbMin();
				// ワールド座標からVoxelインデックス位置
				const FIntVector voxel_ipos = sys.GetVoxelIndex3(pos);
				// Voxelインデックス登録.
				return sys.AddNodeByVoxelIndex(voxel_ipos);
			};


			// ビルドマーカー更新.
			++build_marker_;


			auto	progress_time_start = std::chrono::system_clock::now();
			bool is_full_build = need_fullbuild_;


			const auto num_particle = position_list.Num();


			// 必要ならリサイズ
			if (leaf_voxel_particle_id_list_.Max() < num_particle)
			{
				leaf_voxel_particle_id_list_.Reserve(num_particle);
			}
			// 空にする(確保メモリは維持)
			leaf_voxel_particle_id_list_.Empty(leaf_voxel_particle_id_list_.Max());

			// Scanリストは都度PushするのでEmptyにするだけにしておく
			leaf_voxel_particle_id_ex_scan_list_.Empty(leaf_voxel_particle_id_ex_scan_list_.Max());

			{
				if (need_fullbuild_)
				{
					// Full Buildの場合はIncrementalBuildフェーズの前に全リセットとパーティクル中心のみの登録を実行する.
					// フルビルドフラグ無効化
					need_fullbuild_ = false;

					// フルビルドのためにリセット.
					pool_.Reset();
					root_handle_ = SparseVoxelTreeNodeHandle::Invalid();

					// ルート再確保
					root_handle_ = pool_.AllocNode(0);
					auto root = pool_.GetNode(root_handle_);
					// ルートノードリセット.
					SparseVoxelTreeNodeHeaderHelper::ResetNode(root, level_node_info_[0].node_child_count);


					// First Pass. パーティクル中心位置のみを追加.
					for (auto i = 0; i < num_particle; ++i)
					{
						const auto& e = position_list[i];
						AddNode(*this, e);
					}
				}

				// Incremental Build (あるいはSecondPass).
				//	パーティクルのAABBコーナーで未登録のCellについてのみ処理をすることでほぼビルド済みの構造を高速に完全なビルド状態にする
				//	初回フレームでは先行するフルビルド用のリセットとパーティクル中心登録とともに実行され,それ以降はIncrementalBuildだけが実行される.
				for (auto i = 0; i < num_particle; ++i)
				{
					const auto& e = position_list[i];

					const auto vi = GetVoxelIndex3(e);

					FIntVector corner_index[8];
					int corner_index_cnt = 0;

					// ボトルネックが木構造探索(FindNodeByVoxelIndex)であるため, ローカルでの重複除去によって木構造探索の回数を削減する.
					for (int ci0 = 0; ci0 < std::size(corner_index); ++ci0)
					{
						const auto new_corner_index = CalcLevelBaseVoxelIndex(vi + corner_offsets[ci0], leaf_level_idx_);

						// この時点で範囲外チェック
						if (0 > new_corner_index.X || 0 > new_corner_index.Y || 0 > new_corner_index.Z ||
							voxel_level_reso_ <= static_cast<uint32_t>(new_corner_index.X) || voxel_level_reso_ <= static_cast<uint32_t>(new_corner_index.Y) || voxel_level_reso_ <= static_cast<uint32_t>(new_corner_index.Z))
						{
							continue;
						}

						// 重複除去をローカルに実行する.
						int ci1 = 0;

						// こちらは条件ミス
						for (; (ci1 < corner_index_cnt) && (new_corner_index != corner_index[ci1]); ++ci1) {}

						if (corner_index_cnt == ci1)
						{
							corner_index[corner_index_cnt] = new_corner_index;
							++corner_index_cnt;
						}
					}

					for (int ci = 0; ci < corner_index_cnt; ++ci)
					{
						// 登録済みかどうか探索.
						auto handl = FindNodeByVoxelIndex(corner_index[ci]);

						// invalidハンドルならありえない最大値が変えるのでチェック不要とする.
						const auto find_level = SparseVoxelTreeNodeHandle::GetLevel(handl);


						// パーティクルIDのがエンコードビット表現範囲に収まっているかチェック
						check(i == (i & ((1 << encoded_particle_id_bitwidth) - 1)));
						// エンコード
						// 登録済みの場合は特定ビットを1に設定
						const auto registered_bit_and_particle_id = (find_level == leaf_level_idx_) ? (i | (1 << 18)) : (i);
						const auto enc = math::EncodeInt4ToU15U15U15U19(FIntVector4(corner_index[ci].X, corner_index[ci].Y, corner_index[ci].Z, registered_bit_and_particle_id));
						// 登録済みかどうかに関わらずリストに追加する
						// リストのサイズが大きくなり,ソートコストが増加するが,このリストはそのままラスタライズパスでも利用するので全体コストは低下するはず.
						leaf_voxel_particle_id_list_.Push(enc);

						// 登録済みならマーカー更新.
						if (find_level == leaf_level_idx_)
							pool_.GetNode(handl)->build_marker = build_marker_;
					}
				}
			}

			// 未使用Leafノードの破棄.
			if (!is_full_build)
			{
				// Leafレベルを直接巡回してマーカー更新されていないものを削除する
				const auto num_leaf_node_max = pool_.NumLevelNodeMax(leaf_level_idx_);
				for (auto i = 0u; i < num_leaf_node_max; ++i)
				{
					if (auto p_node = pool_.GetLevelNodeDirect(leaf_level_idx_, i))
					{
						if (p_node->build_marker != build_marker_)
						{
							// マーカーが古いので破棄する.
							// グループ0(ノード), レベル, 要素インデックスからハンドルを作成して破棄.
							const auto node_handle = SparseVoxelTreeNodeHandle::Encode(0, p_node->level, i);

							// ノードを削除. 現在は内部でBrickも破棄.
							RemoveNode(node_handle);
						}
					}
				}
			}

			// 新規ノードの追加.
			{
				const auto num_leaf_voxel_particle_id_list = leaf_voxel_particle_id_list_.Num();

				// voxel位置-登録済みフラグ-パーティクルIDエンコードリストをソート
				std::sort(leaf_voxel_particle_id_list_.GetData(), leaf_voxel_particle_id_list_.GetData() + num_leaf_voxel_particle_id_list);

				// Exclusive Scan計算.
				if (0 < num_leaf_voxel_particle_id_list)
				{
					// 登録済みビットが0の場合はソートによって同一Voxel位置の最初の要素になっているので,最初の要素の登録済みビットを調べてスキップするかどうかを判断できる.

					// 0番は必ず追加.
					leaf_voxel_particle_id_ex_scan_list_.Push(0);
					uint64_t prev_masked_val = leaf_voxel_particle_id_list_[0] & encoded_voxel_id_mask;
					for (auto i = 1; i < num_leaf_voxel_particle_id_list; ++i)
					{
						// voxel位置部分だけをマスク
						const auto masked_val = leaf_voxel_particle_id_list_[i] & encoded_voxel_id_mask;

						if ((prev_masked_val != masked_val))
						{
							leaf_voxel_particle_id_ex_scan_list_.Push(i);

							prev_masked_val = masked_val;
						}
					}
				}

				// 後段のためにCompactリストの要素を必要分確保とクリア.
				{
					if (leaf_voxel_ex_scan_kind_list_.Max() < leaf_voxel_particle_id_ex_scan_list_.Num())
					{
						leaf_voxel_ex_scan_kind_list_.Reserve(leaf_voxel_particle_id_ex_scan_list_.Num());
					}
					leaf_voxel_ex_scan_kind_list_.Empty(leaf_voxel_ex_scan_kind_list_.Max());
				}

				// 追加.
				const auto num_scan = leaf_voxel_particle_id_ex_scan_list_.Num();
				for (int i = 0; i < num_scan; ++i)
				{
					const auto scan_index = leaf_voxel_particle_id_ex_scan_list_[i];


					// CompactリストにはVoxel位置ビット部分のみをマスクした値を追加.
					leaf_voxel_ex_scan_kind_list_.Push(leaf_voxel_particle_id_list_[scan_index] & encoded_voxel_id_mask);


					// 登録済みビットが0の場合はソートによって同一Voxel位置の最初の要素になっているので,最初の要素の登録済みビットを調べてスキップするかどうかを判断できる.
					if (0 != (leaf_voxel_particle_id_list_[scan_index] & encoded_registered_bit_mask))
						continue;


					const auto vi = math::DecodeU15U15U15U19ToInt4(leaf_voxel_particle_id_list_[scan_index]);
					// 範囲チェック済みのVoxelIndexなのでチェック無し版で登録.
					AddNodeByVoxelIndexWithoutRangeCheck(FIntVector(vi.X, vi.Y, vi.Z));
				}
			}
			// Leafより上層の不要ノードの破棄
			if (!is_full_build)
			{
				// ノードの追加が終わった後にLeafより上層ノードについて子を持たないものをボトムアップで破棄. (ノード追加前に破棄してもノード追加で途中のノードが再生成される場合は無駄なので).
				// NOTE. 効率化のためにLeafノード削除した際にその親ノードに対して子ノード削除マークをつけておくのが良いかもしれない.

				// Leafの上の階層からRootの子階層へボトムアップ処理.
				for (uint32_t li = 1; li < leaf_level_idx_; ++li)
				{
					const auto l = leaf_level_idx_ - li;

					// レベル別で管理されているノードのプールを直接巡回する.
					const auto num_node_max = pool_.NumLevelNodeMax(l);
					for (auto i = 0u; i < num_node_max; ++i)
					{
						if (auto p_node = pool_.GetLevelNodeDirect(l, i))
						{
							if (!SparseVoxelTreeNodeHeaderHelper::AnyChildBit(p_node, level_node_info_[l].node_child_count))
							{
								// 子ノードが存在しないので破棄.
								const auto node_handle = SparseVoxelTreeNodeHandle::Encode(0, p_node->level, i);
								// ノードを削除.
								RemoveNode(node_handle);
							}
						}
					}
				}
			}

			if(k_debug_log)
			{
				// 計算時間
				size_t progress_time_ms;
				progress_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - progress_time_start).count();
				if (is_full_build)
				{
					UE_LOG(LogTemp, Display, TEXT("SGT Build Direct (full): %d [micro sec]"), progress_time_ms);
				}
				else
				{
					UE_LOG(LogTemp, Display, TEXT("SGT Build Direct (incremental): %d [micro sec]"), progress_time_ms);
				}
			}
		}

		// Brickクリア. 160MBのBrickデータのクリアで15ms程度かかっていて現状ボトルネック状態.
		void SparseVoxelTreeMpmSystem::ClearBrickData()
		{
			auto	progress_time_start = std::chrono::system_clock::now();

			const auto block_size = mass_brick_pool_.GetBlockSize();
			const auto block_size_in_byte = block_size * mass_brick_pool_.GetComponentByteSize();
			const auto num_h = mass_brick_pool_.NumHandle();

			// メモリアクセス速度ネック.
			for (auto i = 0u; i < num_h; ++i)
			{
				if (auto* data = mass_brick_pool_.GetByIndex(i))
					memset(data, 0, block_size_in_byte);
			}

			if(k_debug_log)
			{
				// 計算時間
				size_t progress_time_ms;
				progress_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - progress_time_start).count();
				UE_LOG(LogTemp, Display, TEXT("SGT ClearMemory: %d [micro sec]"), progress_time_ms);
			}
		}

		// ラスタライズとGrid更新.
		void SparseVoxelTreeMpmSystem::RasterizeAndUpdateGrid(
			float delta_sec,
			TArray<FVector>& position_list, TArray<FVector>& velocity_list, TArray<Mtx3x3>& affine_momentum_list, TArray<Mtx3x3>& deform_grad_list
		)
		{
			const float elastic_lambda = elastic_lambda_;
			const float elastic_mu = elastic_mu_;
			const float initial_density = initial_density_;
			const float initial_particle_volume = 1.0f / initial_density;
			const float voxel_unit_size = level_node_info_[leaf_level_idx_].cell_world_size;
			
			auto	progress_time_start = std::chrono::system_clock::now();

			// 近傍3x3x3セルに対する二次補間係数. 中心セルが (1,1,1) に対応し, position_rate_from_centerは所属セルの中心を原点とした割合位置[-0.5, 0.5].
			// position_rate_from_center = Vec3([0.0, 1.0])
			// for(gx,gy,zz in [0,2])
			//	cell_weight = w[gx].X * w[gy].Y * w[gz].Z
			// グリッドに対するQuadatic補間を採用するとMPMでのテンソル計算が単なるスケーリングに変形できるなどのメリットが有る.
			// https://www.seas.upenn.edu/~cffjiang/research/mpmcourse/mpmcourse.pdf
			auto FuncQuadraticInterpolationWeights = [](const FVector& position_rate_from_center, FVector* w_3)
			{
				w_3[0] = FVector(0.5f) * FMath::Square(FVector(0.5f) - position_rate_from_center);
				w_3[1] = FVector(0.75f) - FMath::Square(position_rate_from_center);
				w_3[2] = FVector(0.5f) * FMath::Square(FVector(0.5f) + position_rate_from_center);
			};

			// ExclusiveScanを利用してラスタライズをするバージョン.
			const auto num_particle = position_list.Num();
			const auto num_voxel_particle_list = leaf_voxel_particle_id_list_.Num();
			const auto num_leaf_voxel_ex_scan_kind = leaf_voxel_ex_scan_kind_list_.Num();
			const auto num_leaf_node_max = pool_.NumLevelNodeMax(leaf_level_idx_);

			// LeafNode毎のラスタライズ処理. LeafNode独立.
			const auto func_rasterize = [&](int i)
			{
				if (auto p_node = pool_.GetLevelNodeDirect(leaf_level_idx_, i))
				{
					// brickはエプロン部を考慮した (size+2)^3で確保されている.
					auto p_brick = mass_brick_pool_.Get(p_node->brick_handle);

					// パーティクルID部を0とした問い合わせ用キーを作成
					const auto voxel_id_code = math::EncodeInt4ToU15U15U15U19(FIntVector4(p_node->base_voxel_ipos.X, p_node->base_voxel_ipos.Y, p_node->base_voxel_ipos.Z, 0));

					// 二分探索でExScaneリストインデックス取得. このVoxelは必ず存在するはず(存在していなかったらロジックミス).
					const auto find_pos = math::BinarySearch(leaf_voxel_ex_scan_kind_list_, 0, num_leaf_voxel_ex_scan_kind, voxel_id_code);
					check(0 <= find_pos && find_pos < num_leaf_voxel_ex_scan_kind);

					// LeafNodeに影響するパーティクルの開始終了インデックス.
					const auto start_index = leaf_voxel_particle_id_ex_scan_list_[find_pos];
					const auto sentinel_index = (find_pos + 1 < num_leaf_voxel_ex_scan_kind) ? leaf_voxel_particle_id_ex_scan_list_[find_pos + 1] : num_voxel_particle_list;

					// このLeafNodeに影響を与える可能性のあるパーティクルを巡回
					for (int pi = start_index; pi < sentinel_index; ++pi)
					{
						const auto voxel_particle_code = leaf_voxel_particle_id_list_[pi];

						// パーティクルID部部の最上位に埋め込まれている登録済みビットを除いたマスクでパーティクルIDを取得.
						const auto particle_id = (voxel_particle_code & encoded_particle_id_mask_without_registerdbit);
						check(particle_id < num_particle);

						const auto sim_pos = position_list[particle_id] - GetSystemAabbMin();
						const auto vi = GetVoxelIndex3(position_list[particle_id]);
						const auto pos_in_cell = (sim_pos * level_node_info_[leaf_level_idx_].cell_world_size_inv) - FVector(vi);

						// パーティクル中心のリーフノード内セル位置.
						const auto vi_in_leaf = vi - p_node->base_voxel_ipos;

						const auto min_pos_in_brick = vi_in_leaf;
						const auto max_pos_in_brick = min_pos_in_brick + FIntVector(3);

						// Apron部には書き込まないように範囲計算.
						const auto min_vi_in_leaf_with_apron = MaxIntVector(min_pos_in_brick, FIntVector(1));
						const auto max_vi_in_leaf_with_apron = MinIntVector(max_pos_in_brick, FIntVector(brick_reso_include_apron_ - 1));

						// クランプ後の値をもとに3x3x3範囲で実際に処理する範囲を計算
						const auto offset_3x3x3 = min_vi_in_leaf_with_apron - min_pos_in_brick;
						const auto range_3x3x3 = max_vi_in_leaf_with_apron - min_pos_in_brick;


						// 分配重み計算.
						FVector cellw[3];
						FuncQuadraticInterpolationWeights(pos_in_cell - 0.5f, cellw);

						// ラスタライズする値. 速度はグリッド単位空間に変換. 質量は1固定.
						const auto particle_vel = velocity_list[particle_id] / voxel_unit_size;
						const auto particle_mass = 1.0f;
						const auto particle_affine_momentum = affine_momentum_list[particle_id];
						const auto particle_deform_grad = deform_grad_list[particle_id];


						// 体積変化
						const auto deform_grad_jacobian = DeterminantMatrix3x3(particle_deform_grad);
						//check(0.0f < deform_grad_jacobian);

						// 変形勾配はVoxel距離正規化されているためvoxel_unit_sizeを乗じてワールド空間へ戻す必要がありそう.
						const auto particle_volume = initial_particle_volume * deform_grad_jacobian;

						// useful matrices for Neo-Hookean model
						const auto deform_grad_t = TransposeMatrix3x3(particle_deform_grad);
						const auto deform_grad_t_inv = InverseMatrix3x3(deform_grad_t);
						const auto deform_grad_sub_t_inv = SubtractMatrix3x3(particle_deform_grad, deform_grad_t_inv);

						// MPM course equation 48
						auto P_term_0 = MulMatrix3x3(deform_grad_sub_t_inv, elastic_mu);
						auto P_term_1 = MulMatrix3x3(deform_grad_t_inv, elastic_lambda * FMath::Loge(deform_grad_jacobian));
						auto P = AddMatrix3x3(P_term_0, P_term_1);

						// cauchy_stress = (1 / det(F)) * P * F_T
						// equation 38, MPM course
						const auto stress = MulMatrix3x3(MulMatrix3x3(P, deform_grad_t), (1.0f / deform_grad_jacobian));

						// (M_p)^-1 = 4, see APIC paper and MPM course page 42
						// this term is used in MLS-MPM paper eq. 16. with quadratic weights, Mp = (1/4) * (delta_x)^2.
						// in this simulation, delta_x = 1, because i scale the rendering of the domain rather than the domain itself.
						// we multiply by dt as part of the process of fusing the momentum and force update for MLS-MPM
						auto eq_16_term_0 = MulMatrix3x3(stress, -particle_volume * 4.0f * delta_sec);

						const auto dist_to_cell_base = FVector((vi - FIntVector(1))) + FVector(0.5) - (sim_pos * level_node_info_[leaf_level_idx_].cell_world_size_inv);

						// 3x3x3範囲内で有効な部分をラスタライズ.
						for (auto bz = offset_3x3x3.Z; bz < range_3x3x3.Z; ++bz)
						{
							for (auto by = offset_3x3x3.Y; by < range_3x3x3.Y; ++by)
							{
								const auto bi_yz = ((by + min_pos_in_brick.Y) * brick_reso_include_apron_)
									+ ((bz + min_pos_in_brick.Z) * brick_reso_include_apron_ * brick_reso_include_apron_);
								const float w_yz = cellw[by].Y * cellw[bz].Z;
								for (auto bx = offset_3x3x3.X; bx < range_3x3x3.X; ++bx)
								{
									const auto bi = (bx + min_pos_in_brick.X) + bi_yz;

									const auto raster_weight = cellw[bx].X * w_yz;

									const auto dist_to_cell = (dist_to_cell_base + FVector(bx, by, bz));
									
									const auto affine_momentum_vel = MulMatrix3x3(particle_affine_momentum, dist_to_cell);

									const auto deform_momentum = MulMatrix3x3(eq_16_term_0, dist_to_cell) / delta_sec;

									const auto raster_momentum = ((particle_vel + affine_momentum_vel) * particle_mass + deform_momentum);

									p_brick[bi] += FVector4(raster_momentum.X, raster_momentum.Y, raster_momentum.Z, particle_mass) * raster_weight;

								}
							}
						}
					}
				}
			};

			if (0 < num_leaf_voxel_ex_scan_kind)
			{
#if 1
				// 並列実行. Ryzen7 3700X で 4倍程度高速.
				ParallelFor(
					num_leaf_node_max,
					func_rasterize
				);
#else
				// シングルスレッド版
				for (auto i = 0u; i < num_leaf_node_max; ++i)
					func_rasterize(i);
#endif
			}

			// GridCell更新.
			{
				// セルの運動量を質量で除算して速度に変換する.
				const auto func_update_grid = [&](int i)
				{
					if (auto p_node = pool_.GetLevelNodeDirect(leaf_level_idx_, i))
					{
						// brickはエプロン部を考慮した (size+2)^3で確保されている.
						auto p_brick = mass_brick_pool_.Get(p_node->brick_handle);


						// 処理はApron部を含まない.
						const auto brick_start_without_apron = 1u;
						const auto brick_reso_without_apron = brick_reso_include_apron_ - 1u;


						for (auto bz = brick_start_without_apron; bz < brick_reso_without_apron; ++bz)
						{
							for (auto by = brick_start_without_apron; by < brick_reso_without_apron; ++by)
							{
								const auto bi_yz = ((by)*brick_reso_include_apron_)
									+ ((bz)*brick_reso_include_apron_ * brick_reso_include_apron_);

								for (auto bx = brick_start_without_apron; bx < brick_reso_without_apron; ++bx)
								{
									const auto bi = (bx)+bi_yz;
									if (0.0f < p_brick[bi].W)
									{
										const auto inv_w = 1.0f / p_brick[bi].W;
										// 運動量として積算した値を積算した質量で除することで速度を求める.
										p_brick[bi].X *= inv_w;
										p_brick[bi].Y *= inv_w;
										p_brick[bi].Z *= inv_w;

#if 1
										// グリッド空間で重力
										p_brick[bi].Z += -9.8f * delta_sec;


										// デバッグ用のボックス配置して境界セルで速度0
										{
											const float debug_area_x_min = -7000.0f;
											const float debug_area_x_max = 5000.0f;
											const float debug_area_y_min = -7000.0f;
											const float debug_area_y_max = 5000.0f;
											const float debug_area_z_min = 0.0f;
											const float debug_area_z_max = 10000.0f;

											const auto cell_size = level_node_info_[leaf_level_idx_].cell_world_size;
											const auto vpos = p_node->base_voxel_ipos + FIntVector(bx, by, bz);
											const auto world_vpos = FVector(vpos) * cell_size + GetSystemAabbMin();


											if (debug_area_x_min >= world_vpos.X || debug_area_x_max <= world_vpos.X)
												p_brick[bi].X = 0.0f;

											if (debug_area_y_min >= world_vpos.Y || debug_area_y_max <= world_vpos.Y)
												p_brick[bi].Y = 0.0f;

											if (debug_area_z_min >= world_vpos.Z || debug_area_z_max <= world_vpos.Z)
												p_brick[bi].Z = 0.0f;
										}
#endif
									}
								}
							}
						}

					}
				};
#if 1
				// 並列実行. Ryzen7 3700X で 4倍程度高速.
				ParallelFor(
					num_leaf_node_max,
					func_update_grid
				);
#else
				// シングルスレッド版
				for (auto i = 0u; i < num_leaf_node_max; ++i)
					func_update_grid(i);
#endif
			}

			// Apron同期.
			if (true)
			{
				{
					const auto leaf_voxel_width = level_node_info_[leaf_level_idx_].node_reso;
					const auto leaf_voxel_last_idx = leaf_voxel_width - 1;
					const auto leaf_voxel_width_with_apron = brick_reso_include_apron_;
					const auto leaf_voxel_last_idx_with_apron = leaf_voxel_width_with_apron - 1;

					// 単方向13近傍方向.
					// 各LeafNodeは並列にこの方向の近傍Nodeを探索して重複エッジのApron部を相互処理する.
					// 反対方向は別のNodeが処理をするので全Nodeの処理が終われば全方向(26方向)のApron処理が完了している.
					const FIntVector oneway_neighbor_dir[13] =
					{
						FIntVector(1,0,0), FIntVector(0,1,0), FIntVector(0,0,1),
						FIntVector(1,1,0), FIntVector(1,-1,0),
						FIntVector(0,1,1), FIntVector(0,-1,1),
						FIntVector(1,0,1), FIntVector(1,0,-1),
						FIntVector(1,1,1), FIntVector(1,-1,1), FIntVector(1,1,-1), FIntVector(1,-1,-1),
					};
					const FIntVector oneway_neighbor_step[13] =
					{
						FIntVector(0,1,1), FIntVector(1,0,1), FIntVector(1,1,0),
						FIntVector(0,0,1), FIntVector(0,0,1),
						FIntVector(1,0,0), FIntVector(1,0,0),
						FIntVector(0,1,0), FIntVector(0,1,0),
						FIntVector(0,0,0), FIntVector(0,0,0), FIntVector(0,0,0), FIntVector(0,0,0),
					};
					const FIntVector oneway_neighbor_begin[13] =
					{
						FIntVector(1,0,0), FIntVector(0,1,0), FIntVector(0,0,1),
						FIntVector(1,1,0), FIntVector(1,0,0),
						FIntVector(0,1,1), FIntVector(0,0,1),
						FIntVector(1,0,1), FIntVector(1,0,0),
						FIntVector(1,1,1), FIntVector(1,0,1),FIntVector(1,1,0), FIntVector(1,0,0),
					};

					constexpr auto num_oneway_neighbor_dir = static_cast<unsigned int>(std::size(oneway_neighbor_dir));

					const auto brick_edgeoffset = brick_reso_include_apron_;
					const auto brick_faceoffset = brick_reso_include_apron_ * brick_reso_include_apron_;

					const auto func_sync_brick_apron = [&](int i)
					{
						if (auto p_node = pool_.GetLevelNodeDirect(leaf_level_idx_, i))
						{
							auto p_brick = mass_brick_pool_.Get(p_node->brick_handle);

							// 近傍探索
							for (auto ni = 0u; ni < num_oneway_neighbor_dir; ++ni)
							{
								// 隣接LeafのVoxel位置
								const auto neighbor_base_vi = p_node->base_voxel_ipos + (oneway_neighbor_dir[ni] * leaf_voxel_width);

								const auto neighbor_node_handle = FindNodeByVoxelIndex(neighbor_base_vi);

								// 見つからないまたはリーフではない場合はスキップ
								if (SparseVoxelTreeNodeHandle::k_invalid == neighbor_node_handle
									|| leaf_level_idx_ != SparseVoxelTreeNodeHandle::GetLevel(neighbor_node_handle))
									continue;

								auto p_n_node = pool_.GetNode(neighbor_node_handle);
								auto p_n_brick = mass_brick_pool_.Get(p_n_node->brick_handle);


								if (9 <= ni)
								{
									// 角
									const auto a_src_pos = (oneway_neighbor_begin[ni]) * leaf_voxel_last_idx + FIntVector(1);
									const auto a_dst_pos = (FIntVector(1) - oneway_neighbor_begin[ni]) * leaf_voxel_last_idx_with_apron;

									const auto b_dst_pos = (oneway_neighbor_begin[ni]) * leaf_voxel_last_idx_with_apron;
									const auto b_src_pos = (FIntVector(1) - oneway_neighbor_begin[ni]) * leaf_voxel_last_idx + FIntVector(1);

									const auto a_src_i = a_src_pos.X + a_src_pos.Y * brick_edgeoffset + a_src_pos.Z * brick_faceoffset;
									const auto a_dst_i = a_dst_pos.X + a_dst_pos.Y * brick_edgeoffset + a_dst_pos.Z * brick_faceoffset;
									const auto b_dst_i = b_dst_pos.X + b_dst_pos.Y * brick_edgeoffset + b_dst_pos.Z * brick_faceoffset;
									const auto b_src_i = b_src_pos.X + b_src_pos.Y * brick_edgeoffset + b_src_pos.Z * brick_faceoffset;
									{
										// 相手->自身
										p_n_brick[a_dst_i] = p_brick[a_src_i];
										// 自身->相手
										p_brick[b_dst_i] = p_n_brick[b_src_i];
									}
								}
								else if (3 <= ni)
								{
									// 辺
									const auto a_src_pos_base = (oneway_neighbor_begin[ni]) * leaf_voxel_last_idx + FIntVector(1);
									const auto a_dst_pos_base = (FIntVector(1) - oneway_neighbor_begin[ni] - oneway_neighbor_step[ni]) * leaf_voxel_last_idx_with_apron + oneway_neighbor_step[ni];

									const auto b_dst_pos_base = (oneway_neighbor_begin[ni]) * leaf_voxel_last_idx_with_apron + oneway_neighbor_step[ni];
									const auto b_src_pos_base = (FIntVector(1) - oneway_neighbor_begin[ni] - oneway_neighbor_step[ni]) * leaf_voxel_last_idx + FIntVector(1);

									for (auto edgei = 0u; edgei <= leaf_voxel_last_idx; ++edgei)
									{
										const auto step_v = (oneway_neighbor_step[ni] * edgei);
										const auto a_src_pos = a_src_pos_base + step_v;
										const auto a_dst_pos = a_dst_pos_base + step_v;
										const auto b_dst_pos = b_dst_pos_base + step_v;
										const auto b_src_pos = b_src_pos_base + step_v;
										{
											// 相手->自身
											p_n_brick[a_dst_pos.X + a_dst_pos.Y * brick_edgeoffset + a_dst_pos.Z * brick_faceoffset]
												= p_brick[a_src_pos.X + a_src_pos.Y * brick_edgeoffset + a_src_pos.Z * brick_faceoffset];

											// 自身->相手
											p_brick[b_dst_pos.X + b_dst_pos.Y * brick_edgeoffset + b_dst_pos.Z * brick_faceoffset]
												= p_n_brick[b_src_pos.X + b_src_pos.Y * brick_edgeoffset + b_src_pos.Z * brick_faceoffset];
										}
									}
								}
								else
								{
									// 面
									const auto a_src_pos_base = (oneway_neighbor_begin[ni]) * leaf_voxel_last_idx + FIntVector(1);
									const auto a_dst_pos_base = (FIntVector(1) - oneway_neighbor_begin[ni] - oneway_neighbor_step[ni]) * leaf_voxel_last_idx_with_apron + oneway_neighbor_step[ni];

									const auto b_dst_pos_base = (oneway_neighbor_begin[ni]) * leaf_voxel_last_idx_with_apron + oneway_neighbor_step[ni];
									const auto b_src_pos_base = (FIntVector(1) - oneway_neighbor_begin[ni] - oneway_neighbor_step[ni]) * leaf_voxel_last_idx + FIntVector(1);

									FIntVector step_0 = FIntVector(1, 0, 0);
									FIntVector step_1 = FIntVector(0, 1, 0);
									if (0 == oneway_neighbor_step[ni].X)
									{
										step_0 = FIntVector(0, 1, 0);
										step_1 = FIntVector(0, 0, 1);
									}
									else if (0 == oneway_neighbor_step[ni].Y)
									{
										step_0 = FIntVector(1, 0, 0);
										step_1 = FIntVector(0, 0, 1);
									}

									for (auto edgej = 0u; edgej <= leaf_voxel_last_idx; ++edgej)
									{
										const auto step_v1 = (step_1 * edgej);
										for (auto edgei = 0u; edgei <= leaf_voxel_last_idx; ++edgei)
										{
											const auto step_v = (step_0 * edgei + step_v1);
											const auto a_src_pos = a_src_pos_base + step_v;
											const auto a_dst_pos = a_dst_pos_base + step_v;
											const auto b_dst_pos = b_dst_pos_base + step_v;
											const auto b_src_pos = b_src_pos_base + step_v;
											{
												// 相手->自身
												p_n_brick[a_dst_pos.X + a_dst_pos.Y * brick_edgeoffset + a_dst_pos.Z * brick_faceoffset]
													= p_brick[a_src_pos.X + a_src_pos.Y * brick_edgeoffset + a_src_pos.Z * brick_faceoffset];
												// 自身->相手
												p_brick[b_dst_pos.X + b_dst_pos.Y * brick_edgeoffset + b_dst_pos.Z * brick_faceoffset]
													= p_n_brick[b_src_pos.X + b_src_pos.Y * brick_edgeoffset + b_src_pos.Z * brick_faceoffset];
											}
										}
									}
								}
							}
						}
					};
#if 1
					// 並列実行. Ryzen7 3700X で 4倍程度高速.
					ParallelFor(
						num_leaf_node_max,
						func_sync_brick_apron
					);
#else
					// シングルスレッド版
					for (auto i = 0u; i < num_leaf_node_max; ++i)
					{
						func_sync_brick_apron(i);
					}
#endif
				}
			}


			// Grid 2 Particle.
			{
				const auto func_grid_2_particle = [&](int i)
				{
					const auto vi = GetVoxelIndex3(position_list[i]);
					const auto node_handle = FindNodeByVoxelIndex(vi);
					if (SparseVoxelTreeNodeHandle::k_invalid == node_handle)
						return;

					if (const auto* p_node = pool_.GetNode(node_handle))
					{
						if (leaf_level_idx_ == p_node->level)
						{
							const auto* p_brick = mass_brick_pool_.Get(p_node->brick_handle);
							check(nullptr != p_brick);


							const auto sim_pos = position_list[i] - GetSystemAabbMin();
							const auto pos_in_cell = (sim_pos * level_node_info_[leaf_level_idx_].cell_world_size_inv) - FVector(vi);
							// 分配重み計算.
							FVector cellw[3];
							FuncQuadraticInterpolationWeights(pos_in_cell - 0.5f, cellw);

							const auto vi_in_leaf = vi - p_node->base_voxel_ipos;
							const auto dist_to_cell_base = FVector(vi - FIntVector(1)) + FVector(0.5) - (sim_pos * level_node_info_[leaf_level_idx_].cell_world_size_inv);

							float particle_density = 0.0f;

							// constructing affine per-particle momentum matrix from APIC / MLS-MPM.
							// see APIC paper (https://web.archive.org/web/20190427165435/https://www.math.ucla.edu/~jteran/papers/JSSTS15.pdf), page 6
							// below equation 11 for clarification. this is calculating C = B * (D^-1) for APIC equation 8,
							// where B is calculated in the inner loop at (D^-1) = 4 is a constant when using quadratic interpolation functions
							Mtx3x3 momentum_matrix = Mtx3x3::Zero();

							if(k_debug_log)
							{
								// デバッグ
								//	UE_LOG(LogTemp, Display, TEXT("\nBrick Base[%d] -> %f, %f, %f"),i, position_list[i].X, position_list[i].Y, position_list[i].Z);
							}

							FVector gather_vel = FVector::ZeroVector;
							for (auto bz = 0; bz < 3; ++bz)
							{
								for (auto by = 0; by < 3; ++by)
								{
									const auto bi_0yz = (vi_in_leaf.X) + ((by + vi_in_leaf.Y) * brick_reso_include_apron_)
										+ ((bz + vi_in_leaf.Z) * brick_reso_include_apron_ * brick_reso_include_apron_);

									const float w_yz = cellw[by].Y * cellw[bz].Z;

									const auto bi_1yz = bi_0yz + 1;
									const auto bi_2yz = bi_0yz + 2;

									{
										particle_density += p_brick[bi_0yz].W * cellw[0].X * w_yz;
										particle_density += p_brick[bi_1yz].W * cellw[1].X * w_yz;
										particle_density += p_brick[bi_2yz].W * cellw[2].X * w_yz;
									}

									const auto gather_vel0 = FVector(p_brick[bi_0yz].X, p_brick[bi_0yz].Y, p_brick[bi_0yz].Z) * cellw[0].X * w_yz;
									const auto gather_vel1 = FVector(p_brick[bi_1yz].X, p_brick[bi_1yz].Y, p_brick[bi_1yz].Z) * cellw[1].X * w_yz;
									const auto gather_vel2 = FVector(p_brick[bi_2yz].X, p_brick[bi_2yz].Y, p_brick[bi_2yz].Z) * cellw[2].X * w_yz;

									{
										const auto dist_to_cell0 = (dist_to_cell_base + FVector(0, by, bz));
										const auto dist_to_cell1 = (dist_to_cell_base + FVector(1, by, bz));
										const auto dist_to_cell2 = (dist_to_cell_base + FVector(2, by, bz));

										// APIC paper equation 10, constructing inner term for B
										const auto c0 = ((gather_vel0 * dist_to_cell0.X) + (gather_vel1 * dist_to_cell1.X) + (gather_vel2 * dist_to_cell2.X));
										const auto c1 = ((gather_vel0 * dist_to_cell0.Y) + (gather_vel1 * dist_to_cell1.Y) + (gather_vel2 * dist_to_cell2.Y));
										const auto c2 = ((gather_vel0 * dist_to_cell0.Z) + (gather_vel1 * dist_to_cell1.Z) + (gather_vel2 * dist_to_cell2.Z));
										momentum_matrix = AddMatrix3x3(momentum_matrix, Mtx3x3(c0, c1, c2));
									}

									gather_vel += gather_vel0 + gather_vel1 + gather_vel2;


									if(k_debug_log)
									{
										// デバッグ
										//	UE_LOG(LogTemp, Display, TEXT("Brick: %f, %f, %f"), p_brick[bi_0yz].W, p_brick[bi_1yz].W, p_brick[bi_2yz].W);
									}
								}
							}
							
							// 速度.
							velocity_list[i] = gather_vel * voxel_unit_size;// Gridの速度はGrid単位長さとなっているため変換.

							// 位置
							position_list[i] += velocity_list[i] * delta_sec;

							//p.affine_momentum_ = B * 4.0f;
							affine_momentum_list[i] = MulMatrix3x3(momentum_matrix, 4.0f);

							// deformation gradient update - MPM course, equation 181
							// Fp' = (I + dt * p.C) * Fp
							const auto Fp_new = AddMatrix3x3(Mtx3x3::Identity(), MulMatrix3x3(affine_momentum_list[i], delta_sec));
							deform_grad_list[i] = MulMatrix3x3(Fp_new, deform_grad_list[i]);


							// 念の為シミュレーション空間に収まるように補正.
							{
								const auto sim_area_range = GetLevelInfo(0).cell_world_size * static_cast<float>(GetLevelInfo(0).node_reso);
								const auto sim_area_min = GetSystemAabbMin();
								position_list[i].X = FMath::Clamp(position_list[i].X, sim_area_min.X, sim_area_min.X + sim_area_range);
								position_list[i].Y = FMath::Clamp(position_list[i].Y, sim_area_min.Y, sim_area_min.Y + sim_area_range);
								position_list[i].Z = FMath::Clamp(position_list[i].Z, sim_area_min.Z, sim_area_min.Z + sim_area_range);
							}
						}
					}
				};

#if 1
				// 並列実行. Ryzen7 3700X で 4倍程度高速.
				ParallelFor(
					num_particle,
					func_grid_2_particle
				);
#else
				// シングルスレッド版
				for (auto i = 0; i < num_particle; ++i)
				{
					func_grid_2_particle(i);
				}
#endif
			}

			if(k_debug_log)
			{
				// 計算時間
				size_t progress_time_ms;
				progress_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - progress_time_start).count();
				UE_LOG(LogTemp, Display, TEXT("SGT Rasterize: %d [micro sec]"), progress_time_ms);
			}
		}

		// ------------------------------------------------------------------------------------------------------------------------
		// ------------------------------------------------------------------------------------------------------------------------

		/*
			実装メモ
						粒子一つに付き半径1セル分を考慮したAABBコーナーのリーフノードを確保しているが、
						各リーフノードのBrickを両端1セル分のエプロンを付加する場合、AABBコーナーは考慮せずに所属Brickにのみ書き込みをして
						後処理でエプロン部の同期をすれば不要なメモリアロケーション処理なども省略できる可能性がある


						リーフノードの隣接リーフを何度も探索する場合は事前にリーフノードに格納しておいたほうが良さそう

		*/


		//--------------------------------------------------------------------------------------------------------------------------------------
	
	
	
	
	
	
	
	
	

		//--------------------------------------------------------------------------------------------------------------------------------------
		// MPM参考実装の検証.
		// https://nialltl.neocities.org/articles/mpm_guide.html
		//--------------------------------------------------------------------------------------------------------------------------------------
		static FMatrix GetZeroMatrix()
		{
			FMatrix tmp;
			const auto n0 = std::size(tmp.M);
			const auto n1 = std::size(tmp.M[0]);
			auto p = &tmp.M[0][0];
			for (int i = 0; i < n0 * n1; ++i)
			{
				p[i] = 0.0f;
			}
			return tmp;
		};
		static FMatrix2x2 GetZeroMatrix2x2()
		{
			FMatrix2x2 tmp{ 0.0f, 0.0f , 0.0f , 0.0f };
			return tmp;
		};
		static FMatrix2x2 TransposeMatrix2x2(const FMatrix2x2& m)
		{
			float a, b, c, d;
			m.GetMatrix(a, b, c, d);
			FMatrix2x2 tmp{ a, c, b, d };
			return tmp;
		};
		static FMatrix2x2 SubtractMatrix2x2(const FMatrix2x2& m0, const FMatrix2x2& m1)
		{
			float a0, b0, c0, d0;
			m0.GetMatrix(a0, b0, c0, d0);
			float a1, b1, c1, d1;
			m1.GetMatrix(a1, b1, c1, d1);
			FMatrix2x2 tmp{ a0 - a1, b0 - b1, c0 - c1, d0 - d1 };
			return tmp;
		};
		static FMatrix2x2 AddMatrix2x2(const FMatrix2x2& m0, const FMatrix2x2& m1)
		{
			float a0, b0, c0, d0;
			m0.GetMatrix(a0, b0, c0, d0);
			float a1, b1, c1, d1;
			m1.GetMatrix(a1, b1, c1, d1);
			FMatrix2x2 tmp{ a0 + a1, b0 + b1, c0 + c1, d0 + d1 };
			return tmp;
		};
		static FMatrix2x2 MulMatrix2x2(const FMatrix2x2& m0, float v)
		{
			float a0, b0, c0, d0;
			m0.GetMatrix(a0, b0, c0, d0);
			FMatrix2x2 tmp{ a0 * v, b0 * v, c0 * v, d0 * v };
			return tmp;
		};
		static FVector2D MulMatrix2x2(const FMatrix2x2& m0, const FVector2D& v)
		{
			float a0, b0, c0, d0;
			m0.GetMatrix(a0, b0, c0, d0);

			return FVector2D(a0 * v.X + b0 * v.Y, c0 * v.X + d0 * v.Y);
		};
		static FMatrix2x2 MulMatrix2x2(const FMatrix2x2& m0, const FMatrix2x2& m1)
		{
			float a0, b0, c0, d0;
			m0.GetMatrix(a0, b0, c0, d0);
			float a1, b1, c1, d1;
			m1.GetMatrix(a1, b1, c1, d1);

			FMatrix2x2 tmp{ a0 * a1 + b0 * c1, a0 * b1 + b0 * d1, c0 * a1 + d0 * c1, c0 * b1 + d0 * d1 };
			return tmp;
		};
		static float TraceMatrix2x2(const FMatrix2x2& m0)
		{
			float a0, b0, c0, d0;
			m0.GetMatrix(a0, b0, c0, d0);
			return a0 + d0;
		};


		bool MlsMpm2d_Base::Initialize(int grid_reso_x, int grid_reso_y)
		{
			static const int grid_rezo_z = 1;
			// grid reso should be larger then 3.
			grid_reso_ = FIntVector(std::max(grid_reso_x, 3), std::max(grid_reso_y, 3), grid_rezo_z);

			int num_cell = (grid_reso_.X) * (grid_reso_.Y) * (grid_reso_.Z);
			grid_param_.SetNumUninitialized(num_cell);

			return true;
		}
		void MlsMpm2d_Base::AddParticle(const FVector2D& pos, const FVector2D& vel)
		{
			static const auto ZeroMatrix = GetZeroMatrix2x2();


			Particle p;
			p.mass_ = 1.0f;
			p.pos_ = pos;
			p.vel_ = vel;
			p.affine_momentum_ = ZeroMatrix;

			particle_.Push(p);
		}
		void MlsMpm2d_Base::AdvanceSimulation(float delta_sec)
		{
			auto FuncQuadraticInterpolationWeights = [](const FVector2D& cell_diff, FVector2D* w_3)
			{
				w_3[0] = FVector2D(0.5f) * FMath::Square(FVector2D(0.5f) - cell_diff);
				w_3[1] = FVector2D(0.75f) - FMath::Square(cell_diff);
				w_3[2] = FVector2D(0.5f) * FMath::Square(FVector2D(0.5f) + cell_diff);
			};


			const float dt = delta_sec;
			const auto gravity = FVector2D(0.0f, -9.8f) * 1.0f;

			// Clear Cell
			{
				static const Cell ZeroCell = Cell::Zero();
				for (auto&& e : grid_param_)
					e = ZeroCell;
			}

			// P2G
			{
				FVector2D weights[3];

				for (const auto& p : particle_)
				{
					// quadratic interpolation weights
					FIntVector cell_idx = FIntVector(p.pos_.X, p.pos_.Y, 0.0);
					FVector2D cell_diff = (p.pos_ - FVector2D(cell_idx.X, cell_idx.Y)) - 0.5f;

					FuncQuadraticInterpolationWeights(cell_diff, weights);

					// for all surrounding 9 cells (2D)
					for (int gx = 0; gx < 3; ++gx)
					{
						for (int gy = 0; gy < 3; ++gy)
						{
							float weight = weights[gx].X * weights[gy].Y;

							FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);

							FVector2D cell_dist = (FVector2D(cell_x.X, cell_x.Y) - p.pos_) + 0.5f;

							const auto affine_mat = p.affine_momentum_;
							//FVector2D Q = affine_mat.TransformVector(cell_dist);
							FVector2D Q = MulMatrix2x2(affine_mat, cell_dist);

							// MPM course, equation 172
							float mass_contrib = weight * p.mass_;

							// converting 2D index to 1D
							int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;
							Cell& cell = grid_param_[cell_index];

							// scatter mass to the grid
							cell.mass_ += mass_contrib;

							cell.vel_ += mass_contrib * (p.vel_ + Q);

							// note: currently "cell.v" refers to MOMENTUM, not velocity!
							// this gets corrected in the UpdateGrid step below.
						}
					}
				}
			}

			// Update Cell
			{
				for (int i = 0; i < grid_param_.Num(); ++i)
				{
					auto& cell = grid_param_[i];

					if (0.0f < cell.mass_)
					{
						// convert momentum to velocity, apply gravity
						cell.vel_ /= cell.mass_;
						cell.vel_ += dt * gravity;

						// boundary conditions
						int y = i / grid_reso_.X;
						int x = i - y * grid_reso_.X;
						if (x < 2 || x > grid_reso_.X - 3) { cell.vel_.X = 0; }
						if (y < 2 || y > grid_reso_.Y - 3) { cell.vel_.Y = 0; }

					}
				}
			}

			// G2P
			{
				FVector2D weights[3];
				for (auto& p : particle_)
				{
					// reset velocity
					p.vel_ = FVector2D::ZeroVector;

					// quadratic interpolation weights
					FIntVector cell_idx = FIntVector(p.pos_.X, p.pos_.Y, 0);
					FVector2D cell_diff = (p.pos_ - FVector2D(cell_idx.X, cell_idx.Y)) - 0.5f;

					FuncQuadraticInterpolationWeights(cell_diff, weights);


					// constructing affine per-particle momentum matrix from APIC / MLS-MPM.
					// see APIC paper (https://web.archive.org/web/20190427165435/https://www.math.ucla.edu/~jteran/papers/JSSTS15.pdf), page 6
					// below equation 11 for clarification. this is calculating C = B * (D^-1) for APIC equation 8,
					// where B is calculated in the inner loop at (D^-1) = 4 is a constant when using quadratic interpolation functions
					FMatrix2x2 B = GetZeroMatrix2x2();
					for (int gx = 0; gx < 3; ++gx)
					{
						for (int gy = 0; gy < 3; ++gy)
						{
							float weight = weights[gx].X * weights[gy].Y;

							FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);
							int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;

							FVector2D dist = (FVector2D(cell_x.X, cell_x.Y) - p.pos_) + 0.5f;
							FVector2D weighted_velocity = grid_param_[cell_index].vel_ * weight;

							// APIC paper equation 10, constructing inner term for B
							const auto c0 = weighted_velocity * dist.X;
							const auto c1 = weighted_velocity * dist.Y;

							//B. += term;
							float m00, m01, m10, m11;
							B.GetMatrix(m00, m01, m10, m11);
							m00 += c0.X;
							m01 += c0.Y;
							m10 += c1.X;
							m11 += c1.Y;
							B = FMatrix2x2(m00, m01, m10, m11);


							p.vel_ += weighted_velocity;
						}
					}
					//p.affine_momentum_ = B * 4.0f;
					{
						float m00, m01, m10, m11;
						B.GetMatrix(m00, m01, m10, m11);
						p.affine_momentum_ = FMatrix2x2(m00 * 4.0f, m01 * 4.0f, m10 * 4.0f, m11 * 4.0f);
					}

					// advect particles
					p.pos_ += p.vel_ * dt;

					// safety clamp to ensure particles don't exit simulation domain
					p.pos_.X = FMath::Clamp(p.pos_.X, 1.0f, grid_reso_.X - 2.0f);
					p.pos_.Y = FMath::Clamp(p.pos_.Y, 1.0f, grid_reso_.Y - 2.0f);

				}
			}
		}
		// テスト用のパーティクルセットアップ
		void MlsMpm2d_Base::SetupTestParticle()
		{
			FVector2D center = FVector2D(grid_reso_.X, grid_reso_.Y) * 0.5f;
			{
				const float spacing = 1.0f;
				const int box_x = 16, box_y = 16;
				const float sx = grid_reso_.X / 2.0f, sy = grid_reso_.Y / 2.0f;
				for (float i = sx - box_x / 2; i < sx + box_x / 2; i += spacing) {
					for (float j = sy - box_y / 2; j < sy + box_y / 2; j += spacing) {
						auto pos = FVector2D(i, j);
						auto vel = FVector2D(FMath::FRand() * 2.0f - 1.0f, FMath::FRand() + 2.75f) * 6.0f;
						AddParticle(pos, vel);
					}
				}
			}
		}


		//--------------------------------------------------------------------------------------------------------------------------------------
		bool MlsMpm2d_Elastic::Initialize(int grid_reso_x, int grid_reso_y)
		{
			static const int grid_rezo_z = 1;
			// grid reso should be larger then 3.
			grid_reso_ = FIntVector(std::max(grid_reso_x, 3), std::max(grid_reso_y, 3), grid_rezo_z);

			int num_cell = (grid_reso_.X) * (grid_reso_.Y) * (grid_reso_.Z);
			grid_param_.SetNumUninitialized(num_cell);

			return true;
		}
		void MlsMpm2d_Elastic::AddParticle(const FVector2D& pos, const FVector2D& vel)
		{
			static const auto ZeroMatrix = GetZeroMatrix2x2();


			Particle p;
			p.mass_ = 1.0f;
			p.pos_ = pos;
			p.vel_ = vel;
			p.affine_momentum_ = ZeroMatrix;
			p.initial_volume_ = 0.0f;

			particle_.Push(p);

			particle_deform_grad_.Push(FMatrix2x2());// Identity
		}
		void MlsMpm2d_Elastic::AdvanceSimulation(float delta_sec)
		{
			auto FuncQuadraticInterpolationWeights = [](const FVector2D& cell_diff, FVector2D* w_3)
			{
				w_3[0] = FVector2D(0.5f) * FMath::Square(FVector2D(0.5f) - cell_diff);
				w_3[1] = FVector2D(0.75f) - FMath::Square(cell_diff);
				w_3[2] = FVector2D(0.5f) * FMath::Square(FVector2D(0.5f) + cell_diff);
			};

			const float dt = delta_sec;
			const auto gravity = FVector2D(0.0f, -9.8f) * 1.0f;

			const float elastic_lambda = 10.0f;
			const float elastic_mu = 3.0f;

			if (is_first_frame_)
			{
				// 初回に初期体積計算.
				is_first_frame_ = false;

				// Clear Cell
				{
					static const Cell ZeroCell = Cell::Zero();
					for (auto&& e : grid_param_)
						e = ZeroCell;
				}
				// 質量Scatter
				{
					FVector2D weights[3];

					for (const auto& p : particle_)
					{
						// quadratic interpolation weights
						FIntVector cell_idx = FIntVector(p.pos_.X, p.pos_.Y, 0.0);
						FVector2D cell_diff = (p.pos_ - FVector2D(cell_idx.X, cell_idx.Y)) - 0.5f;

						FuncQuadraticInterpolationWeights(cell_diff, weights);

						// for all surrounding 9 cells (2D)
						for (int gx = 0; gx < 3; ++gx)
						{
							for (int gy = 0; gy < 3; ++gy)
							{
								float weight = weights[gx].X * weights[gy].Y;
								FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);
								FVector2D cell_dist = (FVector2D(cell_x.X, cell_x.Y) - p.pos_) + 0.5f;
								// MPM course, equation 172
								float mass_contrib = weight * p.mass_;

								// converting 2D index to 1D
								int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;
								Cell& cell = grid_param_[cell_index];
								// scatter mass to the grid
								cell.mass_ += mass_contrib;
							}
						}
					}
				}
				// 初期体積を計算
				{
					FVector2D weights[3];
					for (auto& p : particle_)
					{
						// reset velocity
						//p.vel_ = FVector2D::ZeroVector;

						// quadratic interpolation weights
						FIntVector cell_idx = FIntVector(p.pos_.X, p.pos_.Y, 0);
						FVector2D cell_diff = (p.pos_ - FVector2D(cell_idx.X, cell_idx.Y)) - 0.5f;

						FuncQuadraticInterpolationWeights(cell_diff, weights);

						float density = 0.0f;
						for (int gx = 0; gx < 3; ++gx)
						{
							for (int gy = 0; gy < 3; ++gy)
							{
								float weight = weights[gx].X * weights[gy].Y;

								FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);
								int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;

								density += grid_param_[cell_index].mass_ * weight;
							}
						}
						p.initial_volume_ = p.mass_ / density;// 初期体積.
					}
				}
			}


			// Clear Cell
			{
				static const Cell ZeroCell = Cell::Zero();
				for (auto&& e : grid_param_)
					e = ZeroCell;
			}

			// P2G
			{
				FVector2D weights[3];


				for (int pi = 0; pi < particle_.Num(); ++pi)
				{
					const auto& p = particle_[pi];


					FMatrix2x2 stress = GetZeroMatrix2x2();

					FMatrix2x2 F = particle_deform_grad_[pi];

					float J = F.Determinant();

					float volume = p.initial_volume_ * J;


					// useful matrices for Neo-Hookean model
					auto F_T = TransposeMatrix2x2(F);
					auto F_inv_T = F_T.Inverse();
					auto F_minus_F_inv_T = SubtractMatrix2x2(F, F_inv_T);

					// MPM course equation 48
					auto P_term_0 = MulMatrix2x2(F_minus_F_inv_T, elastic_mu);
					auto P_term_1 = MulMatrix2x2(F_inv_T, elastic_lambda * FMath::Loge(J));
					auto P = AddMatrix2x2(P_term_0, P_term_1);

					// cauchy_stress = (1 / det(F)) * P * F_T
					// equation 38, MPM course
					stress = MulMatrix2x2(MulMatrix2x2(P, F_T), (1.0f / J));

					// (M_p)^-1 = 4, see APIC paper and MPM course page 42
					// this term is used in MLS-MPM paper eq. 16. with quadratic weights, Mp = (1/4) * (delta_x)^2.
					// in this simulation, delta_x = 1, because i scale the rendering of the domain rather than the domain itself.
					// we multiply by dt as part of the process of fusing the momentum and force update for MLS-MPM
					auto eq_16_term_0 = MulMatrix2x2(stress, -volume * 4 * dt);


					// quadratic interpolation weights
					FIntVector cell_idx = FIntVector(p.pos_.X, p.pos_.Y, 0.0);
					FVector2D cell_diff = (p.pos_ - FVector2D(cell_idx.X, cell_idx.Y)) - 0.5f;

					FuncQuadraticInterpolationWeights(cell_diff, weights);

					// for all surrounding 9 cells (2D)
					for (int gx = 0; gx < 3; ++gx)
					{
						for (int gy = 0; gy < 3; ++gy)
						{
							float weight = weights[gx].X * weights[gy].Y;

							FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);

							FVector2D cell_dist = (FVector2D(cell_x.X, cell_x.Y) - p.pos_) + 0.5f;

							const auto affine_mat = p.affine_momentum_;
							//FVector2D Q = affine_mat.TransformVector(cell_dist);
							FVector2D Q = MulMatrix2x2(affine_mat, cell_dist);


							// MPM course, equation 172
							float mass_contrib = weight * p.mass_;

							// converting 2D index to 1D
							int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;
							Cell& cell = grid_param_[cell_index];

							// scatter mass to the grid
							cell.mass_ += mass_contrib;

							cell.vel_ += mass_contrib * (p.vel_ + Q);

							// fused force/momentum update from MLS-MPM
							// see MLS-MPM paper, equation listed after eqn. 28
							//FVector2D momentum = MulMatrix2x2(MulMatrix2x2(eq_16_term_0, weight), cell_dist);
							FVector2D momentum = MulMatrix2x2(MulMatrix2x2(eq_16_term_0, weight), cell_dist) / dt;// dt除算追加. 元実装ではデルタタイム1だったため,本来はdtで除する必要があると思われる.
							cell.vel_ += momentum;


							// total update on cell.v is now:
							// weight * (dt * M^-1 * p.volume * p.stress + p.mass * p.C)
							// this is the fused momentum + force from MLS-MPM. however, instead of our stress being derived from the energy density,
							// i use the weak form with cauchy stress. converted:
							// p.volume_0 * (dΨ/dF)(Fp)*(Fp_transposed)
							// is equal to p.volume * σ

							// note: currently "cell.v" refers to MOMENTUM, not velocity!
							// this gets converted in the UpdateGrid step below.
						}
					}
				}
			}

			// Update Cell
			{
				for (int i = 0; i < grid_param_.Num(); ++i)
				{
					auto& cell = grid_param_[i];

					if (0.0f < cell.mass_)
					{
						// convert momentum to velocity, apply gravity
						cell.vel_ /= cell.mass_;
						cell.vel_ += dt * gravity;

						// boundary conditions
						int y = i / grid_reso_.X;
						int x = i - y * grid_reso_.X;
						if (x < 2 || x > grid_reso_.X - 3) { cell.vel_.X = 0; }
						if (y < 2 || y > grid_reso_.Y - 3) { cell.vel_.Y = 0; }

					}
				}
			}

			// G2P
			{
				FVector2D weights[3];
				for (int pi = 0; pi < particle_.Num(); ++pi)
				{
					auto& p = particle_[pi];

					// reset velocity
					p.vel_ = FVector2D::ZeroVector;

					// quadratic interpolation weights
					FIntVector cell_idx = FIntVector(p.pos_.X, p.pos_.Y, 0);
					FVector2D cell_diff = (p.pos_ - FVector2D(cell_idx.X, cell_idx.Y)) - 0.5f;

					FuncQuadraticInterpolationWeights(cell_diff, weights);


					// constructing affine per-particle momentum matrix from APIC / MLS-MPM.
					// see APIC paper (https://web.archive.org/web/20190427165435/https://www.math.ucla.edu/~jteran/papers/JSSTS15.pdf), page 6
					// below equation 11 for clarification. this is calculating C = B * (D^-1) for APIC equation 8,
					// where B is calculated in the inner loop at (D^-1) = 4 is a constant when using quadratic interpolation functions
					FMatrix2x2 B = GetZeroMatrix2x2();
					for (int gx = 0; gx < 3; ++gx)
					{
						for (int gy = 0; gy < 3; ++gy)
						{
							float weight = weights[gx].X * weights[gy].Y;

							FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);
							int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;

							FVector2D dist = (FVector2D(cell_x.X, cell_x.Y) - p.pos_) + 0.5f;
							FVector2D weighted_velocity = grid_param_[cell_index].vel_ * weight;

							// APIC paper equation 10, constructing inner term for B
							const auto c0 = weighted_velocity * dist.X;
							const auto c1 = weighted_velocity * dist.Y;
							B = AddMatrix2x2(B, FMatrix2x2(c0.X, c1.X, c0.Y, c1.Y));

							p.vel_ += weighted_velocity;
						}
					}
					//p.affine_momentum_ = B * 4.0f;
					{
						float m00, m01, m10, m11;
						B.GetMatrix(m00, m01, m10, m11);
						p.affine_momentum_ = MulMatrix2x2(FMatrix2x2(m00, m01, m10, m11), 4.0f);
					}

					// advect particles
					p.pos_ += p.vel_ * dt;

					// safety clamp to ensure particles don't exit simulation domain
					p.pos_.X = FMath::Clamp(p.pos_.X, 1.0f, grid_reso_.X - 2.0f);
					p.pos_.Y = FMath::Clamp(p.pos_.Y, 1.0f, grid_reso_.Y - 2.0f);


					// deformation gradient update - MPM course, equation 181
					// Fp' = (I + dt * p.C) * Fp
					auto Fp_new = FMatrix2x2();// Identity
					Fp_new = AddMatrix2x2(Fp_new, MulMatrix2x2(p.affine_momentum_, dt));
					particle_deform_grad_[pi] = MulMatrix2x2(Fp_new, particle_deform_grad_[pi]);

				}
			}
		}
		// テスト用のパーティクルセットアップ
		void MlsMpm2d_Elastic::SetupTestParticle()
		{
			FVector2D center = FVector2D(grid_reso_.X, grid_reso_.Y) * 0.5f;

			AddParticle(FVector2D(center.X, 10), FVector2D(0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 1.0f, 10), FVector2D(0.0f, -5.0f));
			AddParticle(FVector2D(center.X + 0.5f * 2.0f, 10), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 3.0f, 10), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 4.0f, 10), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 5.0f, 10), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 6.0f, 10), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 7.0f, 10), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 8.0f, 10), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 9.0f, 10), FVector2D(0.0f, 0.0f));

			AddParticle(FVector2D(center.X + 0.5f * 5.0f, 10.0f + 0.5 * 1.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 5.0f, 10.0f + 0.5 * 2.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 5.0f, 10.0f + 0.5 * 3.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 5.0f, 10.0f + 0.5 * 4.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 5.0f, 10.0f + 0.5 * 5.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 5.0f, 10.0f + 0.5 * 6.0f), FVector2D(0.0f, 0.0f));

			AddParticle(FVector2D(center.X + 0.5f * 6.0f, 10.0f + 0.5 * 6.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 7.0f, 10.0f + 0.5 * 6.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 8.0f, 10.0f + 0.5 * 6.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 9.0f, 10.0f + 0.5 * 6.0f), FVector2D(0.0f, 0.0f));

			AddParticle(FVector2D(center.X + 0.5f * 9.0f, 10.0f + 0.5 * 1.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 9.0f, 10.0f + 0.5 * 2.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 9.0f, 10.0f + 0.5 * 3.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 9.0f, 10.0f + 0.5 * 4.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 9.0f, 10.0f + 0.5 * 5.0f), FVector2D(0.0f, 0.0f));
			AddParticle(FVector2D(center.X + 0.5f * 9.0f, 10.0f + 0.5 * 6.0f), FVector2D(0.0f, 0.0f));

		}
		//--------------------------------------------------------------------------------------------------------------------------------------

		//--------------------------------------------------------------------------------------------------------------------------------------
		bool MlsMpm2d_Fluid::Initialize(int grid_reso_x, int grid_reso_y)
		{
			static const int grid_rezo_z = 1;
			// grid reso should be larger then 3.
			grid_reso_ = FIntVector(std::max(grid_reso_x, 3), std::max(grid_reso_y, 3), grid_rezo_z);

			int num_cell = (grid_reso_.X) * (grid_reso_.Y) * (grid_reso_.Z);
			grid_param_.SetNumUninitialized(num_cell);

			return true;
		}
		void MlsMpm2d_Fluid::AddParticle(const FVector2D& pos, const FVector2D& vel)
		{
			static const auto ZeroMatrix = GetZeroMatrix2x2();


			Particle p;
			p.mass_ = 1.0f;
			p.pos_ = pos;
			p.vel_ = vel;
			p.affine_momentum_ = ZeroMatrix;

			particle_.Push(p);
		}
		void MlsMpm2d_Fluid::AdvanceSimulation(float delta_sec)
		{
			auto FuncQuadraticInterpolationWeights = [](const FVector2D& cell_diff, FVector2D* w_3)
			{
				w_3[0] = FVector2D(0.5f) * FMath::Square(FVector2D(0.5f) - cell_diff);
				w_3[1] = FVector2D(0.75f) - FMath::Square(cell_diff);
				w_3[2] = FVector2D(0.5f) * FMath::Square(FVector2D(0.5f) + cell_diff);
			};

			const float dt = delta_sec;
			const auto gravity = FVector2D(0.0f, -9.8f) * 1.0f;

			// fluid parameters
			const float rest_density = 4.0f;//4.0f;
			const float dynamic_viscosity = 0.05f;//0.1f;
			// equation of state
			const float eos_stiffness = 50.0f;//10.0f;
			const float eos_power = 4;//4.0f;


			// Clear Cell
			{
				static const Cell ZeroCell = Cell::Zero();
				for (auto&& e : grid_param_)
					e = ZeroCell;
			}

			// P2G
			{
				FVector2D weights[3];

				for (int pi = 0; pi < particle_.Num(); ++pi)
				{
					const auto& p = particle_[pi];

					// quadratic interpolation weights
					FIntVector cell_idx = FIntVector(p.pos_.X, p.pos_.Y, 0.0);
					FVector2D cell_diff = (p.pos_ - FVector2D(cell_idx.X, cell_idx.Y)) - 0.5f;

					FuncQuadraticInterpolationWeights(cell_diff, weights);

					const auto affine_mat = p.affine_momentum_;

					// for all surrounding 9 cells (2D)
					for (int gx = 0; gx < 3; ++gx)
					{
						for (int gy = 0; gy < 3; ++gy)
						{
							float weight = weights[gx].X * weights[gy].Y;

							FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);

							FVector2D cell_dist = (FVector2D(cell_x.X, cell_x.Y) - p.pos_) + 0.5f;

							//FVector2D Q = affine_mat.TransformVector(cell_dist);
							FVector2D Q = MulMatrix2x2(affine_mat, cell_dist);

							// MPM course, equation 172
							float mass_contrib = weight * p.mass_;

							// converting 2D index to 1D
							int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;
							Cell& cell = grid_param_[cell_index];

							// scatter mass to the grid
							cell.mass_ += mass_contrib;

							cell.vel_ += mass_contrib * (p.vel_ + Q);
						}
					}
				}
			}

			// P2G Second
			{
				FVector2D weights[3];

				for (auto& p : particle_)
				{
					// reset velocity
					p.vel_ = FVector2D::ZeroVector;

					// quadratic interpolation weights
					FIntVector cell_idx = FIntVector(p.pos_.X, p.pos_.Y, 0);
					FVector2D cell_diff = (p.pos_ - FVector2D(cell_idx.X, cell_idx.Y)) - 0.5f;

					FuncQuadraticInterpolationWeights(cell_diff, weights);

					float density = 0.0f;
					for (int gx = 0; gx < 3; ++gx)
					{
						for (int gy = 0; gy < 3; ++gy)
						{
							float weight = weights[gx].X * weights[gy].Y;

							FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);
							int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;

							density += grid_param_[cell_index].mass_ * weight;
						}
					}
					float volume = p.mass_ / density;// 体積.



					// end goal, constitutive equation for isotropic fluid: 
					// stress = -pressure * I + viscosity * (velocity_gradient + velocity_gradient_transposed)

					// Tait equation of state. i clamped it as a bit of a hack.
					// clamping helps prevent particles absorbing into each other with negative pressures
					float pressure = FMath::Max(-0.1f, eos_stiffness * (FMath::Pow(density / rest_density, eos_power) - 1.0f));

					auto stress = FMatrix2x2(
						-pressure, 0,
						0, -pressure
					);

					// velocity gradient - CPIC eq. 17, where deriv of quadratic polynomial is linear
					const auto dudv = p.affine_momentum_;
					auto strain = dudv;

					//float trace = strain.c1.x + strain.c0.y;
					float trace = TraceMatrix2x2(TransposeMatrix2x2(strain));
					//strain.c0.y = strain.c1.x = trace;
					{
						float m00, m01, m10, m11;
						strain.GetMatrix(m00, m01, m10, m11);
						strain = FMatrix2x2(m00, trace, trace, m11);
						// Newtonian fluidでは反対角成分ii成分とjj成分も2倍のはずなので修正.
						//strain = FMatrix2x2(m00 + m00, trace, trace, m11 + m11);
					}


					auto viscosity_term = MulMatrix2x2(strain, dynamic_viscosity);
					//stress += viscosity_term;
					stress = AddMatrix2x2(stress, viscosity_term);

					//const auto eq_16_term_0 = -volume * 4 * stress * dt;
					const auto eq_16_term_0 = MulMatrix2x2(stress, -volume * 4.0f * dt);

					for (int gx = 0; gx < 3; ++gx)
					{
						for (int gy = 0; gy < 3; ++gy)
						{
							float weight = weights[gx].X * weights[gy].Y;

							FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);
							int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;

							FVector2D cell_dist = (FVector2D(cell_x.X, cell_x.Y) - p.pos_) + 0.5f;

							// fused force + momentum contribution from MLS-MPM
							//FVector2D momentum = math.mul(eq_16_term_0 * weight, cell_dist);
							FVector2D momentum = MulMatrix2x2(MulMatrix2x2(eq_16_term_0, weight), cell_dist);

							grid_param_[cell_index].vel_ += momentum;
						}
					}
				}
			}

			// Update Cell
			{
				for (int i = 0; i < grid_param_.Num(); ++i)
				{
					auto& cell = grid_param_[i];

					if (0.0f < cell.mass_)
					{
						// convert momentum to velocity, apply gravity
						cell.vel_ /= cell.mass_;
						cell.vel_ += dt * gravity;

						// boundary conditions
						int y = i / grid_reso_.X;
						int x = i - y * grid_reso_.X;
						if (x < 2 || x > grid_reso_.X - 3) { cell.vel_.X = 0; }
						if (y < 2 || y > grid_reso_.Y - 3) { cell.vel_.Y = 0; }

					}
				}
			}

			// G2P
			{
				FVector2D weights[3];
				for (int pi = 0; pi < particle_.Num(); ++pi)
				{
					auto& p = particle_[pi];

					// reset velocity
					p.vel_ = FVector2D::ZeroVector;

					// quadratic interpolation weights
					FIntVector cell_idx = FIntVector(p.pos_.X, p.pos_.Y, 0);
					FVector2D cell_diff = (p.pos_ - FVector2D(cell_idx.X, cell_idx.Y)) - 0.5f;

					FuncQuadraticInterpolationWeights(cell_diff, weights);

					// constructing affine per-particle momentum matrix from APIC / MLS-MPM.
					// see APIC paper (https://web.archive.org/web/20190427165435/https://www.math.ucla.edu/~jteran/papers/JSSTS15.pdf), page 6
					// below equation 11 for clarification. this is calculating C = B * (D^-1) for APIC equation 8,
					// where B is calculated in the inner loop at (D^-1) = 4 is a constant when using quadratic interpolation functions
					FMatrix2x2 B = GetZeroMatrix2x2();
					for (int gx = 0; gx < 3; ++gx)
					{
						for (int gy = 0; gy < 3; ++gy)
						{
							float weight = weights[gx].X * weights[gy].Y;

							FIntVector cell_x = FIntVector(cell_idx.X + gx - 1, cell_idx.Y + gy - 1, 0);
							int cell_index = (int)cell_x.X + grid_reso_.X * (int)cell_x.Y;

							FVector2D dist = (FVector2D(cell_x.X, cell_x.Y) - p.pos_) + 0.5f;
							FVector2D weighted_velocity = grid_param_[cell_index].vel_ * weight;

							// APIC paper equation 10, constructing inner term for B
							const auto c0 = weighted_velocity * dist.X;
							const auto c1 = weighted_velocity * dist.Y;
							B = AddMatrix2x2(B, FMatrix2x2(c0.X, c1.X, c0.Y, c1.Y));

							p.vel_ += weighted_velocity;
						}
					}
					//p.affine_momentum_ = B * 4.0f;
					{
						float m00, m01, m10, m11;
						B.GetMatrix(m00, m01, m10, m11);
						p.affine_momentum_ = MulMatrix2x2(FMatrix2x2(m00, m01, m10, m11), 4.0f);
					}

					// advect particles
					p.pos_ += p.vel_ * dt;

					// safety clamp to ensure particles don't exit simulation domain
					p.pos_.X = FMath::Clamp(p.pos_.X, 1.0f, grid_reso_.X - 2.0f);
					p.pos_.Y = FMath::Clamp(p.pos_.Y, 1.0f, grid_reso_.Y - 2.0f);
				}
			}
		}
		// テスト用のパーティクルセットアップ
		void MlsMpm2d_Fluid::SetupTestParticle()
		{
			FVector2D center = FVector2D(grid_reso_.X, grid_reso_.Y) * 0.5f;
			{
				const float spacing = 0.5f;
				const int box_x = 16, box_y = 16;
				const float sx = grid_reso_.X / 2.0f, sy = grid_reso_.Y / 2.0f;
				for (float i = sx - box_x / 2; i < sx + box_x / 2; i += spacing) {
					for (float j = sy - box_y / 2; j < sy + box_y / 2; j += spacing) {
						auto pos = FVector2D(i, j);
						auto vel = FVector2D::ZeroVector; //FVector2D(FMath::FRand() * 2.0f - 1.0f, FMath::FRand() + 2.75f) * 6.0f;
						AddParticle(pos, vel);
					}
				}
			}
		}
	}
}