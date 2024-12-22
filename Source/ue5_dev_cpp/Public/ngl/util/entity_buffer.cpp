
#include "entity_buffer.h"

#include <assert.h>


namespace ngl
{
	NglEntityIdManager::NglEntityIdManager()
	{
	}
	NglEntityIdManager::~NglEntityIdManager()
	{
	}

	// エンティティ作成
	NglEntityIdManager::EntityID	NglEntityIdManager::NewEntity()
	{
		// 新規ID割り当て
		EntityID newId = next_spawn_entity_id_;
		++next_spawn_entity_id_;// IDユニーク番号増やす

		// 前提としてID-Index配列はソート済みであり, 追加されるIDは常に既存のIDより大きい、さらに次回のUpdateまで削除はされないので常に昇順で並んでいることが期待できる.

		int unused_index = -1;

		// id_to_data_index_array_ の先頭から enable_particle_id_count_ は前回Updateで有効なのが確定していて且つソート済みなのでその後ろに使われていないインデックス情報が並んでいる
		// (削除されないということは途中の要素が無効になったりそこに新規の大きな値のIDが差し込まれたりしないということ)
		// まず確保済みで使われていないデータインデックスを探す. 探すのはソート済み範囲の後ろから順番に. こうすることで常に新しい(大きい)IDが後ろに追加される.
		for (int i = enable_entity_id_count_; i < id_to_data_index_array_.Num(); ++i)
		{
			if (InvalidID() == id_to_data_index_array_[i].Key)
			{
				// 未使用を発見
				unused_index = i;
				// 新規IDに書き換え
				id_to_data_index_array_[unused_index].Key = newId;
				break;
			}
		}
		// 未使用要素が見つからなければ新規追加する. 追加した要素のインデックスを使用.
		if (0 > unused_index)
		{
			unused_index = id_to_data_index_array_.Num();
			// 末尾に新規データインデックスを指す情報として追加
			id_to_data_index_array_.Add(EntityID_Index_Pair(newId, unused_index));

			// データバッファ側も要素追加
			AddBufferElement();
		}

		// 新たなパーティクルが追加されたので有効な個数を加算
		++enable_entity_id_count_;

		return newId;
	}

	// エンティティ削除
	void		NglEntityIdManager::RemoveEntity(const EntityID& id)
	{
		remove_id_info_array_.Add(TPair<EntityID, int>(id, -1));
	}
	// エンティティ全て削除
	void		NglEntityIdManager::RemoveEntityAll()
	{
		// パーティクルIDを無効にしていく
		for (int i = 0; i < enable_entity_id_count_; ++i)
		{
			// 無効化
			id_to_data_index_array_[i].Key = InvalidID();
		}
		enable_entity_id_count_ = 0;
	}

	// エンティティIDからデータインデックスを取得( 内部で二分探索 )
	int			NglEntityIdManager::GetEntityDataIndex(const EntityID& id) const
	{
		// 検索範囲を新規追加分と既存部分で分けて高速化
		if (used_entity_id_count_ > id)
		{
			// 削除IDが前回Update時点でソートされているパーティクルならその範囲で二分探索
			return BinarySearchEntityDataIndex(id_to_data_index_array_, 0, prev_enable_entity_id_count_, id);
		}
		else
		{
			// 削除IDが今回フレームで追加したものなら prev_sorted_particle_id_count 以降に昇順に並んでいるはずなのでその範囲で二分探索
			return BinarySearchEntityDataIndex(id_to_data_index_array_, prev_enable_entity_id_count_, id_to_data_index_array_.Num() - prev_enable_entity_id_count_, id);
		}
	}
	// エンティティバッファに直接アクセスしてIDとデータインデックスをを取得
	NglEntityIdManager::EntityID_Index_Pair	NglEntityIdManager::GetEntityInfo(int index) const
	{
		if (NumEnableEntity() <= index)
			return EntityID_Index_Pair(InvalidID(), -1);
		return id_to_data_index_array_[index];
	}

	// 有効なエンティティ数
	int			NglEntityIdManager::NumEnableEntity() const
	{ 
		return enable_entity_id_count_;
	}

	// フレームに一度実行する必要がある.
	// エンティティ削除の反映やエンティティIDバッファの前詰などをする.
	void		NglEntityIdManager::PrepareForNextFrame()
	{
		// 削除するパーティクルIDが id_to_data_index_array_　内の前回Update時点でのパーティクルか今回追加されたパーティクルかを判別するために保持. 二分探索の範囲をできるだけ絞るため.
		const auto prev_sorted_particle_id_count = used_entity_id_count_;
		// メインバッファ側に存在する可能性のあるIDの最大個数( == 最大ID+1の値) を更新
		used_entity_id_count_ = next_spawn_entity_id_;

		// Removeバッファのパーティクルを削除
		for (int i = 0; i < remove_id_info_array_.Num(); ++i)
		{
			auto& rem_id_info = remove_id_info_array_[i];

			int rem_index = -1;
			if (prev_sorted_particle_id_count > rem_id_info.Key)
			{
				// 削除IDが前回Update時点でソートされているパーティクルならその範囲で二分探索
				rem_index = BinarySearchEntityDataIndex(id_to_data_index_array_, 0, prev_enable_entity_id_count_, rem_id_info.Key);
			}
			else
			{
				// 削除IDが今回フレームで追加したものなら prev_sorted_particle_id_count 以降に昇順に並んでいるはずなのでその範囲で二分探索
				rem_index = BinarySearchEntityDataIndex(id_to_data_index_array_, prev_enable_entity_id_count_, id_to_data_index_array_.Num() - prev_enable_entity_id_count_, rem_id_info.Key);
			}

			// 対応する要素を無効化するが,ここでKeyを書き換えてしまうとほかの削除IDのための二分探索ができなくなるため第二要素に削除用の情報を保持しておく
			rem_id_info.Value = rem_index;

		}
		// 実際に削除パーティクルIDを無効にしていく
		for (int i = 0; i < remove_id_info_array_.Num(); ++i)
		{
			const auto& rem_id_info = remove_id_info_array_[i];
			auto rem_index = rem_id_info.Value;
			// 無効にする要素のインデックスが検索にヒットしてセットされていたら処理
			if (0 <= rem_index)
			{
				// 複数回に削除リクエストされる可能性があるためすでに無効化されているかチェックする
				if (InvalidID() != id_to_data_index_array_[rem_index].Key)
				{
					// 実際に無効化
					id_to_data_index_array_[rem_index].Key = InvalidID();
					// 無効化したので有効パーティクル数を減らす
					assert(0 < enable_particle_id_count_);// 一応チェック
					enable_entity_id_count_ = (0 < enable_entity_id_count_) ? enable_entity_id_count_ - 1 : enable_entity_id_count_;

				}
			}
		}
		// 削除IDバッファをクリアする. UE4のTArray::Emptyは特に指定しないとメモリを解放してしまい次のフレームでまた追加のたびにメモリ確保してしまう.
		// 現在のサイズを引数に指定してメモリ領域自体はそのまま保持するように指定しておく.
		remove_id_info_array_.Empty(remove_id_info_array_.Max());

		// 削除も完了したのでこの時点の有効パーティクル数を保持
		prev_enable_entity_id_count_ = enable_entity_id_count_;

		// ソート
		id_to_data_index_array_.Sort([](const EntityID_Index_Pair& l, const EntityID_Index_Pair& r) { return l.Key < r.Key; });
	}

	int NglEntityIdManager::BinarySearchEntityDataIndex(const EntityID_Index_Array& id_index_array, int start_index, int search_range, EntityID search_id) const
	{
#if 0
		// 二分探索functor ( 同一値が連続する場合はその先頭を返す実装 )
		const auto maxCount = search_range; // id_index_array.Num();
		const auto maxIndex = maxCount - 1;
		const auto searchLevel = static_cast<int>(FMath::Log2(maxCount)) + 1;
		auto i = 0;
		for (int l = 1; l <= searchLevel; ++l)
		{
			const auto width = maxCount >> (l);
			auto piv = FMath::Min(maxIndex, i + width);
			const auto c = id_index_array[piv + start_index].Key;// 実際に参照するインデックスはオフセットする
			if (c < search_id)
				i = piv + 1;
		}
		const auto ret_index = i + start_index;
		// 範囲内部にiが収まっていて且つオフセットした実際のインデックス値が検索対象と一致すればOK
		return (maxCount > i && id_index_array[ret_index].Key == search_id) ? ret_index : -1;
#else
		// 二分探索functor ( 同一値が連続する場合はその先頭を返す実装 )	
		int max_count = start_index + search_range;
		int left = start_index - 1;
		int right = max_count;

		while (abs(right - left) > 1) {
			int mid = (right + left) / 2;
			if (search_id <= id_index_array[mid].Key)
				right = mid;
			else
				left = mid;
		}
		return (max_count > right && search_id == id_index_array[right].Key) ? right : -1;
#endif
	}



	/*
		one element tree manage 32^max_level element.
		for example, max_level==2 then element count will be 1024.
		If the size is not enough, a tree is added.
	
		一つの管理木が 32^max_level の要素を管理できる.
		例えば max_level==2 の場合は 1024要素.
		サイズが足りなくなった場合は木が追加される.
	*/
	NglEntityBuffer::NglEntityBuffer(int max_level)
	{
		max_level_ = (0 < max_level) ? max_level : 1;

		max_tree_element_count_ = pow(BitElementManageSize(), max_level_);

		// 階層管理のみ確保して個々の要素は確保しない. Newで必要に応じて生成される.
		status_bit_hierarchy_ = new TArray<BitElementType>[max_level_];
	}
	NglEntityBuffer::~NglEntityBuffer()
	{
		if (status_bit_hierarchy_)
			delete[] status_bit_hierarchy_;
	}

	// 新規生成. 戻り値は識別ID. 
	NglEntityBuffer::EntityID NglEntityBuffer::New()
	{
		constexpr BitElementType mask_all_1 = FillMask();
		constexpr int sizeof_element = BitElementManageSize();
		// sizeof_elementはビットサイズなので2の冪数のはずなのでMSBでlogの代用. constexprも使える.
		constexpr int sizeof_element_log2 = ngl::math::Msb(uint32_t(sizeof_element));

		int tree_index = -1;
		// 最上位レベルで空きをチェック
		for (int li = 0; li < status_bit_hierarchy_[0].Num(); ++li)
		{
			if (mask_all_1 != (status_bit_hierarchy_[0][li] & mask_all_1))
			{
				tree_index = li;
				break;
			}
		}

		// 空きが無い場合はツリーを増やす
		if (0 > tree_index)
		{
			tree_index = status_bit_hierarchy_[0].Num();

			for (int i = 0; i < max_level_; ++i)
			{
				const int append_num = (1 << (i * sizeof_element_log2));
				status_bit_hierarchy_[i].AddZeroed(append_num);
			}

			// 世代チェック用バッファも追加
			element_generation_buffer_.AddZeroed(max_tree_element_count_);

			// 要素追加のコールバック
			OnAppendTree(max_tree_element_count_);
		}

		int empty_index = 0;
		int element_index = 0;
		for (int i = 0; i < max_level_; ++i)
		{
			const auto tree_offset = tree_index * (1 << (i * sizeof_element_log2));
			element_index = (element_index << sizeof_element_log2) + empty_index;
			// 反転して最も下位の1のビット位置を取得( bitが0の位置が空き )
			empty_index = ngl::math::Lsb(~status_bit_hierarchy_[i][tree_offset + element_index]);
		}

		const auto index_in_tree = empty_index + (element_index << sizeof_element_log2);

		// ビット更新
		{
			// 下位nビット
			constexpr int element_bit_mask = (1 << sizeof_element_log2) - 1;
			int bit_fill = 1 << (index_in_tree & element_bit_mask);

			for (int i = max_level_ - 1; i >= 0; --i)
			{
				const auto tree_offset = tree_index * (1 << (i * sizeof_element_log2));
				// ビット表現バッファのインデックス
				const auto bit_element_index = index_in_tree >> ((max_level_ - i) * sizeof_element_log2);

				// フラグ更新
				status_bit_hierarchy_[i][tree_offset + bit_element_index] |= bit_fill;

				// 空きが無いことを親階層に伝搬する
				const int is_fill = (mask_all_1 == status_bit_hierarchy_[i][tree_offset + bit_element_index]) ? 1 : 0;
				bit_fill = is_fill << (bit_element_index & element_bit_mask);
			}
		}

		const auto buffer_index = index_in_tree + tree_index * (1 << (max_level_ * sizeof_element_log2));

		// 世代チェック用情報と合わせてID生成
		auto new_id = CreateID(buffer_index, element_generation_buffer_[buffer_index]);

		// 有効要素数追跡
		++enable_element_count_;

		return new_id;
	}

	int NglEntityBuffer::Delete(EntityID id)
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
		// 世代が一致するなら破棄できるので世代を進める
		++element_generation_buffer_[buffer_index];

		// 所属ツリーを判定
		const int tree_index = buffer_index >> (max_level_ * sizeof_element_log2);
		// 所属ツリーの最下層でのインデックス
		const int index_in_tree = buffer_index - (tree_index << (max_level_ * sizeof_element_log2));

		// ツリー内インデックスの下位5Bit部分(0-31)を取り出し,それが32bitフラグのうちどのBitに対応するかのビットマスクを計算.
		// 32bit単位の使用ビット管理バッファのうち対応する要素の32bitとandを取ることでこの要素が使われているかチェックできる.
		constexpr int element_bit_mask = (1 << sizeof_element_log2) - 1;
		int bit_fill = 1 << (index_in_tree & element_bit_mask);

		for (int i = max_level_ - 1; i >= 0; --i)
		{
			const auto tree_offset = tree_index * (1 << (i * sizeof_element_log2));
			// ビット表現バッファのインデックス
			const auto bit_element_index = index_in_tree >> ((max_level_ - i) * sizeof_element_log2);

			// フラグ更新
			status_bit_hierarchy_[i][tree_offset + bit_element_index] &= ~bit_fill;

			// 空きがあることを親階層に伝搬する. すべて利用中なら1, 一つでも秋があるなら0.
			const int is_fill = (mask_all_1 != status_bit_hierarchy_[i][tree_offset + bit_element_index]) ? 1 : 0;
			bit_fill = is_fill << (bit_element_index & element_bit_mask);
		}

		// 有効要素数追跡
		--enable_element_count_;

		return buffer_index;
	}

	// インデックスが有効か.
	//	インデックス要素が使用中ならtrue.
	bool NglEntityBuffer::IsValid(EntityID id) const
	{
		return 0 <= GetBufferIndex(id);
	}
	// 有効な要素の最初のインデックスを返す
	// TODO. 最下層ビットのチェックをしているが, not_emptyの階層ビットも生成するようにして階層チェックすることで更に効率化できそう.
	int NglEntityBuffer::GetFirstEnableIndex() const
	{
		const auto bottom_lebel = max_level_ - 1;
		auto i = 0;
		// 先頭から有効要素のある位置を検索.
		for (; (i < status_bit_hierarchy_[bottom_lebel].Num()) && (0 == status_bit_hierarchy_[bottom_lebel][i]); ++i) {}
		// 使用中がない場合は負数
		if (status_bit_hierarchy_[bottom_lebel].Num() <= i)
			return -1;

		// 最下位の有効ビット位置
		const auto element_index = ngl::math::Lsb(status_bit_hierarchy_[bottom_lebel][i]);
		return (0 <= element_index) ? (element_index + i * BitElementManageSize()) : 0;
	}
	// 有効な要素の最後のインデックスを返す
	// TODO. 最下層ビットのチェックをしているが, not_emptyの階層ビットも生成するようにして階層チェックすることで更に効率化できそう.
	int NglEntityBuffer::GetLastEnableIndex() const
	{
		const auto bottom_lebel = max_level_ - 1;
		auto i = status_bit_hierarchy_[bottom_lebel].Num() - 1;
		// 末尾から有効要素のある位置を検索.
		for (; (i >= 0) && (0 == status_bit_hierarchy_[bottom_lebel][i]); --i){}
		// 使用中がない場合は負数
		if (0 > i)
			return -1;
		// 最上位の有効ビット位置
		const auto element_index = ngl::math::Msb(status_bit_hierarchy_[bottom_lebel][i]);
		return (0<= element_index)? (element_index + i* BitElementManageSize()) : 0;
	}

	// 全削除
	void NglEntityBuffer::DeleteAll()
	{
		// 全てのフラグを削除
		for (int i = 0; i < max_level_; ++i)
		{
			memset(status_bit_hierarchy_[i].GetData(), 0x00, status_bit_hierarchy_[i].GetAllocatedSize());
		}

		// 世代チェック用バッファも全て1進める
		const auto element_generation_buffer_num = element_generation_buffer_.Num();
		for (int i = 0; i < element_generation_buffer_num; ++i)
			++element_generation_buffer_[i];

		// 有効要素数追跡
		enable_element_count_ = 0;
	}

	// 要素最大数
	// シーケンシャルアクセス用
	int NglEntityBuffer::NumMaxElement() const
	{
		return element_generation_buffer_.Num();
	}

	// 有効要素の個数
	int NglEntityBuffer::NumEnableElement() const
	{
		return enable_element_count_;
	}

	// 要素インデックスからID取得. 無効な要素の場合は InvalidID() を返す.
	// get ID from element index. return InvalidID() if element is invalid.
	NglEntityBuffer::EntityID NglEntityBuffer::GetElementEntityID(int index) const
	{
		if(!IsValidIndex(index))
			return InvalidID();

		return CreateID(index, element_generation_buffer_[index]);
	}
	// インデックス指定した要素が有効か.
	bool NglEntityBuffer::IsValidIndex(int index) const
	{
		constexpr BitElementType mask_all_1 = FillMask();
		constexpr int sizeof_element = BitElementManageSize();
		// sizeof_elementはビットサイズなので2の冪数のはずなのでMSBでlogの代用. constexprも使える.
		constexpr int sizeof_element_log2 = ngl::math::Msb(uint32_t(sizeof_element));

		auto bit_element_index = (index >> sizeof_element_log2);

		// 使用ビットまでチェックする
		constexpr int element_bit_mask = (1 << sizeof_element_log2) - 1;
		int bit = 1 << (index & element_bit_mask);

		return (status_bit_hierarchy_[(max_level_ - 1)][bit_element_index] & bit);
	}

	constexpr NglEntityBuffer::EntityID NglEntityBuffer::CreateID(int buffer_index, uint8_t generation) const
	{
		// 下位24bitにバッファインデックス, 上位8bitに世代を埋め込み
		constexpr EntityID index_mask = ((1 << 24) - 1);
		constexpr EntityID generation_mask = ((1 << 8) - 1);

		EntityID id = (buffer_index & index_mask) | ((generation & generation_mask) << 24);
		return id;
	}

	void NglEntityBuffer::DecodeID(EntityID id, int& out_buffer_index, uint8_t& out_generation) const
	{
		constexpr EntityID index_mask = ((1 << 24) - 1);
		constexpr EntityID generation_mask = ((1 << 8) - 1);
		out_buffer_index = id & index_mask;
		out_generation = (id >> 24) & generation_mask;
	}





	PhysicsParticleSystem::PhysicsParticleSystem()
	{
		// 適当なセットアップ

#if 1
		const auto debug_group_pos_offset = FVector(600.0f, 0.0f, 0.0f);
		auto debug_group_pos_base = FVector(-4000.0f, 1000.0f, 2000.0f);


#if 0
		// カゴ
		{
			int branch_root0 = 0;
			{
				TArray<int> p_index;
				p_index.Add(AddParticle(FVector(0, 0, 0) + debug_group_pos_base, 0.0));
				p_index.Add(AddParticle(FVector(200, 0, 0) + debug_group_pos_base, 1.0));
				p_index.Add(AddParticle(FVector(400, 0, 0) + debug_group_pos_base, 0.0));
				for (int i = 0; i < p_index.Num(); ++i)
				{
					auto pi = p_index[i];
					ex_force_[pi] = FVector(0.0f, 0.0f, 0);
				}
				for (int i = 0; i < p_index.Num() - 1; ++i)
				{
					auto pi = p_index[i];
					auto pi_next = p_index[i + 1];
					{
						TPair<int, int> pair(pi, pi_next);
						TPair<TPair<int, int>, float> distance_c(pair, (position_[pi] - position_[pi_next]).Size());
						distance_constraint_.Add(distance_c);
					}
				}
				branch_root0 = p_index[1];
			}
			{
				auto cos0 = FMath::Cos(FMath::DegreesToRadians(0));
				auto sin0 = FMath::Sin(FMath::DegreesToRadians(0));
				auto cos120 = FMath::Cos(FMath::DegreesToRadians(120));
				auto sin120 = FMath::Sin(FMath::DegreesToRadians(120));
				auto cos240 = FMath::Cos(FMath::DegreesToRadians(240));
				auto sin240 = FMath::Sin(FMath::DegreesToRadians(240));

				FVector basket_p0(500.0f*cos0, 500.0f*sin0, -500.0f);
				FVector basket_p1(500.0f*cos120, 500.0f*sin120, -500.0f);
				FVector basket_p2(500.0f*cos240, 500.0f*sin240, -500.0f);

				basket_p0 = basket_p0.RotateAngleAxis(90.0f, FVector(1, 1, 0));
				basket_p1 = basket_p1.RotateAngleAxis(90.0f, FVector(1, 1, 0));
				basket_p2 = basket_p2.RotateAngleAxis(90.0f, FVector(1, 1, 0));


				TArray<int> p_index;
				p_index.Add(AddParticle(basket_p0 + position_[branch_root0], 1.0));
				p_index.Add(AddParticle(basket_p1 + position_[branch_root0], 1.0));
				p_index.Add(AddParticle(basket_p2 + position_[branch_root0], 1.0));
				for (int i = 0; i < p_index.Num(); ++i)
				{
					auto pi = p_index[i];
					ex_force_[pi] = FVector(0.0f, 0.0f, 0);
				}

				for (int i = 0; i < p_index.Num(); ++i)
				{
					auto i_next = (i + 1) % p_index.Num();

					TPair<int, int> pair(p_index[i], p_index[i_next]);
					TPair<TPair<int, int>, float> distance_c(pair, (position_[pair.Key] - position_[pair.Value]).Size());
					distance_constraint_.Add(distance_c);
				}

				for (int i = 0; i < p_index.Num(); ++i)
				{
					TPair<int, int> pair(p_index[i], branch_root0);
					TPair<TPair<int, int>, float> distance_c(pair, (position_[pair.Key] - position_[pair.Value]).Size());
					distance_constraint_.Add(distance_c);
				}
			}
			debug_group_pos_base += debug_group_pos_offset;
		}
#endif
#if 0
		// 縄梯子
		{
			const int k_ladder_count = 30;
			{
				float ladder_offset = 0.0f;

				auto ladder_dir = FVector(0, 1, 0);
				ladder_dir.Normalize();

				TArray<int> p_index;
				p_index.Add(AddParticle(FVector(-200, 0, 0) + ladder_dir * ladder_offset + debug_group_pos_base, 0.0));
				p_index.Add(AddParticle(FVector(200, 0, 0) + ladder_dir * ladder_offset + debug_group_pos_base, 0.0));
				ladder_offset -= 200;

				for (int li = 0; li < k_ladder_count; ++li)
				{
					p_index.Add(AddParticle(FVector(-200, 0, 0) + ladder_dir * ladder_offset + debug_group_pos_base, 1.0));
					p_index.Add(AddParticle(FVector(200, 0, 0) + ladder_dir * ladder_offset + debug_group_pos_base, 1.0));
					ladder_offset -= 200;
				}

				for (int i = 0; i < p_index.Num(); ++i)
				{
					auto pi = p_index[i];
					ex_force_[pi] = FVector(0.0f, 0.0f, 0);
				}

				// 縦方向につなげる
				for (int i = 0; i < p_index.Num() - 2; i = i + 1)
				{
					auto pi = p_index[i];
					auto pi_next = p_index[i + 2];
					{
						TPair<int, int> pair(pi, pi_next);
						TPair<TPair<int, int>, float> distance_c(pair, (position_[pi] - position_[pi_next]).Size());
						distance_constraint_.Add(distance_c);
					}
				}
				// 横方向につなげる
				for (int i = 0; i < p_index.Num() - 1; i = i + 2)
				{
					auto pi = p_index[i];
					auto pi_next = p_index[i + 1];
					{
						TPair<int, int> pair(pi, pi_next);
						TPair<TPair<int, int>, float> distance_c(pair, (position_[pi] - position_[pi_next]).Size());
						distance_constraint_.Add(distance_c);
					}
				}
			}
			debug_group_pos_base += debug_group_pos_offset;
		}
#endif
#else
#endif
	}
	PhysicsParticleSystem::ParticleID PhysicsParticleSystem::AddParticle(const FVector& pos, float mass)
	{
		const float k_epsilon = 1e-6f;

		// エンティティ管理下のバージョン
		auto newID = particles_.New();
		auto buffer_index = particles_.GetBufferIndex(newID);

		particles_.position_[buffer_index] = (pos);
		particles_.position_prev_[buffer_index] = (pos);
		particles_.inv_mass_[buffer_index] = (k_epsilon < mass) ? 1.0f / mass : 0.0f;
		particles_.velocity_[buffer_index] = (FVector::ZeroVector);
		particles_.ex_force_[buffer_index] = (FVector::ZeroVector);
		particles_.constraint_force_[buffer_index] = (FVector::ZeroVector);

		return newID;
	}
	// パーティクル削除
	void PhysicsParticleSystem::RemoveParticle(ParticleID id)
	{
		int buffer_index = particles_.Delete(id);
	}

	PhysicsParticleSystem::DistansConstID PhysicsParticleSystem::AddDistanceConstraint(ParticleID p0, ParticleID p1, float distance)
	{
		auto newID = distnce_const_.New();
		auto buffer_index = distnce_const_.GetBufferIndex(newID);

		if (0.0f > distance)
		{
			// 距離が負の場合は現在のパーティクル間の距離を計算
			auto p0_i = particles_.GetBufferIndex(p0);
			auto p1_i = particles_.GetBufferIndex(p1);
			if (0 <= p0_i && 0 <= p1_i)
			{
				distance = (particles_.position_[p0_i] - particles_.position_[p1_i]).Size();
			}
		}

		distnce_const_.constraint_[buffer_index] = DistanceConstraintBuffer::Constraint(DistanceConstraintBuffer::ConstraintPair(p0, p1), distance);

		// warm start値は0リセット. 毎フレームクリアしてそのフレームで有効な拘束を追加する場合は何らかの形でキャッシュしておいて検索する必要がある.
		distnce_const_.prev_lambda_[buffer_index] = 0.0f;
		distnce_const_.prev_lambda_grad_[buffer_index] = 0.0f;

		return newID;
	}
	void PhysicsParticleSystem::RemoveDistanceConstraint(DistansConstID id)
	{
		distnce_const_.Delete(id);
	}

	void PhysicsParticleSystem::SolveConstraint(float delta_sec, int iteration_count, float vioration_penarty_bias_rate, float momentum_rate,
		float warm_start_rate, float grad_warm_start_rate)
	{
		const float k_epsilon = 1e-6f;
		const float k_vioration_penarty_bias_rate = vioration_penarty_bias_rate;
		const int k_jacobi_iteration_count = iteration_count;

		// 検証用の外力
		const FVector g_gravity(0, 0, -980);


		{
			// 外力設定
			for (int i = 0; i < particles_.NumMaxElement(); ++i)
			{
				const auto id = particles_.GetElementEntityID(i);
				if (ParticleBuffer::InvalidID() != id)
				{
					for (int j = 0; j < external_force_sphere_shape_.Num(); ++j)
					{
						auto sphere = external_force_sphere_shape_[j];
						auto intensity = external_force_sphere_intensity_[j];


						const auto distance_vec = particles_.position_[i] - sphere.Center;
						float distance;
						FVector dir;
						distance_vec.ToDirectionAndLength(dir, distance);
						if (sphere.W > distance)
						{
							auto intensity_rate = FMath::Pow(1.0f - (distance / sphere.W), 2);

							particles_.ex_force_[i] += dir * intensity * intensity_rate;
						}
					}
				}
			}
		}



		// 拘束を構成するパーティクルインデックスをキャッシュ
		TArray<TPair<int, int>> constraint_particles;
		TArray<float> constraint_value;
		TArray<float> constraint_position_violation;
		// 拘束のJacobian
		TArray<TPair<FVector, FVector>> jacobian;
		// 方程式のb項
		TArray<float> le_b;
		// Projected-Gauss-Seidelのクランプ値
		TArray<TPair<float, float>> constraint_lambda_clamp;
		TArray<float> constraint_lambda[2];
		TArray<float> constraint_lambda_grad;
		// 計算したlambdaをwarmstartingのために書き戻すアドレスを保持
		TArray<float*> constraint_prev_lambda_ptr;
		TArray<float*> constraint_prev_lambda_grad_ptr;

		int enable_element_count = 0;
		for (int i = 0; i < distnce_const_.NumMaxElement(); ++i)
		{
			TPair<int, int> particle_indices;
			TPair<FVector, FVector> constraint_jacobian;
			float constraint_v;
			float violation;
			TPair<float, float> lambda_min_max;
			float* prev_lambda_ptr = nullptr;
			float* prev_lambda_grad_ptr = nullptr;

			// 計算した拘束条件がバッファ内の拘束条件数を超えたら早期break.
			if (distnce_const_.NumEnableElement() <= enable_element_count)
				break;
			// 拘束条件と付随する情報を計算
			auto is_enable_constriant_element = distnce_const_.CalcConstraint(particles_, i, particle_indices, constraint_jacobian, constraint_v, violation, lambda_min_max,
				prev_lambda_ptr, prev_lambda_grad_ptr);
			if (is_enable_constriant_element)
			{
				++enable_element_count;

				const float inv_mass0 = particles_.inv_mass_[particle_indices.Key];
				const float inv_mass1 = particles_.inv_mass_[particle_indices.Value];
				const FVector inv_mass_vec0(inv_mass0, inv_mass0, inv_mass0);
				const FVector inv_mass_vec1(inv_mass1, inv_mass1, inv_mass1);

				// M^-1 * f_ex
				auto inv_mass_f0 = ((particles_.ex_force_[particle_indices.Key] + g_gravity) * inv_mass_vec0);
				auto inv_mass_f1 = ((particles_.ex_force_[particle_indices.Value] + g_gravity) * inv_mass_vec1);

				// v_pred / dt
				auto div_dt_v0 = particles_.velocity_[particle_indices.Key] / delta_sec;
				auto div_dt_v1 = particles_.velocity_[particle_indices.Value] / delta_sec;

				// add
				auto imp0 = div_dt_v0 + inv_mass_f0;
				auto imp1 = div_dt_v1 + inv_mass_f1;

				// b := J * (v_pred / dt + M^-1 * f_ex)
				auto tmp_le_b = (FVector::DotProduct(constraint_jacobian.Key, imp0) + FVector::DotProduct(constraint_jacobian.Value, imp1));

				// BAUMGARTEの安定化法
				float bias_v = violation / delta_sec;
				// バイアスを加算
				tmp_le_b += k_vioration_penarty_bias_rate * (bias_v) / delta_sec;


				// 反復計算で近傍ペアアクセスをするのでキャッシュしておく
				constraint_particles.Add(TPair<int, int>(particle_indices.Key, particle_indices.Value));
				// 拘束条件の値も一応保持
				constraint_value.Add(constraint_v);
				// 拘束条件違反量
				constraint_position_violation.Add(violation);
				// パーティクルペアJacobian
				jacobian.Add(constraint_jacobian);
				// Ax=b のb項
				le_b.Add(tmp_le_b);
				// 拘束力のクランプ範囲
				constraint_lambda_clamp.Add(lambda_min_max);
				// 拘束の前回lambdaの参照と今回の値の書き戻し先アドレス
				constraint_prev_lambda_ptr.Add(prev_lambda_ptr);
				constraint_prev_lambda_grad_ptr.Add(prev_lambda_grad_ptr);

				// 計算結果の格納用
				constraint_lambda[0].Add((prev_lambda_ptr) ? (*prev_lambda_ptr)*warm_start_rate : 0.0f); // lambda初期値にwarm start 適用
				constraint_lambda[1].Add(0.0f);
				constraint_lambda_grad.Add((prev_lambda_grad_ptr) ? (*prev_lambda_grad_ptr)*grad_warm_start_rate : 0.0f);// gradのwarm start

			}
		}

		bool is_first_iteration = true;
		int lambda_flip = 0;
		for (int itr = 0; itr < k_jacobi_iteration_count; ++itr)
		{
			for (int i = 0; i < constraint_particles.Num(); ++i)
			{
				int pair0 = constraint_particles[i].Key;
				int pair1 = constraint_particles[i].Value;

				const float inv_mass0 = particles_.inv_mass_[pair0];
				const float inv_mass1 = particles_.inv_mass_[pair1];
				const FVector inv_mass_vec0(inv_mass0, inv_mass0, inv_mass0);
				const FVector inv_mass_vec1(inv_mass1, inv_mass1, inv_mass1);

				// TODO J^T M J
				float n_a_i_j = 0.0f;
				for (int j = 0; j < constraint_particles.Num(); ++j)
				{
					// 評価対象の拘束以外の有効な制約
					if (i == j)
						continue;

					int n_pair0 = constraint_particles[j].Key;
					int n_pair1 = constraint_particles[j].Value;

					// 拘束条件iに影響する拘束条件jについて  J^T M J の ij要素 を参照する
					// Jは拘束を構成する要素以外はゼロなので拘束要素が含まれる別の拘束を検索して参照すれば巨大な行列を保持しなくてすむ
					float a_i_j = 0.0f;
					if (n_pair0 == pair0 || n_pair0 == pair1 || n_pair1 == pair0 || n_pair1 == pair1)
					{
						const float inv_j_mass0 = particles_.inv_mass_[n_pair0];
						const float inv_j_mass1 = particles_.inv_mass_[n_pair1];
						const FVector inv_j_mass_vec0(inv_j_mass0, inv_j_mass0, inv_j_mass0);
						const FVector inv_j_mass_vec1(inv_j_mass1, inv_j_mass1, inv_j_mass1);

						auto mj0 = jacobian[j].Key * inv_j_mass_vec0;
						auto mj1 = jacobian[j].Value * inv_j_mass_vec1;
						// 拘束iの要素と同じ要素を持つ拘束jを探してその要素に対応する質量ベクトルにi側の要素のJacobianをかけ合わせる
						if (n_pair0 == pair0)
						{
							a_i_j += FVector::DotProduct(jacobian[i].Key, mj0);
						}
						else if (n_pair0 == pair1)
						{
							a_i_j += FVector::DotProduct(jacobian[i].Value, mj0);
						}
						if (n_pair1 == pair0)
						{
							a_i_j += FVector::DotProduct(jacobian[i].Key, mj1);
						}
						else if (n_pair1 == pair1)
						{
							a_i_j += FVector::DotProduct(jacobian[i].Value, mj1);
						}
					}
					// 拘束iのJacobianを掛けた質量ベクトルにjの拘束力を掛け合わせる
					n_a_i_j += a_i_j * constraint_lambda[lambda_flip][j];
				}

				// Jacobi反復法で更新する
				float a_i_i = FVector::DotProduct(jacobian[i].Key, jacobian[i].Key * inv_mass_vec0) + FVector::DotProduct(jacobian[i].Value, jacobian[i].Value * inv_mass_vec1);
				auto next_lambda = (-le_b[i] - n_a_i_j) / a_i_i;


				// 反復Lambdaの更新
				constraint_lambda[1 - lambda_flip][i] = momentum_rate * constraint_lambda_grad[i] + next_lambda;
				{
					// クランプをここで
					constraint_lambda[1 - lambda_flip][i] = FMath::Clamp(constraint_lambda[1 - lambda_flip][i], constraint_lambda_clamp[i].Key, constraint_lambda_clamp[i].Value);
				}
				// Lambda勾配の更新. Lambda勾配もクランプしたほうが良い?
				constraint_lambda_grad[i] = constraint_lambda[1 - lambda_flip][i] - constraint_lambda[lambda_flip][i];
			}
			lambda_flip = 1 - lambda_flip;
			is_first_iteration = false;
		}



		// 拘束力リセット
		for (int i = 0; i < particles_.NumMaxElement(); ++i)
		{
			const auto id = particles_.GetElementEntityID(i);
			if (ParticleBuffer::InvalidID() != id)
			{
				particles_.constraint_force_[i] = FVector::ZeroVector;
			}
		}
		// パーティクル毎の拘束力合計
		for (int i = 0; i < constraint_particles.Num(); ++i)
		{
			const auto pair0 = constraint_particles[i].Key;
			const auto pair1 = constraint_particles[i].Value;

			auto cf0 = constraint_lambda[lambda_flip][i] * jacobian[i].Key;
			auto cf1 = constraint_lambda[lambda_flip][i] * jacobian[i].Value;

			// 拘束力の合計
			particles_.constraint_force_[pair0] += cf0;
			particles_.constraint_force_[pair1] += cf1;


			// 今回計算したlambdaを書き戻す
			if (constraint_prev_lambda_ptr[i])
				*constraint_prev_lambda_ptr[i] = constraint_lambda[lambda_flip][i];
			if (constraint_prev_lambda_grad_ptr[i])
				*constraint_prev_lambda_grad_ptr[i] = constraint_lambda_grad[i];
		}


		// 拘束力が求まったので外力と合わせてIntegrate
		for (int i = 0; i < particles_.NumMaxElement(); ++i)
		{
			const auto id = particles_.GetElementEntityID(i);
			if (ParticleBuffer::InvalidID() != id)
			{
				if (k_epsilon >= particles_.inv_mass_[i])
					continue;

				// vertlet法
				particles_.position_[i] += particles_.velocity_[i] * delta_sec;
				particles_.position_[i] += (((particles_.ex_force_[i] + g_gravity) + particles_.constraint_force_[i]) * particles_.inv_mass_[i]) * delta_sec * delta_sec;

				// 速度更新
				auto v = particles_.position_[i] - particles_.position_prev_[i];
				particles_.velocity_[i] = v / delta_sec;
				particles_.position_prev_[i] = particles_.position_[i];


				// 外力リセット
				particles_.ex_force_[i] = FVector::ZeroVector;
			}
		}


		// 外力球リセット
		external_force_sphere_shape_.Empty();
		external_force_sphere_intensity_.Empty();
	}
}