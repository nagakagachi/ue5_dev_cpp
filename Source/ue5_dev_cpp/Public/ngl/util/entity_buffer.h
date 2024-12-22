#pragma once

#include "CoreMinimal.h"

#include <tuple>

#include "math_util.h"

namespace ngl
{
	//	効率的なエンティティの追加と削除、ID管理をサポートするクラス.
	//		追加でエンティティのIDを生成し、内部でそのIDとデータを紐付ける
	//		ID指定で削除(削除情報の登録だけで実際の削除は次のPrepareForNextFrame実行まで遅延される)
	//		ID指定でデータインデックスの取得(内部で二分探索するので何度も呼ばないようにする)
	//		PrepareForNextFrame()を定期的(1フレームに1度)実行することでID削除の反映やID管理情報の整理などをする
	class NglEntityIdManager
	{
	public:
		using EntityID = uint32_t;
		using EntityID_Index_Pair = TPair<EntityID, uint32_t>;
		using EntityID_Index_Array = TArray<EntityID_Index_Pair>;

		static EntityID InvalidID()
		{
			return (EntityID(~0x00));
		}

	public:
		NglEntityIdManager();
		virtual ~NglEntityIdManager();

	private:
		// 実際のデータバッファを増やす処理は派生クラスで実装
		virtual void AddBufferElement() = 0;

	public:
		// エンティティ作成
		EntityID	NewEntity();

		// エンティティ削除
		void		RemoveEntity(const EntityID& id);
		// エンティティ全て削除
		void		RemoveEntityAll();

		// エンティティIDからデータインデックスを取得( 内部で二分探索 )
		int			GetEntityDataIndex(const EntityID& id) const;
		// エンティティバッファに直接アクセスしてIDとデータインデックスをを取得
		EntityID_Index_Pair	GetEntityInfo(int index) const;

		// 有効なエンティティ数
		int			NumEnableEntity() const;

		// フレームに一度実行する必要がある.
		// エンティティ削除の反映やエンティティIDバッファの前詰などをする.
		void		PrepareForNextFrame();

	protected:
		int BinarySearchEntityDataIndex(const EntityID_Index_Array& id_index_array, int start_index, int search_range, EntityID search_id) const;

	protected:
		// 次に生成されるエンティティのユニークID
		EntityID							next_spawn_entity_id_ = 0;
		// 前回のUpdateで確定されたメインバッファ側のIDの個数
		EntityID							used_entity_id_count_ = 0;

		// id_to_data_index_array_ の先頭から何個が有効なIDかを格納. ソート後に走査しても良いが個数が多くなると問題になりそうなので追加と削除のタイミングで適宜これを増減する.
		int									enable_entity_id_count_ = 0;
		// 前回のUpdate以降に追加された要素とそれ以外を識別するために前回の値として保持
		int									prev_enable_entity_id_count_ = 0;

		// UpdateでKeyソートされるIDとデータインデックスの配列. Update完了後から次のUpdateの内部処理までの間は常にKeyで昇順になっている(そのようになるように追加削除をしている)
		EntityID_Index_Array				id_to_data_index_array_;

		// 今回のフレームで削除指定されたIDを格納する. 第二要素は実際の削除時に一旦削除用情報をストアするための作業領域.
		// 実際の削除は次のUpdate
		TArray<TPair<EntityID, int>>			remove_id_info_array_;
	};

	// BUFFER_CLASS_TYPEはAdd()メソッドを実装して内部データ要素数を一つ増やす機能をもつ必要がある.
	template<typename BUFFER_CLASS_TYPE>
	class NglEntityIdManagerT : public NglEntityIdManager
	{
	public:

	public:
		NglEntityIdManagerT()
		{
		}
		~NglEntityIdManagerT()
		{
		}
		// 実際にバッファクラスへアクセスするため
		const BUFFER_CLASS_TYPE& GetBuffer() const { return buffer_; }
		BUFFER_CLASS_TYPE& GetBuffer() { return buffer_; }
	private:
		void AddBufferElement()
		{
			// バッファクラスはAdd()メソッドを実装して内部データ要素数を一つ増やす機能をもつ必要がある.
			buffer_.Add();
		}

	protected:
		// データ部
		BUFFER_CLASS_TYPE					buffer_;
	};


	/*
		固定サイズ要素を高速で割当/解除するためのクラス.
		管理ビットサイズのn分木で高速に空き要素を検索して割り当てる.
	
		下位n要素の使用状況をbit管理し,使用状態に変更/使用状態を解除したらそれを上位階層に伝搬することでbit演算で空き領域検索が可能.
	
	*/
	class NglEntityBuffer
	{
	public:
		//using EntityID = uint32_t;
		struct EntityID
		{
			constexpr EntityID(uint32_t v) : id_(v)
			{
			}
			constexpr EntityID()
			{
			}
			constexpr operator uint32_t() const { return id_; }
			uint32_t id_ = ~uint32_t(0);
		};

		// 何要素単位で管理するか. 現状は32bit==32要素管理単位にする.
		using BitElementType = uint32_t;

		static constexpr BitElementType FillMask()
		{
			return ~BitElementType(0);
		}
		static constexpr int BitElementManageSize()
		{
			return 8 * sizeof(BitElementType);
		}

		static constexpr EntityID InvalidID()
		{
			return ~EntityID(0);
		}
		/*
			one element tree manage 32^max_level element.
			for example, max_level==2 then element count will be 1024.
			If the size is not enough, a tree is added.
	
			一つの管理木が 32^max_level の要素を管理できる.
			例えば max_level==2 の場合は 1024要素.
			サイズが足りなくなった場合は木が追加される.
		*/
		NglEntityBuffer(int max_level = 2);
		virtual ~NglEntityBuffer();

	private:
		// New()によってツリーの追加が発生した際のコールバック.
		// append_element_count : managed element count of single tree. ツリー一つが管理できる要素数,
		virtual void OnAppendTree(int append_element_count)
		{
			// TODO.
		}
	public:
		// 新規生成. 戻り値は識別ID. 
		EntityID New();
		// IDからバッファインデックスを取得.
		// 無効なIDの場合 -1 .
		// template引数FAST_MODEがfalseの場合は使用ビットまでチェックする. 基本的には true でも問題ないはず.
		template<bool FAST_MODE = true>
		int GetBufferIndex(EntityID id) const
		{
			constexpr BitElementType mask_all_1 = FillMask();
			constexpr int sizeof_element = BitElementManageSize();
			// sizeof_elementはビットサイズなので2の冪数のはずなのでMSBでlogの代用. constexprも使える.
			constexpr int sizeof_element_log2 = ngl::math::Msb(uint32_t(sizeof_element));


			int buffer_index = 0;
			uint8_t generation = 0;
			DecodeID(id, buffer_index, generation);

			// 範囲外, または世代が異なるため同じIDで二重解放しようとしているので無効
			if (element_generation_buffer_.Num() <= buffer_index || generation != element_generation_buffer_[buffer_index])
				return -1;

			// 高速モードの場合は使用ビットのチェックをスキップする
			if (!FAST_MODE)
			{
				// 所属ツリーを判定
				const int tree_index = buffer_index >> (max_level_ * sizeof_element_log2);
				// ツリー内での最終インデックス
				const int index_in_tree = buffer_index - (tree_index << (max_level_ * sizeof_element_log2));

				// 使用ビットまでチェックする
				constexpr int element_bit_mask = (1 << sizeof_element_log2) - 1;
				int bit = 1 << (index_in_tree & element_bit_mask);

				const auto tree_offset = tree_index * (1 << ((max_level_ - 1) * sizeof_element_log2));
				// ビット表現バッファのインデックス. 1要素32bitならツリー内のインデックスを32で割った値(5ビット右シフト)のインデックス要素で管理される
				const auto bit_element_index = index_in_tree >> (1 * sizeof_element_log2);
				// 最下層の対応ビットが1でない場合は破棄済みなので無効
				if (!(status_bit_hierarchy_[(max_level_ - 1)][tree_offset + bit_element_index] & bit))
				{
					return -1;
				}
			}
			return buffer_index;
		}
		int Delete(EntityID id);
		// インデックスが有効か.
		//	インデックス要素が使用中ならtrue.
		bool IsValid(EntityID id) const;
		// 全削除
		void DeleteAll();
		// 要素最大数
		// シーケンシャルアクセス用
		int NumMaxElement() const;
		// 有効要素の個数
		int NumEnableElement() const;
		// 要素インデックスからID取得. 無効な要素の場合は InvalidID() を返す.
		// get ID from element index. return InvalidID() if element is invalid.
		EntityID GetElementEntityID(int index) const;
		// インデックス指定した要素が有効か.
		bool IsValidIndex(int index) const;
		// 有効な要素の最初のインデックスを返す
		int GetFirstEnableIndex() const;
		// 有効な要素の最後のインデックスを返す
		int GetLastEnableIndex() const;

	private:
		constexpr EntityID CreateID(int buffer_index, uint8_t generation) const;
		void DecodeID(EntityID id, int& out_buffer_index, uint8_t& out_generation) const;

	private:
		int						max_level_ = 2;
		TArray<BitElementType>*	status_bit_hierarchy_;

		// 要素の生成と削除で以前のIDを無効と判定するための世代数管理.8bitなので255回New/Deleteしたあとに最初のIDを問い合わせると有効判定されてしまうが運用で回避したい.
		TArray<uint8_t>		element_generation_buffer_;

		// バッファ内で有効な要素の数. シーケンシャルアクセス時に早期スキップするため.
		int					enable_element_count_ = 0;

		// 最上位の管理単位で管理可能な要素数. ビット管理サイズ^最大レベル で計算される.
		int					max_tree_element_count_ = 0;
	};






	// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

	// パーティクル管理
	class ParticleBuffer : public NglEntityBuffer
	{
	public:
		ParticleBuffer()
		{
		}
		~ParticleBuffer()
		{
		}

		TArray<FVector>	position_prev_;
		TArray<FVector>	position_;
		TArray<FVector>	velocity_;
		TArray<float>	inv_mass_;
		TArray<FVector>	ex_force_;
		TArray<FVector>	constraint_force_;

	private:
		// エンティティマネージャで要素追加が発生した場合にはその分バッファを増やす
		void OnAppendTree(int append_element_count) override
		{
			position_prev_.AddZeroed(append_element_count);
			position_.AddZeroed(append_element_count);
			velocity_.AddZeroed(append_element_count);
			inv_mass_.AddZeroed(append_element_count);
			ex_force_.AddZeroed(append_element_count);
			constraint_force_.AddZeroed(append_element_count);
		}
	};

	// パーティクル間の距離拘束条件管理
	class DistanceConstraintBuffer : public NglEntityBuffer
	{
	public:
		using ConstraintPair = TPair<ParticleBuffer::EntityID, ParticleBuffer::EntityID>;
		// パーティクルペアとその間の拘束距離
		using Constraint = TPair<ConstraintPair, float>;

		TArray<Constraint>	constraint_;

		// 前回の反復時の最終Lambda
		TArray<float>		prev_lambda_;
		// 前回の反復時の最終Lambda勾配 収束チェックのためと勾配のWarmStartとかを試すために保存
		TArray<float>		prev_lambda_grad_;

		//	パーティクルデータと拘束条件インデックスを受け取って該当する拘束条件の諸々の計算をして結果を返す
		//	戻り値は拘束条件インデックスに該当する拘束条件が使用中かどうかの真偽値. 拘束条件バッファの先頭から処理して使用中拘束条件を全て処理したら早期切り上げするために利用.
		bool CalcConstraint(const ParticleBuffer& particles, int constraint_index,
			TPair<int, int>& out_particle_indices, TPair<FVector, FVector>& out_jacobian,
			float& out_constraint_value, float& out_constraint_violation, TPair<float, float>& lambda_min_max, float*& out_prev_lambda_ptr, float*& out_prev_lambda_grad_ptr)
		{
			const float epsiolon = 1e-7f;
			const float projection_limit = 1e7f;
			//const float projection_limit = 1e9f;

			const auto id = GetElementEntityID(constraint_index);
			if (DistanceConstraintBuffer::InvalidID() == id)
				return false;// 無効な拘束なので false
			// -------------------------------------------------------------

			const auto pairID0 = constraint_[constraint_index].Key.Key;
			const auto pairID1 = constraint_[constraint_index].Key.Value;
			const auto const_dist = constraint_[constraint_index].Value;

			// ペアを構成するIDから実インデックスを取得
			const auto pair0 = particles.GetBufferIndex(pairID0);
			const auto pair1 = particles.GetBufferIndex(pairID1);

			// 拘束を構成するパーティクルが無効になっていたらスキップ
			if (0 > pair0 || 0 > pair1)
			{
				// ついでに無効なコンストレインとして削除しておく(有効な要素数がデクリメントされる)
				Delete(id);
				return false;// 無効な拘束にしたので false
			}

			// --------------------------------------------------------------------------------
			// 拘束条件のJacobianと違反値計算
			const auto dist_vec = particles.position_[pair1] - particles.position_[pair0];
			FVector dist_dir;
			float dist_len;
			dist_vec.ToDirectionAndLength(dist_dir, dist_len);

			if (epsiolon >= dist_len)
			{
				// 念の為Jacobianが計算できないような座標一致の場合をスキップ
				return false;
			}

			// 拘束条件自体の値
			// Modeling and solving constrain 参考
			const auto constraint_value = 0.5f * (dist_len*dist_len - const_dist * const_dist);


			// 距離拘束のBaumgarte項の拘束条件からの位置に関する違反
			const auto constraint_position_violation = dist_len - const_dist;

			// TODO
			// Jacobianを正規化ベクトルとする. 
			// 論文では長さベクトルそのままだったが, NGL_CONSTRAINT_BASED_PHYSICS_VIOLATION_FORCE_BIAS で処理している拘束力への違反変位量の加算で非常に良い結果が出たので要調査.
			const auto jacobian0 = -dist_dir;
			const auto jacobian1 = dist_dir;
			// --------------------------------------------------------------------------------
			// 後段で近傍拘束の検索のためにパーティクルの実インデックスが必要なため
			out_particle_indices = TPair<int, int>(pair0, pair1);
			// この拘束条件におけるパーティクルのJacobian
			out_jacobian = TPair<FVector, FVector>(jacobian0, jacobian1);
			// 拘束条件の違反量. Baumgarteの安定化法の項に使用される
			out_constraint_violation = constraint_position_violation;
			// 拘束条件自体の値
			out_constraint_value = constraint_value;
			// Projected-Gauss-Seidelの拘束力制限. 本来距離拘束の力は無制限だがfloatなので適当にClampしないと発散する
			lambda_min_max = TPair<float, float>(-projection_limit, projection_limit);

			// warm starting値のアドレス. 前回のlambdaを多少減衰させて反復の初期値に利用することで継続状態の安定性を高めるため. また反復完了後にlambdaを書き戻す.
			out_prev_lambda_ptr = &prev_lambda_[constraint_index];
			out_prev_lambda_grad_ptr = &prev_lambda_grad_[constraint_index];
			return true;
		}

	private:
		// for template class
		void OnAppendTree(int append_element_count) override
		{
			constraint_.AddUninitialized(append_element_count);
			prev_lambda_.AddUninitialized(append_element_count);
			prev_lambda_grad_.AddUninitialized(append_element_count);
		}
	};


	// 拘束ベース物理のテスト
	class PhysicsParticleSystem
	{
	public:
		using ParticleID = ParticleBuffer::EntityID;
		using DistansConstID = DistanceConstraintBuffer::EntityID;

		PhysicsParticleSystem();

		// パーティクル追加
		ParticleID AddParticle(const FVector& pos, float mass);
		// パーティクル削除
		void RemoveParticle(ParticleID id);

		DistansConstID AddDistanceConstraint(ParticleID p0, ParticleID p1, float distance);
		void RemoveDistanceConstraint(DistansConstID id);

		void SolveConstraint(float delta_sec, int iteration_count = 7, float vioration_penarty_bias_rate = 0.25f, float momentum_rate = 0.75f,
			float warm_start_rate = 0.0f, float grad_warm_start_rate = 1.0f);



		void AddDebugExForceSphere(const FSphere& sphere, float intensity)
		{
			external_force_sphere_shape_.Add(sphere);
			external_force_sphere_intensity_.Add(intensity);
		}
		TArray <FSphere>	external_force_sphere_shape_;
		TArray <float>		external_force_sphere_intensity_;



		ParticleBuffer		particles_;
		DistanceConstraintBuffer distnce_const_;
	};


	// 固定半径粒子群の近傍処理.
	class NglNeighborhoodTraverseGrid
	{
	public:
		using MortonCodeKey = uint32_t;
		static const MortonCodeKey InvalidMortonCodeKey = ~MortonCodeKey(0u);

		using PairUintInt = std::tuple<MortonCodeKey, int>;
		using PairIntInt = std::tuple<int, int>;

	public:
		NglNeighborhoodTraverseGrid()
		{
		}
		~NglNeighborhoodTraverseGrid()
		{
		}
		// src_position : 全粒子の位置バッファ
		// src_index	: 全粒子の内有効なインデックスを格納したバッファ
		// space_box	: シミュレーション空間のボックス情報. この空間内のみシミュレーションが動作.
		void Rebuild(const TArray<FVector>& src_position, const TArray<int>& src_index, float src_element_radius, const FBox& space_box)
		{
			auto func_is_inside = [](const FIntVector grid_pos, const FIntVector& sim_space_reso)
			{
				return (0 <= grid_pos.X && sim_space_reso.X > grid_pos.X) && (0 <= grid_pos.Y && sim_space_reso.Y > grid_pos.Y) && (0 <= grid_pos.Z && sim_space_reso.Z > grid_pos.Z);
			};

			{
				// この実装では 10bit 10bit 10bit のMortonCodeを利用するためグリッド解像度は各軸最大1024とする
				const int grid_reso_max = 1024;

				// 可能性のある最大の粒子直径をセルのサイズとする.
				grid_cell_size_ = src_element_radius * 2.0f;
				// グリッドがカバーする空間
				grid_space_ = space_box;

				{
					auto temp_space_size = grid_space_.GetSize();
					// 空間サイズの下限保証
					temp_space_size = FVector(FMath::Max(grid_cell_size_, temp_space_size.X), FMath::Max(grid_cell_size_, temp_space_size.Y), FMath::Max(grid_cell_size_, temp_space_size.Z));
					// 下限保証サイズで再設定
					grid_space_ = FBox(grid_space_.GetCenter() - temp_space_size * 0.5f, grid_space_.GetCenter() + temp_space_size * 0.5f);
				}

				FIntVector temp_grid_reso = FIntVector(grid_space_.GetSize() / grid_cell_size_);
				temp_grid_reso = FIntVector(FMath::Min(grid_reso_max, temp_grid_reso.X), FMath::Min(grid_reso_max, temp_grid_reso.Y), FMath::Min(grid_reso_max, temp_grid_reso.Z));
				// 安全のために最低値1
				grid_reso_ = FIntVector(FMath::Max(1, temp_grid_reso.X), FMath::Max(1, temp_grid_reso.Y), FMath::Max(1, temp_grid_reso.Z));

				// 確定したグリッド解像度によってセルサイズを再計算
				grid_cell_size_ = (grid_space_.GetSize() / FVector(grid_reso_)).GetMax();
			}

			// メモリ保持しつつ空に.
			grid_key_value_.Empty(grid_key_value_.Max());

			for (auto ei = 0; ei < src_index.Num(); ++ei)
			{
				const auto i = src_index[ei];
				const auto grid_pos = FIntVector((src_position[i] - grid_space_.Min) / (src_element_radius*2.0f));

				const auto is_inside = func_is_inside(grid_pos, grid_reso_);

				// XYZ各軸に10bit割当てた32bitのMortonCodeを計算(2bit余り. 各軸の解像度は1024)
				// 領域外の場合はInvalid値(全ビット1)
				MortonCodeKey morton = InvalidMortonCodeKey;
				if (is_inside)
				{
					morton = ngl::math::EncodeMortonCodeX10Y10Z10(grid_pos.X, grid_pos.Y, grid_pos.Z); // Plugin側ではNglMathUtilに置き換え
				}
				grid_key_value_.Add(PairUintInt(morton, i));// MortonCodeと実インデックス
			}

			// ソート.
			const auto array_size = grid_key_value_.Num();
			grid_key_value_.Sort(
				[](const PairUintInt& v0, const PairUintInt& v1) { return std::get<0>(v0) < std::get<0>(v1); }
			);

			// レンジバッファ構築.
			grid_key_kind_count_ = 0;
			if (array_size > grid_key_kind_.Num())
			{
				grid_key_kind_.SetNumUninitialized(array_size);
				grid_key_range_.SetNumUninitialized(array_size);
			}
			if (0 < array_size)
			{
				auto prev_index = 0;
				auto prev_key = std::get<0>(grid_key_value_[prev_index]);
				for (auto i = 1; i < array_size; ++i)
				{
					if (prev_key != std::get<0>(grid_key_value_[i]))
					{
						grid_key_kind_[grid_key_kind_count_] = prev_key;
						grid_key_range_[grid_key_kind_count_] = PairIntInt(prev_index, i - 1);
						++grid_key_kind_count_;

						prev_index = i;
						prev_key = std::get<0>(grid_key_value_[i]);
					}
				}
				// last range.
				grid_key_kind_[grid_key_kind_count_] = prev_key;
				grid_key_range_[grid_key_kind_count_] = PairIntInt(prev_index, array_size - 1);
				++grid_key_kind_count_;
			}
		}

		// Targetは近傍ペアのインデックスを引数に取るTraverseNeighborPair()を実装するクラス
		// Rebuild()で近傍情報を構築したあとにこの関数で実際の近傍処理を実行させる.
		template<typename Target>
		void TraverseNeighborPair(Target& target)
		{
			// 隣接セルの一方向オフセット. 隣接セルペアを一括処理するため26近傍の半分の方向を定義.
			const FIntVector n_oneside_offset[] =
			{
				// X=1
				{1,1,-1}, {1,1,0}, {1,1,1}, {1,0,-1}, {1,0,0}, {1,0,1}, {1,-1,-1}, {1,-1,0}, {1,-1,1},
				// X=0, Y=1
				{0,1,-1}, {0,1,0}, {0,1,1},
				// X=0, Y=1, Z=1
				{0,0,1},
			};
			constexpr auto n_oneside_offset_size = sizeof(n_oneside_offset) / sizeof(n_oneside_offset[0]);

			// 同一セル内処理
			for (auto i = 0; i < grid_key_kind_count_; ++i)
			{
				const auto key = grid_key_kind_[i];
				if (InvalidMortonCodeKey == key)
					continue;

				const auto head = std::get<0>(grid_key_range_[i]);
				const auto tail = std::get<1>(grid_key_range_[i]);

				for (auto j = head; j <= tail - 1; ++j)
				{
					for (auto k = j + 1; k <= tail; ++k)
						target.TraverseNeighborPair(std::get<1>(grid_key_value_[j]), std::get<1>(grid_key_value_[k]));
				}
			}

			// 離接セル間処理
			for (auto i = 0; i < grid_key_kind_count_; ++i)
			{
				const auto key = grid_key_kind_[i];
				if (InvalidMortonCodeKey == key)
					continue;

				const auto head = std::get<0>(grid_key_range_[i]);
				const auto tail = std::get<1>(grid_key_range_[i]);

				FIntVector cur_cell;
				ngl::math::DecodeMortonCodeX10Y10Z10(key, cur_cell.X, cur_cell.Y, cur_cell.Z);

				for (auto c = 0; c < n_oneside_offset_size; ++c)
				{
					FIntVector neighbor_cell = cur_cell + n_oneside_offset[c];
					const auto is_inner = (0 <= neighbor_cell.X && grid_reso_.X > neighbor_cell.X)
						&& (0 <= neighbor_cell.Y && grid_reso_.Y > neighbor_cell.Y)
						&& (0 <= neighbor_cell.Z && grid_reso_.Z > neighbor_cell.Z);
					if (!is_inner)
						continue;
					auto neighbor_morton = ngl::math::EncodeMortonCodeX10Y10Z10(neighbor_cell.X, neighbor_cell.Y, neighbor_cell.Z);

					const auto find_index = ngl::math::BinarySearch(grid_key_kind_, 0, grid_key_kind_count_, neighbor_morton);

					if (0 > find_index)
						continue;

					const auto neighbor_head = std::get<0>(grid_key_range_[find_index]);
					const auto neighbor_tail = std::get<1>(grid_key_range_[find_index]);

					for (auto j = head; j <= tail; ++j)
					{
						for (auto k = neighbor_head; k <= neighbor_tail; ++k)
						{
							target.TraverseNeighborPair(std::get<1>(grid_key_value_[j]), std::get<1>(grid_key_value_[k]));
						}
					}
				}
			}
		}

	private:
		FBox				grid_space_;
		FIntVector			grid_reso_;
		float				grid_cell_size_ = 1.0f;

		TArray<PairUintInt>	grid_key_value_;

		int						grid_key_kind_count_ = 0;
		TArray<MortonCodeKey>	grid_key_kind_;
		TArray<PairIntInt>		grid_key_range_;

	};
}