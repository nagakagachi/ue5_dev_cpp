#pragma once

#include "CoreMinimal.h"

#include <tuple>

#include "math_util.h"

namespace ngl
{
	/*
		固定サイズ要素を高速で割当/解除するためのクラス.
		管理ビットサイズのn分木で高速に空き要素を検索して割り当てる.
	
		下位n要素の使用状況をbit管理し,使用状態に変更/使用状態を解除したらそれを上位階層に伝搬することでbit演算で空き領域検索が可能.
	*/
	class EntityAllocator
	{
	public:
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
		EntityAllocator(int max_level = 2);
		virtual ~EntityAllocator();

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
			constexpr int sizeof_element_log2 = math::Msb(uint32_t(sizeof_element));


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
		TArray<BitElementType>*	status_bit_hierarchy_;// 階層ビットフラグで高速に空き要素検索をするためのバッファ.

		// 要素の生成と削除で以前のIDを無効と判定するための世代数管理.8bitなので255回New/Deleteしたあとに最初のIDを問い合わせると有効判定されてしまうが運用で回避したい.
		TArray<uint8_t>		element_generation_buffer_;

		// バッファ内で有効な要素の数. シーケンシャルアクセス時に早期スキップするため.
		int					enable_element_count_ = 0;

		// 最上位の管理単位で管理可能な要素数. ビット管理サイズ^最大レベル で計算される.
		int					max_tree_element_count_ = 0;
	};


	
	//	効率的なエンティティの追加と削除、ID管理をサポートするクラス.
	//		追加でエンティティのIDを生成し、内部でそのIDとデータを紐付ける
	//		ID指定で削除(削除情報の登録だけで実際の削除は次のPrepareForNextFrame実行まで遅延される)
	//		ID指定でデータインデックスの取得(内部で二分探索するので何度も呼ばないようにする)
	//		PrepareForNextFrame()を定期的(1フレームに1度)実行することでID削除の反映やID管理情報の整理などをする
	class EntityIdManager
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
		EntityIdManager();
		virtual ~EntityIdManager();

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

	// Entityの具体的なデータ保持実装.
	// BUFFER_CLASS_TYPEはAdd()メソッドを実装して内部データ要素数を一つ増やす機能をもつ必要がある.
	template<typename BUFFER_CLASS_TYPE>
	class EntityIdManagerT : public EntityIdManager
	{
	public:

	public:
		EntityIdManagerT()
		{
		}
		~EntityIdManagerT()
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
}