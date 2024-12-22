
#include "entity_buffer.h"

#include <assert.h>


namespace ngl
{
	EntityIdManager::EntityIdManager()
	{
	}
	EntityIdManager::~EntityIdManager()
	{
	}

	// エンティティ作成
	EntityIdManager::EntityID	EntityIdManager::NewEntity()
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
	void		EntityIdManager::RemoveEntity(const EntityID& id)
	{
		remove_id_info_array_.Add(TPair<EntityID, int>(id, -1));
	}
	// エンティティ全て削除
	void		EntityIdManager::RemoveEntityAll()
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
	int			EntityIdManager::GetEntityDataIndex(const EntityID& id) const
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
	EntityIdManager::EntityID_Index_Pair	EntityIdManager::GetEntityInfo(int index) const
	{
		if (NumEnableEntity() <= index)
			return EntityID_Index_Pair(InvalidID(), -1);
		return id_to_data_index_array_[index];
	}

	// 有効なエンティティ数
	int			EntityIdManager::NumEnableEntity() const
	{ 
		return enable_entity_id_count_;
	}

	// フレームに一度実行する必要がある.
	// エンティティ削除の反映やエンティティIDバッファの前詰などをする.
	void		EntityIdManager::PrepareForNextFrame()
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

	int EntityIdManager::BinarySearchEntityDataIndex(const EntityID_Index_Array& id_index_array, int start_index, int search_range, EntityID search_id) const
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
	EntityAllocator::EntityAllocator(int max_level)
	{
		max_level_ = (0 < max_level) ? max_level : 1;

		max_tree_element_count_ = pow(BitElementManageSize(), max_level_);

		// 階層管理のみ確保して個々の要素は確保しない. Newで必要に応じて生成される.
		status_bit_hierarchy_ = new TArray<BitElementType>[max_level_];
	}
	EntityAllocator::~EntityAllocator()
	{
		if (status_bit_hierarchy_)
			delete[] status_bit_hierarchy_;
	}

	// 新規生成. 戻り値は識別ID. 
	EntityAllocator::EntityID EntityAllocator::New()
	{
		constexpr BitElementType mask_all_1 = FillMask();
		constexpr int sizeof_element = BitElementManageSize();
		// sizeof_elementはビットサイズなので2の冪数のはずなのでMSBでlogの代用. constexprも使える.
		constexpr int sizeof_element_log2 = math::Msb(uint32_t(sizeof_element));

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
			empty_index = math::Lsb(~status_bit_hierarchy_[i][tree_offset + element_index]);
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

	int EntityAllocator::Delete(EntityID id)
	{
		constexpr BitElementType mask_all_1 = FillMask();
		constexpr int sizeof_element = BitElementManageSize();
		// sizeof_elementはビットサイズなので2の冪数のはずなのでMSBでlogの代用. constexprも使える.
		constexpr int sizeof_element_log2 = math::Msb(uint32_t(sizeof_element));

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
	bool EntityAllocator::IsValid(EntityID id) const
	{
		return 0 <= GetBufferIndex(id);
	}
	// 有効な要素の最初のインデックスを返す
	// TODO. 最下層ビットのチェックをしているが, not_emptyの階層ビットも生成するようにして階層チェックすることで更に効率化できそう.
	int EntityAllocator::GetFirstEnableIndex() const
	{
		const auto bottom_lebel = max_level_ - 1;
		auto i = 0;
		// 先頭から有効要素のある位置を検索.
		for (; (i < status_bit_hierarchy_[bottom_lebel].Num()) && (0 == status_bit_hierarchy_[bottom_lebel][i]); ++i) {}
		// 使用中がない場合は負数
		if (status_bit_hierarchy_[bottom_lebel].Num() <= i)
			return -1;

		// 最下位の有効ビット位置
		const auto element_index = math::Lsb(status_bit_hierarchy_[bottom_lebel][i]);
		return (0 <= element_index) ? (element_index + i * BitElementManageSize()) : 0;
	}
	// 有効な要素の最後のインデックスを返す
	// TODO. 最下層ビットのチェックをしているが, not_emptyの階層ビットも生成するようにして階層チェックすることで更に効率化できそう.
	int EntityAllocator::GetLastEnableIndex() const
	{
		const auto bottom_lebel = max_level_ - 1;
		auto i = status_bit_hierarchy_[bottom_lebel].Num() - 1;
		// 末尾から有効要素のある位置を検索.
		for (; (i >= 0) && (0 == status_bit_hierarchy_[bottom_lebel][i]); --i){}
		// 使用中がない場合は負数
		if (0 > i)
			return -1;
		// 最上位の有効ビット位置
		const auto element_index = math::Msb(status_bit_hierarchy_[bottom_lebel][i]);
		return (0<= element_index)? (element_index + i* BitElementManageSize()) : 0;
	}

	// 全削除
	void EntityAllocator::DeleteAll()
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
	int EntityAllocator::NumMaxElement() const
	{
		return element_generation_buffer_.Num();
	}

	// 有効要素の個数
	int EntityAllocator::NumEnableElement() const
	{
		return enable_element_count_;
	}

	// 要素インデックスからID取得. 無効な要素の場合は InvalidID() を返す.
	// get ID from element index. return InvalidID() if element is invalid.
	EntityAllocator::EntityID EntityAllocator::GetElementEntityID(int index) const
	{
		if(!IsValidIndex(index))
			return InvalidID();

		return CreateID(index, element_generation_buffer_[index]);
	}
	// インデックス指定した要素が有効か.
	bool EntityAllocator::IsValidIndex(int index) const
	{
		constexpr BitElementType mask_all_1 = FillMask();
		constexpr int sizeof_element = BitElementManageSize();
		// sizeof_elementはビットサイズなので2の冪数のはずなのでMSBでlogの代用. constexprも使える.
		constexpr int sizeof_element_log2 = math::Msb(uint32_t(sizeof_element));

		auto bit_element_index = (index >> sizeof_element_log2);

		// 使用ビットまでチェックする
		constexpr int element_bit_mask = (1 << sizeof_element_log2) - 1;
		int bit = 1 << (index & element_bit_mask);

		return (status_bit_hierarchy_[(max_level_ - 1)][bit_element_index] & bit);
	}

	constexpr EntityAllocator::EntityID EntityAllocator::CreateID(int buffer_index, uint8_t generation) const
	{
		// 下位24bitにバッファインデックス, 上位8bitに世代を埋め込み
		constexpr EntityID index_mask = ((1 << 24) - 1);
		constexpr EntityID generation_mask = ((1 << 8) - 1);

		EntityID id = (buffer_index & index_mask) | ((generation & generation_mask) << 24);
		return id;
	}

	void EntityAllocator::DecodeID(EntityID id, int& out_buffer_index, uint8_t& out_generation) const
	{
		constexpr EntityID index_mask = ((1 << 24) - 1);
		constexpr EntityID generation_mask = ((1 << 8) - 1);
		out_buffer_index = id & index_mask;
		out_generation = (id >> 24) & generation_mask;
	}
	
}