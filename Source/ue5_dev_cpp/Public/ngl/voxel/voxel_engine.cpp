// Fill out your copyright notice in the Description page of Project Settings.


#include "voxel_engine.h"
#include "voxel_noise.h"


#include "Materials/Material.h"
#include "Components/SphereComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

#include "ProceduralMeshComponent.h"

#include "Engine.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

#include "Runtime/Core/Public/Async/ParallelFor.h"

#if WITH_EDITOR
// For Editor
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

class ANglVoxelEngine;

namespace ngl
{
	NglVoxelEngineAsyncTask::NglVoxelEngineAsyncTask()
	{
	}
	NglVoxelEngineAsyncTask::~NglVoxelEngineAsyncTask()
	{
		Finalize();
	}

	bool NglVoxelEngineAsyncTask::Initialize(ANglVoxelEngine* owner)
	{

		// 念の為Async完了待ち
		WaitAsyncUpdate();
	
		if (!owner)
		{
			UE_LOG(LogTemp, Fatal, TEXT("[NglVoxelEngineAsyncTask] Invalid Owner Ptr."));
			return false;
		}

		// TODO
		owner_ = owner;
	
		return true;
	}
	void NglVoxelEngineAsyncTask::Finalize()
	{
		// Async完了待ち
		WaitAsyncUpdate();

		// TODO
		owner_ = nullptr;
	}

	// 非同期実行関数
	void NglVoxelEngineAsyncTask::AsyncUpdate()
	{
		// TODO.
		if (!owner_)
		{
			UE_LOG(LogTemp, Fatal, TEXT("[NglVoxelEngineAsyncTask] Invalid Owner Ptr."));
			return;
		}

		// Async更新関数呼び出し
		owner_->AsyncUpdate();
	}
}

	// ----------------------------------------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------------------------
	ANglVoxelEngine::ANglVoxelEngine()
	{
		// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
		PrimaryActorTick.bCanEverTick = true;

		// 原点固定
		SetActorTransform(FTransform::Identity);

		USphereComponent* rootComp = CreateDefaultSubobject<USphereComponent>("RootCollider");
		RootComponent = Cast<USceneComponent>(rootComp);

		auto engine_mesh = ConstructorHelpers::FObjectFinder<UStaticMesh>(TEXT("Static Mesh'/Engine/BasicShapes/Cube.Cube'"));
		if (engine_mesh.Succeeded())
		{
			voxel_mesh_ = engine_mesh.Object;
		}
	
		// Async初期化
		async_task_.Initialize(this);
	}
	ANglVoxelEngine::~ANglVoxelEngine()
	{
		// 非同期タスク待機.
		async_task_.WaitAsyncUpdate();

		// 破棄.
		FinalizeVoxel();
	}
	// Voxelメモリの解放とComponentの解放
	void ANglVoxelEngine::FinalizeVoxel()
	{
		{
			for (auto&& it : voxel_chunk_map_)
			{
				if (it.Value)
				{
					delete it.Value;
					it.Value = nullptr;
				}
			}
			voxel_chunk_map_.Empty();
		}

		{
			for (auto&& it : chunk_mesh_component_map_)
			{
				// Actorの破棄に伴ってすでに破棄済みの可能性があるためチェック.
				if (!it.Value->IsValidLowLevel())
					continue;
				it.Value->ConditionalBeginDestroy();
			}
			chunk_mesh_component_map_.Empty();
			for (auto&& it : chunk_mesh_component_pool_)
			{
				// Actorの破棄に伴ってすでに破棄済みの可能性があるためチェック.
				if (!it->IsValidLowLevel())
					continue;
				it->ConditionalBeginDestroy();
			}
			chunk_mesh_component_pool_.Empty();



			// ProceduralMesh版 プール化などはあとで
			for (auto&& it : chunk_proc_mesh_component_map_)
			{
				// Actorの破棄に伴ってすでに破棄済みの可能性があるためチェック.
				if (!it.Value->IsValidLowLevel())
					continue;
				it.Value->ConditionalBeginDestroy();
			}
			chunk_proc_mesh_component_map_.Empty();
		}
	}

	void ANglVoxelEngine::OnConstruction(const FTransform& Transform)
	{
		Super::OnConstruction(Transform);

	}
	void ANglVoxelEngine::BeginPlay()
	{
		Super::BeginPlay();

		//important_position_ = FVector::ZeroVector;
		main2AsyncParam_[0] = main2AsyncParam_[1] = {};

		// 破棄
		async_task_.WaitAsyncUpdate();
		FinalizeVoxel();

		/*
		// テスト
		auto p_mesh_cmp = GetChunkMeshComponentFromPool();
		{
			p_mesh_cmp->ClearInstances();

			p_mesh_cmp->AddInstance(FTransform(FVector(0.0f, 0.0f, 500.0f)), true);
		}
		*/

#if WITH_EDITOR
		// TickがEditorModeでも呼ばれる場合はBeginPlayで実際にプレイが始まったときに諸々リセットする必要があるかもしれない
		if (ShouldTickIfViewportsOnly())
		{
			// TODO. EditorModeで処理したもののリセットなど.
		}
#endif
	}
	void ANglVoxelEngine::EndPlay(EEndPlayReason::Type reason)
	{
		Super::EndPlay(reason);


		// 破棄
		async_task_.WaitAsyncUpdate();
		FinalizeVoxel();


#if WITH_EDITOR
		// TickがEditorModeでも呼ばれる場合はBeginPlayで実際にプレイが始まったときに諸々リセットする必要があるかもしれない
		if (ShouldTickIfViewportsOnly())
		{
			// TODO. EditorModeで処理したもののリセットなど.
		}
#endif
	}

	// EditorMode Tick Enable.
	//	EditorモードでのTick呼び出し許可.
	bool ANglVoxelEngine::ShouldTickIfViewportsOnly() const
	{
		//return true; Editorモードでも動作するようになるが,ProceduralMeshの後処理かなにかでPlay開始時に停止時間が発生するため一旦無効化する.
		return false;
	}

	// ShouldTickIfViewportsOnly()が真を返すことでEditorモードでも呼び出されます.
	void ANglVoxelEngine::Tick(float DeltaTime)
	{
		Super::Tick(DeltaTime);

		const auto		tick_start_time = std::chrono::system_clock::now();

		// 強制的に原点
		SetActorTransform(FTransform::Identity);

		// 前回の重視位置保持.
		main2AsyncParam_[0].important_position_prev_ = main2AsyncParam_[0].important_position_;

		// 重視位置の更新(本来は外部からリクエスト方式)
		{
			if (auto* player_actor = Cast<AActor>(UGameplayStatics::GetPlayerPawn(this, 0)))
			{
				// プレイヤーが取得できたらプレイヤー中心.
				main2AsyncParam_[0].important_position_ = player_actor->GetActorLocation();

			}
			else
			{
#if WITH_EDITOR
				// EditorModeではカメラを使う.
				for (FLevelEditorViewportClient* level_viewport_clients : GEditor->GetLevelViewportClients())
				{
					if (level_viewport_clients && level_viewport_clients->IsPerspective())
					{
						auto CameraLocation = level_viewport_clients->GetViewLocation();
						auto CameraRotation = level_viewport_clients->GetViewRotation();

						main2AsyncParam_[0].important_position_ = CameraLocation;

						break;
					}
				}
#endif
			}
		}

		// カレント重視チャンク
		FIntVector important_chunk_position = ngl::math::FVectorFloorToInt(main2AsyncParam_[0].important_position_ / (voxel_size_ * ChunkType::CHUNK_RESOLUTION()));


		// 空にする.
		stream_out_chunk_array_[0].Empty(stream_out_chunk_array_[0].Max());
		stream_in_chunk_array_[0].Empty(stream_in_chunk_array_[0].Max());
		render_dirty_chunk_id_array_.Empty(render_dirty_chunk_id_array_.Max());
		{
			for (auto&& e : voxel_chunk_map_)
			{
				// 確保されてないことは無いはず.
				assert(e.Value);

				// Activeのみ対象(競合対策のためにLoading等は完了するまで待つ)
				if (ngl::NglVoxelChunkState::Active != e.Value->GetState())
					continue;

				// Stream Out チャンクIDリストアップ
				auto chunk_distance = important_chunk_position - e.Key;
				auto need_stream_out = (stream_out_chunk_range < abs(chunk_distance.X)) || (stream_out_chunk_range < abs(chunk_distance.Y)) || (stream_out_chunk_range < abs(chunk_distance.Z));
				if (need_stream_out)
				{
					// stream out するチャンクに追加.
					stream_out_chunk_array_[0].Add(e.Key);
				}
				else
				{
#if 0
					// LOD切り替えチェック
					const auto chunk_distance_len = FVector::Distance(FVector::ZeroVector, FVector(chunk_distance));
					const auto world_distance_len = chunk_distance_len * ChunkType::CHUNK_RESOLUTION() * voxel_size_;

					auto lod_level = static_cast<unsigned int>(world_distance_len / (ChunkType::CHUNK_RESOLUTION() * voxel_size_ * 6));// 適当に距離に線形にLOD
					// 最小LODでクランプ
					lod_level = FMath::Min(e.Value->LOD_MAX_INDEX(), FMath::Max( static_cast<unsigned int>(min_lod_level_), lod_level));

					if (e.Value->GetCurrentLodLevel() != lod_level)
					{
						e.Value->SetCurrentLodLevel(lod_level);

						// 描画パラメータ更新リストに追加.
						// 最大LODをチェックして有効なセルが一つもない場合は描画不要のためスキップする
						if (e.Value->Get(0, 0, 0, e.Value->LOD_MAX_INDEX()))
						{
							render_dirty_chunk_id_array_.Add(e.Key);
						}
					}
#endif
				}
			}

			// Stream In チャンクIDリストアップ
			for (int k = -stream_in_chunk_range_vertical; k <= stream_in_chunk_range_vertical; ++k)
			{
				for (int j = -stream_in_chunk_range_horizontal; j <= stream_in_chunk_range_horizontal; ++j)
				{
					for (int i = -stream_in_chunk_range_horizontal; i <= stream_in_chunk_range_horizontal; ++i)
					{
						auto target_chunk = important_chunk_position + FIntVector(i, j, k);

						auto* find_current_chunk = voxel_chunk_map_.Find(target_chunk);
						if (!find_current_chunk)
						{
							// デコードが必要なチャンクリストアップ
							stream_in_chunk_array_[0].Add(target_chunk);
						}
					}
				}
			}
		}
		auto		sync_elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::duration::zero()).count();

		{
			// 非同期タスクの完了待ち
			async_task_.WaitAsyncUpdate();


			const auto		sync_start_time = std::chrono::system_clock::now();
			// 同期処理
			SyncUpdate();
			sync_elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - sync_start_time).count();

			// 非同期タスクを起動
			async_task_.StartAsyncUpdate(true);
		}

		// Update Render
		UpdateRenderChunk(render_dirty_chunk_id_array_);


		const auto		tick_elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - tick_start_time).count();

		// デバッグ表示
		{
			// チャンクのメモリ使用量チェック
			int chunk_count = 0;
			size_t chunk_memory_byte_size = 0;
			for (auto&& it = voxel_chunk_map_.begin(); it != voxel_chunk_map_.end(); ++it)
			{
				chunk_memory_byte_size += it->Value->GetAllocatedSize();
				++chunk_count;
			}

			int chunk_mesh_component_count = 0;
			for (auto&& it = chunk_mesh_component_map_.begin(); it != chunk_mesh_component_map_.end(); ++it)
			{
				++chunk_mesh_component_count;
			}
			int chunk_mesh_component_pool_count = chunk_mesh_component_pool_.Num();


			int chunk_proc_mesh_component_count = 0;
			for (auto&& it = chunk_proc_mesh_component_map_.begin(); it != chunk_proc_mesh_component_map_.end(); ++it)
			{
				++chunk_proc_mesh_component_count;
			}

			auto display_string =
				FString::Printf(
					TEXT("NglVoxelEngine\n    runtime_chunk:%f[MB]\n	chunk_count:%d\n	mesh_count:%d\n	mesh_pool:%d\n\n	proc_mesh_count:%d\n"),


					(static_cast<float>(chunk_memory_byte_size)/(1024.0f*1024.0f)),
					chunk_count,
					chunk_mesh_component_count,
					chunk_mesh_component_pool_count,
					chunk_proc_mesh_component_count
				);

			auto display_string2 =
				FString::Printf(
					TEXT("\n    tick_time:%d[micro sec]\n	sync_time:%d[micro sec]\n\n	async_time:%d[micro sec]\n"),


					tick_elapsed_time,
					sync_elapsed_time,
					async_micro_sec_
				);

			UKismetSystemLibrary::DrawDebugString(GetWorld(), GetActorLocation(), display_string + display_string2, nullptr, FLinearColor::White, 0);
		}
	}

	// 非同期処理.
	void ANglVoxelEngine::SyncUpdate()
	{
		// voxel_chunk_map_への要素追加や削除は同期中に実行する.

		// Asyncへのパラメータをコピーする
		main2AsyncParam_[1] = main2AsyncParam_[0];


		// Stream In 情報をAsync側へ
		for (auto&& e : stream_in_chunk_array_[0])
		{
			stream_in_chunk_array_[1].Add(e);

			// Mapに追加.
			auto&& new_chunk = voxel_chunk_map_.Add(e);
			if (!new_chunk)
			{
				// 要素確保.
				new_chunk = new ChunkType();
				// IDを設定
				new_chunk->SetId(e);
				// 状態を設定.
				new_chunk->SetState(ngl::NglVoxelChunkState::Empty);
			}
		}
		// Stream Out 情報をAsync側へ
		for (auto&& e : stream_out_chunk_array_[0])
		{
			stream_out_chunk_array_[1].Add(e);
		}


		// Asyncによる stream out完了要素を処理
		for (auto&& e : stream_out_chunk_complete_array_)
		{
			auto&& chunk_ptr = voxel_chunk_map_.Find(e);
			if (chunk_ptr)
			{
				auto&& chunk = *chunk_ptr;
				assert(NglVoxelChunkState::Deletable == chunk->GetState());
				// 要素破棄
				if (chunk)
				{
					delete chunk;
					chunk = nullptr;
				}
				// Mapから除外
				voxel_chunk_map_.Remove(e);


				// 描画オブジェクト処理
				{
					// メッシュコンポーネントをPoolへ返却
					auto&& chunk_mesh = chunk_mesh_component_map_.Find(e);
					if (chunk_mesh)
					{
						// メッシュ描画情報クリア
						(*chunk_mesh)->ClearInstances();
						// Poolへ返却
						RestoreChunkMeshComponentToPool(*chunk_mesh);
					}
					// Meshマップから除去
					chunk_mesh_component_map_.Remove(e);
				}
				// ProcMesh版
				{
					auto&& chunk_proc_mesh = chunk_proc_mesh_component_map_.Find(e);
					if (chunk_proc_mesh)
					{
						(*chunk_proc_mesh)->DestroyComponent();
					}
					chunk_proc_mesh_component_map_.Remove(e);
				}
			}
		}

		// 近傍チャンクからオーバーラップ部をコピーする処理. こちらに統合したい.
		auto func_copy_overlap_from_neighbor = [this](ChunkType* target)
		{
			const auto id = target->GetId();

			// 近傍チャンクとのOverlapDirty
			for (int nk = -1; nk <= 1; ++nk)
			{
				for (int nj = -1; nj <= 1; ++nj)
				{
					for (int ni = -1; ni <= 1; ++ni)
					{
						// 0,0,0は常にfalseになるようにビット操作しているのでチェック不要
						if (target->GetNeighborChunkChangeFlag(ni, nj, nk))
						{
							const auto neightbor_chunk_id = id + FIntVector(ni, nj, nk);

							if (auto&& neightbor_chunk_ptr = voxel_chunk_map_.Find(neightbor_chunk_id))
							{
								const auto neightbor_chunk = *neightbor_chunk_ptr;
								if (ngl::NglVoxelChunkState::Active == neightbor_chunk->GetState())
								{
									if (true)
									{
										// 隣接チャンクの面、エッジ、角からのコピー.

										if (false) {/*dummy*/}
										else if (ni == -1 && nj == 0 && nk == 0) target->CopyOverlapFromSrcEdgeX<false>(neightbor_chunk);
										else if (ni == 1 && nj == 0 && nk == 0) target->CopyOverlapFromSrcEdgeX<true>(neightbor_chunk);
										else if (ni == 0 && nj == -1 && nk == 0) target->CopyOverlapFromSrcEdgeY<false>(neightbor_chunk);
										else if (ni == 0 && nj == 1 && nk == 0) target->CopyOverlapFromSrcEdgeY<true>(neightbor_chunk);
										else if (ni == 0 && nj == 0 && nk == -1) target->CopyOverlapFromSrcEdgeZ<false>(neightbor_chunk);
										else if (ni == 0 && nj == 0 && nk == 1) target->CopyOverlapFromSrcEdgeZ<true>(neightbor_chunk);

										else if (ni == -1 && nj == -1 && nk == 0) target->CopyOverlapFromSrcEdgeXY<false, false>(neightbor_chunk);
										else if (ni == 1 && nj == -1 && nk == 0) target->CopyOverlapFromSrcEdgeXY<true, false>(neightbor_chunk);
										else if (ni == -1 && nj == 1 && nk == 0) target->CopyOverlapFromSrcEdgeXY<false, true>(neightbor_chunk);
										else if (ni == 1 && nj == 1 && nk == 0) target->CopyOverlapFromSrcEdgeXY<true, true>(neightbor_chunk);

										else if (ni == -1 && nj == 0 && nk == -1) target->CopyOverlapFromSrcEdgeXZ<false, false>(neightbor_chunk);
										else if (ni == 1 && nj == 0 && nk == -1) target->CopyOverlapFromSrcEdgeXZ<true, false>(neightbor_chunk);
										else if (ni == -1 && nj == 0 && nk == 1) target->CopyOverlapFromSrcEdgeXZ<false, true>(neightbor_chunk);
										else if (ni == 1 && nj == 0 && nk == 1) target->CopyOverlapFromSrcEdgeXZ<true, true>(neightbor_chunk);

										else if (ni == 0 && nj == -1 && nk == -1) target->CopyOverlapFromSrcEdgeYZ<false, false>(neightbor_chunk);
										else if (ni == 0 && nj == 1 && nk == -1) target->CopyOverlapFromSrcEdgeYZ<true, false>(neightbor_chunk);
										else if (ni == 0 && nj == -1 && nk == 1) target->CopyOverlapFromSrcEdgeYZ<false, true>(neightbor_chunk);
										else if (ni == 0 && nj == 1 && nk == 1) target->CopyOverlapFromSrcEdgeYZ<true, true>(neightbor_chunk);

										else if (ni == -1 && nj == -1 && nk == -1) target->CopyOverlapFromSrcEdgeXYZ<false, false, false>(neightbor_chunk);
										else if (ni == 1 && nj == -1 && nk == -1) target->CopyOverlapFromSrcEdgeXYZ<true, false, false>(neightbor_chunk);
										else if (ni == -1 && nj == 1 && nk == -1) target->CopyOverlapFromSrcEdgeXYZ<false, true, false>(neightbor_chunk);
										else if (ni == 1 && nj == 1 && nk == -1) target->CopyOverlapFromSrcEdgeXYZ<true, true, false>(neightbor_chunk);
										else if (ni == -1 && nj == -1 && nk == 1) target->CopyOverlapFromSrcEdgeXYZ<false, false, true>(neightbor_chunk);
										else if (ni == 1 && nj == -1 && nk == 1) target->CopyOverlapFromSrcEdgeXYZ<true, false, true>(neightbor_chunk);
										else if (ni == -1 && nj == 1 && nk == 1) target->CopyOverlapFromSrcEdgeXYZ<false, true, true>(neightbor_chunk);
										else if (ni == 1 && nj == 1 && nk == 1) target->CopyOverlapFromSrcEdgeXYZ<true, true, true>(neightbor_chunk);
									}
								}

							}
						}
					}
				}
			}
		};


		// Asyncによる stream in完了要素を処理
		TMap<FIntVector, ChunkType*> diry_chunk_map;
		for (auto&& e : stream_in_chunk_complete_array_)
		{
			auto&& chunk_ptr = voxel_chunk_map_.Find(e);
			assert(nullptr != chunk_ptr);
			if (!chunk_ptr)
				continue;

			auto&& chunk = *chunk_ptr;
			assert(nullptr != chunk);
			if (!chunk)
				continue;
			assert(NglVoxelChunkState::Active == chunk->GetState());

			diry_chunk_map.Add(e, chunk);
			chunk->SetAnyVoxelChangeFlag(false);
		}
		// 変更があったチャンクのエッジとオーバーラップしている近傍チャンクのフラグをセットする
		// TODO. 理想は変更のあったチャンクのみループ
		for (auto e : voxel_chunk_map_)
		{
			const auto id = e.Key;
			auto chunk = e.Value;

			// ステートチェック
			if (ngl::NglVoxelChunkState::Active != chunk->GetState())
				continue;

			// ここではVoxelの更新はせず,エッジ部の変更状態から近傍チャンクの近傍更新フラグを変更する.
			{
				if (!chunk->GetAnyEdgeVoxelChangeFlag())
					continue;

				// 近傍チャンクとのOverlapDirty
				for (int nk = -1; nk <= 1; ++nk)
				{
					for (int nj = -1; nj <= 1; ++nj)
					{
						for (int ni = -1; ni <= 1; ++ni)
						{
							// 0,0,0は常にfalseになるようにビット操作しているのでチェック不要
							if (chunk->GetEdgeVoxelChangeFlag(ni, nj, nk))
							{
								const auto neightbor_chunk_id = id + FIntVector(ni, nj, nk);

								if (auto&& neightbor_chunk_ptr = voxel_chunk_map_.Find(neightbor_chunk_id))
								{
									auto neightbor_chunk = *neightbor_chunk_ptr;
									if (ngl::NglVoxelChunkState::Active == neightbor_chunk->GetState())
									{
										// 近傍チャンクの逆方向エッジにDirtyを通知.
										neightbor_chunk->SetNeighborChunkChangeFlag(-ni, -nj, -nk);
									}
								}
							}
						}
					}
				}
			}

			// 近傍チャンクへの通知が完了したのでフラグクリア.
			chunk->ClearEdgeVoxelChangeFlag();
		}

		// TODO. 非効率だが検証のため全チャンクをループ
		// TODO. NeighborChunkChangeFlagが非ゼロのチャンクのみループ
		for (auto e : voxel_chunk_map_)
		{
			const auto id = e.Key;
			auto chunk = e.Value;

			// ステートチェック
			if (ngl::NglVoxelChunkState::Active != chunk->GetState())
				continue;

			if (!chunk->GetAnyNeighborChunkChangeFlag())
				continue;

			const auto nz = chunk->GetNeighborChunkChangeFlag(0, 0, -1);
			const auto pz = chunk->GetNeighborChunkChangeFlag(0, 0, 1);

			func_copy_overlap_from_neighbor(chunk);

			// オーバーラップ部コピーが完了したのでフラグクリア.
			chunk->ClearNeighborChunkChangeFlag();

			// 変更チャンクとして登録
			diry_chunk_map.Add(id, chunk);
		}

		// あらためて描画更新するリストに登録.
		for (auto e : diry_chunk_map)
		{
			// 完了したので描画更新リストに追加.
			render_dirty_chunk_id_array_.Add(e.Key);
		}

		// 次のAsyncのためにクリア
		stream_out_chunk_complete_array_.Empty(stream_out_chunk_complete_array_.Max());
		stream_in_chunk_complete_array_.Empty(stream_in_chunk_complete_array_.Max());
	}
	// 非同期処理.
	void ANglVoxelEngine::AsyncUpdate()
	{
		const float default_chunk_gen_noise_scale = default_chunk_noise_scale_;

		// 非同期処理の継続チェック用マイクロ秒.
		const uint32	async_continue_limit_micro_sec = async_fast_terminate_sec_ * 1000 * 1000;

		const auto		async_start_time = std::chrono::system_clock::now();

		const auto& main2AsyncParam = main2AsyncParam_[1];

		auto&& stream_out_data = stream_out_chunk_array_[1];
		auto&& stream_in_data = stream_in_chunk_array_[1];

		// Stream Out
		{
			// すべてを処理せず次回フレームへ繰り越す対応
			bool is_continue = true;
			for (;is_continue && 0 < stream_out_data.Num();)
			{
				// Pop. TODO. Queueにしたいところ.
				auto chunk_id = stream_out_data.Pop();

				auto&& chunk_ptr = voxel_chunk_map_.Find(chunk_id);

				// StreamIn対象は登録済み且つメモリ確保もされているはず.
				assert(nullptr != chunk_ptr && nullptr != *chunk_ptr);
				if (!chunk_ptr || !*chunk_ptr)
					continue;// 念の為

				auto&& chunk = *chunk_ptr;
				assert(nullptr != *chunk);

				// ステート変更
				chunk->SetState(ngl::NglVoxelChunkState::Unloading);


				// TODO. chunkをディスクに保存


				// ステート変更
				chunk->SetState(ngl::NglVoxelChunkState::Deletable);

				// メモリの解放やMapからの除去はメイン側で実行する.

				// 完了リストに追加
				stream_out_chunk_complete_array_.Add(chunk_id);
			}
		}

		// Stream In
		{
			// フレームレートを落とさないように途中で処理を中断する.
			bool is_continue = true;

			// ParallelForで一度に並列実行する最大数. どうもこの実装ではむしろスパイクが大きくなってしまう
			//	並列数をあげると一部が直列になってしまうのか指定時間内に終わらせる実装との相性が悪くなるので並列数は少なめ(2とか)にしておく
			constexpr unsigned int MAX_PARALLEL = 2;
			ChunkType* parallel_work_target[MAX_PARALLEL] = {};
			for (; is_continue && 0 < stream_in_data.Num();)
			{
				const auto start_time = std::chrono::system_clock::now();


				// 並列処理用データ準備
				unsigned int parallel_count = 0;
				for (unsigned int pi = 0; pi < MAX_PARALLEL && 0 < stream_in_data.Num(); ++pi)
				{
					// Pop. TODO. Queueにしたいところ.
					auto chunk_id = stream_in_data.Pop();

					// 対象を取得
					auto&& chunk_ptr = voxel_chunk_map_.Find(chunk_id);

					// StreamIn対象は登録済み且つメモリ確保もされているはず.
					assert(nullptr != chunk_ptr && nullptr != *chunk_ptr);
					if (!chunk_ptr || !*chunk_ptr)
						continue;// 念の為

					// ステートチェック.
					assert((*chunk_ptr)->GetState() == NglVoxelChunkState::Empty);

					parallel_work_target[parallel_count] = *chunk_ptr;
					++parallel_count;
				}

				// 並列実行.
				ParallelFor(parallel_count, [this, &parallel_work_target, default_chunk_gen_noise_scale](int32 index)
				{
					auto chunk = parallel_work_target[index];

					// 状態遷移
					chunk->SetState(ngl::NglVoxelChunkState::Allocating);
					// 内部メモリ確保
					chunk->Allocate();

					// ステート変更
					chunk->SetState(ngl::NglVoxelChunkState::Loading);
					chunk->Fill(false);// 念の為フィル
					// デコード対象チャンクのデータがあるかどうかで分岐
					if (false)
					{
						// TODO データがあればデコードする
					}
					else
					{
						// データが無ければ適当なノイズから新規生成.
						GenerateChunkFromNoise(*chunk, default_chunk_gen_noise_scale, 2);
					}
				
					// すべてのエッジ部のDirtyをセット.
					// StreamInではなく動的更新の場合は変更のあったエッジのみDirtyを建てるように.
					chunk->ClearEdgeVoxelChangeFlag(~0u);
					// 新規生成により近傍のオーバーラップ部を取り込む必要があるため近傍変更フラグもDirty.
					chunk->ClearNeighborChunkChangeFlag(~0u);
					chunk->SetAnyVoxelChangeFlag(true);


					// 生成が完了したらフラグ設定
					chunk->SetState(ngl::NglVoxelChunkState::Active);

				}, false);


				// 完了リストに追加
				for (unsigned int pi = 0; pi < parallel_count; ++pi)
					stream_in_chunk_complete_array_.Add(parallel_work_target[pi]->GetId());



				const auto loop_end_time = std::chrono::system_clock::now();
				const auto loop_micro_sec = std::chrono::duration_cast<std::chrono::microseconds>(loop_end_time - start_time).count();
				const auto elapsed_total_micro_sec = std::chrono::duration_cast<std::chrono::microseconds>(loop_end_time - async_start_time).count();
				is_continue = (async_continue_limit_micro_sec >= (elapsed_total_micro_sec + loop_micro_sec));// 同じ時間がかかる次のループを含めて時間内に終わりそうなら継続.
			}
		}

		async_micro_sec_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - async_start_time).count();

	}
	// チャンクをノイズから生成
	void ANglVoxelEngine::GenerateChunkFromNoise(ChunkType& out_chunk, float default_chunk_gen_noise_scale, int noise_octave_count) const
	{
		// データが無ければ適当なノイズから新規生成.
		// ノイズ計算を含めてかなり重いためAsync実行したい.

		// 生成時はLOD0から生成. LODMaxから作ることもできる？
		// LOD0の生成.
		for (auto lk = 0u; lk < ChunkType::CHUNK_RESOLUTION(); ++lk)
		{
			for (auto lj = 0u; lj < ChunkType::CHUNK_RESOLUTION(); ++lj)
			{
				auto* row = out_chunk.GetXRow(lj, lk, 0);

				for (auto li = 0u; li < ChunkType::CHUNK_RESOLUTION(); ++li)
				{
					// チャンク内セル中心ワールド座標
					const auto cell_center_pos_world = CalcChunkVoxelCenterPosition(out_chunk.GetId(), FIntVector(li, lj, lk), 0);

					// ワールド位置から適当なノイズで初期生成.
					auto noise = 0.0f;

#if 0
					// チャンク範囲の確認をしやすくするためにZゼロ平面より上には生成しない
					if ((0.0f > cell_center_pos_world.Z))
					{
						noise = ngl::NglVoxelNoise::Fbm<false>(cell_center_pos_world * default_chunk_gen_noise_scale, noise_octave_count).X;
						//noise = 1.0f;
					}
#else
					noise = ngl::NglVoxelNoise::Fbm<false>(cell_center_pos_world * default_chunk_gen_noise_scale, noise_octave_count).X;

					const float height_base = 1000.0f;
					const float height_rate = FMath::Max(0.0f, ((cell_center_pos_world.Z) / height_base));

					const auto cell_center_xy_pos_world = FVector(cell_center_pos_world.X, cell_center_pos_world.Y, 0.0f);
					const float distance_rate = FMath::Clamp((cell_center_xy_pos_world.Length() - 500.0f) / 50000.0f, 0.0f, 1.0f);

					/*
					const auto noise_plane_sample_pos = FVector(cell_center_pos_world.X, cell_center_pos_world.Y, 0.0f);
					const auto noise_plane_xy = ngl::NglVoxelNoise::Fbm<false>(noise_plane_sample_pos * default_chunk_gen_noise_scale * 0.5f, 1).X;
					noise += ((noise_plane_xy) - height_rate);
					*/
					
					noise = FMath::Max(0.0f, (noise) - (height_rate * (1.0 - distance_rate)));


#endif
					auto cell_value = 0.0f < noise;
					//out_chunk.Set(0 != cell_value, li, lj, lk, 0);
					row[li] = (cell_value)? 0xff : 0;
				}
			}
		}

		// Generate LOD.
		for (auto lod = 1u; lod < ChunkType::LOD_COUNT(); ++lod)
		{
			const auto parent_lod = lod - 1;
		
			const auto reso = ChunkType::CHUNK_RESOLUTION(lod);

			for (auto lk = 0u; lk < reso; ++lk)
			{
				for (auto lj = 0u; lj < reso; ++lj)
				{
					auto* child_row = out_chunk.GetXRow(lj, lk, lod);

					for (auto li = 0u; li < reso; ++li)
					{
						const auto parent_lk_base = lk * 2;
						const auto parent_lj_base = lj * 2;
						const auto parent_li_base = li * 2;

						auto* parent_row0 = out_chunk.GetXRow(parent_lj_base, parent_lk_base, parent_lod);
						auto* parent_row1 = out_chunk.GetXRow(parent_lj_base+1, parent_lk_base, parent_lod);
						auto* parent_row2 = out_chunk.GetXRow(parent_lj_base, parent_lk_base+1, parent_lod);
						auto* parent_row3 = out_chunk.GetXRow(parent_lj_base+1, parent_lk_base+1, parent_lod);


						const auto pv0 = parent_row0[parent_li_base];
						const auto pv1 = parent_row0[parent_li_base + 1];
						const auto pv2 = parent_row1[parent_li_base];
						const auto pv3 = parent_row1[parent_li_base + 1];

						const auto pv4 = parent_row2[parent_li_base];
						const auto pv5 = parent_row2[parent_li_base + 1];
						const auto pv6 = parent_row3[parent_li_base];
						const auto pv7 = parent_row3[parent_li_base + 1];

						// 現状のLODは上位の最大値を入れておく
						const auto pvmax0 = FMath::Max(pv0, pv1);
						const auto pvmax1 = FMath::Max(pv2, pv3);
						const auto pvmax2 = FMath::Max(pv4, pv5);
						const auto pvmax3 = FMath::Max(pv6, pv7);
					
						child_row[li] = FMath::Max(FMath::Max(pvmax0, pvmax1), FMath::Max(pvmax2, pvmax3));
					}
				}
			}
		}

	}

	void ANglVoxelEngine::UpdateRenderChunk(const TArray<FIntVector>& render_dirty_chunk_id_array)
	{
#if 1
		UpdateRenderChunkSurfaceNets_NaiveVoxel(render_dirty_chunk_id_array);
#else
		UpdateRenderChunkDebugCube(render_dirty_chunk_id_array);
#endif
		//UpdateRenderChunkSurfaceNets_BitCompressionVoxel(render_dirty_chunk_id_array);
	}
	void ANglVoxelEngine::UpdateRenderChunkSurfaceNets_NaiveVoxel(const TArray<FIntVector>& render_dirty_chunk_id_array)
	{
		constexpr auto chunk_cell_pos_max = ChunkType::CHUNK_RESOLUTION() - 1;

		// 描画更新要求チャンクの隣接チャンクを含めたリストを構築する.
		for (auto&& e : render_dirty_chunk_id_array)
		{
			auto&& find_chunk_ptr = voxel_chunk_map_.Find(e);
			if (!find_chunk_ptr)
				continue;

			auto&& find_chunk = *find_chunk_ptr;
			assert(nullptr != find_chunk);

			if (ngl::NglVoxelChunkState::Active != find_chunk->GetState())
				continue;

			UProceduralMeshComponent* mesh_comp = nullptr;
			if (auto&& chunk_mesh = chunk_proc_mesh_component_map_.Find(e))
			{
				mesh_comp = *chunk_mesh;
			}

			if (!mesh_comp)
			{
				// なければ生成

				mesh_comp = NewObject<UProceduralMeshComponent>(this);
				mesh_comp->RegisterComponent();
				mesh_comp->SetFlags(RF_Transactional);
				mesh_comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

				// シャドウ無効
				mesh_comp->SetCastShadow(false);
				mesh_comp->bCastDynamicShadow = false;

				mesh_comp->SetMaterial(0, material_);

				if (chunk_proc_mesh_component_map_.Contains(e))
					chunk_proc_mesh_component_map_[e] = mesh_comp;
				else
					chunk_proc_mesh_component_map_.Add(e, mesh_comp);
			}

			// クリア
			if (0 < mesh_comp->GetNumSections())
				mesh_comp->ClearMeshSection(0);

			if (true)
			{
				bool bCreateCollision = true;
				TArray<FVector> vtx;
				TArray<int32> tri;
				TArray<FVector> nor;
				TArray<FVector2D> uv0;
				TArray<FColor> col;
				TArray<FProcMeshTangent> tan;
#
				// SurfaceNets.
				// 元のVoxelを頂点とするようなSurfaceVoxelを考え、構成するエッジに境界があるかテストする.
				// 表示LODレベル
				//const auto lod_level = find_chunk->GetCurrentLodLevel();
				const auto lod_level = 0;// 簡単のためLOD0固定
			
				const float lod_voxel_size = 1.0f * static_cast<float>(1 << lod_level);
				const auto chunk_reso = ChunkType::CHUNK_RESOLUTION(lod_level);
				const auto voxel_extent = GetVoxelSize(lod_level);


				for (auto k = 0u; k < chunk_reso; ++k)
				{
					for (auto j = 0u; j < chunk_reso; ++j)
					{
						// SurfaceNetsの頂点計算のために必要な追加の近傍
						const auto row_y0z0 = find_chunk->GetXRowWithOverlap(j, k, lod_level);
						const auto row_y0z1 = find_chunk->GetXRowWithOverlap(j, k + 1, lod_level);
						const auto row_y0z2 = find_chunk->GetXRowWithOverlap(j, k + 2, lod_level);

						const auto row_y1z0 = find_chunk->GetXRowWithOverlap(j + 1, k, lod_level);
						const auto row_y1z1 = find_chunk->GetXRowWithOverlap(j + 1, k + 1, lod_level);
						const auto row_y1z2 = find_chunk->GetXRowWithOverlap(j + 1, k + 2, lod_level);

						const auto row_y2z0 = find_chunk->GetXRowWithOverlap(j + 2, k, lod_level);
						const auto row_y2z1 = find_chunk->GetXRowWithOverlap(j + 2, k + 1, lod_level);
						const auto row_y2z2 = find_chunk->GetXRowWithOverlap(j + 2, k + 2, lod_level);

						uint32_t x_row_x0y1z0 = 0;
						uint32_t x_row_x0y0z1 = 0;
						uint32_t x_row_x0y1z1 = 0;

						for (auto ri = 0u; ri < ChunkType::CHUNK_RESOLUTION_WITH_OVERLAP(lod_level); ++ri)
						{
							x_row_x0y1z0 |= (row_y1z0[ri]) ? (0x01 << (ri)) : 0;
							x_row_x0y0z1 |= (row_y0z1[ri]) ? (0x01 << (ri)) : 0;
							x_row_x0y1z1 |= (row_y1z1[ri]) ? (0x01 << (ri)) : 0;
						}

						const auto shift_count_top = (chunk_reso - 1);
						// X方向に-1シフトして+X隣接チャンクの情報を最上位に埋め込む.
						uint32_t shifted_x0y1z0_row = x_row_x0y1z0 >> 1;//
						uint32_t shifted_x0y1z1_row = x_row_x0y1z1 >> 1;//
						uint32_t shifted_x0y0z1_row = x_row_x0y0z1 >> 1;//


						// XY 3x3の中心のZ差分
						uint32_t z_dif_center = (shifted_x0y1z0_row) ^ (shifted_x0y1z1_row);
						// YZ 3X3の中心のX差分
						uint32_t x_dif_center = (x_row_x0y1z1) ^ (shifted_x0y1z1_row);
						// ZX 3x3の中心のY差分
						uint32_t y_dif_center = (shifted_x0y0z1_row) ^ (shifted_x0y1z1_row);

						for (auto i = 0u; i < chunk_reso; ++i)
						{
							// Z差分
							const bool has_z_diff = (z_dif_center & (1 << (i)));
							// X差分
							const bool has_x_diff = (x_dif_center & (1 << (i)));
							// Y差分
							const bool has_y_diff = (y_dif_center & (1 << (i)));

							if (!has_x_diff && !has_y_diff && !has_z_diff)
								continue;

							// return : SurfaceNetsのSurfacePoint位置を[0,1]の割合で返す.
							// voxel_sdf_values : 8点のsdf値を xyzの順で優先した配列. (x0y0z0,x1y0z0,x0y1z0,x1y1z0...).
							const auto func_gen_surface_point = [](float (&voxel_sdf_values)[8])
							{
								FVector pos_rate = FVector::ZeroVector;

								float rate_div_work = 0.0f;
								for (auto i = 0u; i < 4; i += 1)
								{
									const auto i0 = i * 2;
									const auto i1 = i0 + 1;
									if (0 > voxel_sdf_values[i0] * voxel_sdf_values[i1])
									{
										float rate = 1 - voxel_sdf_values[i0] / (voxel_sdf_values[i0] - voxel_sdf_values[i1]);
										pos_rate += FVector(rate, (i0 >> 1) & 0b01, (i0 >> 2) & 0b01);
									
										rate_div_work += 1.0f;
									}
								}
							
								for (auto i = 0u; i < 4; i += 1)
								{
									const auto i0 = (i & 0b01) + ((i>>1) & 0b01) * 4;
									const auto i1 = i0 + 2;
									if (0 > voxel_sdf_values[i0] * voxel_sdf_values[i1])
									{
										float rate = 1 - voxel_sdf_values[i0] / (voxel_sdf_values[i0] - voxel_sdf_values[i1]);
										pos_rate += FVector(i0 & 0b01, rate, (i0 >> 2) & 0b01);

										rate_div_work += 1.0f;
									}
								}

								for (auto i = 0u; i < 4; i += 1)
								{
									const auto i0 = i;
									const auto i1 = i0 + 4;
									if (0 > voxel_sdf_values[i0] * voxel_sdf_values[i1])
									{
										float rate = 1 - voxel_sdf_values[i0] / (voxel_sdf_values[i0] - voxel_sdf_values[i1]);
										pos_rate += FVector(i0 & 0b01, (i0 >> 1) & 0b01, rate);

										rate_div_work += 1.0f;
									}
								}

								if (0.0f < rate_div_work)
									pos_rate /= rate_div_work;

								return pos_rate;
							};


							float local_voxel_sdf[8];
							local_voxel_sdf[0] = (0 != row_y0z0[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[1] = (0 != row_y0z0[i+1]) ? -1.0f : 1.0f;
							local_voxel_sdf[2] = (0 != row_y1z0[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[3] = (0 != row_y1z0[i+1]) ? -1.0f : 1.0f;
							local_voxel_sdf[4] = (0 != row_y0z1[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[5] = (0 != row_y0z1[i+1]) ? -1.0f : 1.0f;
							local_voxel_sdf[6] = (0 != row_y1z1[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[7] = (0 != row_y1z1[i+1]) ? -1.0f : 1.0f;
							const auto surface_pos_0 = func_gen_surface_point(local_voxel_sdf);

							local_voxel_sdf[0] = (0 != row_y0z0[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[1] = (0 != row_y0z0[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[2] = (0 != row_y1z0[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[3] = (0 != row_y1z0[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[4] = (0 != row_y0z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[5] = (0 != row_y0z1[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[6] = (0 != row_y1z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[7] = (0 != row_y1z1[i + 2]) ? -1.0f : 1.0f;
							const auto surface_pos_1 = func_gen_surface_point(local_voxel_sdf);

							local_voxel_sdf[0] = (0 != row_y1z0[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[1] = (0 != row_y1z0[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[2] = (0 != row_y2z0[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[3] = (0 != row_y2z0[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[4] = (0 != row_y1z1[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[5] = (0 != row_y1z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[6] = (0 != row_y2z1[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[7] = (0 != row_y2z1[i + 1]) ? -1.0f : 1.0f;
							const auto surface_pos_2 = func_gen_surface_point(local_voxel_sdf);

							local_voxel_sdf[0] = (0 != row_y1z0[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[1] = (0 != row_y1z0[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[2] = (0 != row_y2z0[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[3] = (0 != row_y2z0[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[4] = (0 != row_y1z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[5] = (0 != row_y1z1[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[6] = (0 != row_y2z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[7] = (0 != row_y2z1[i + 2]) ? -1.0f : 1.0f;
							const auto surface_pos_3 = func_gen_surface_point(local_voxel_sdf);

							local_voxel_sdf[0] = (0 != row_y0z1[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[1] = (0 != row_y0z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[2] = (0 != row_y1z1[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[3] = (0 != row_y1z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[4] = (0 != row_y0z2[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[5] = (0 != row_y0z2[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[6] = (0 != row_y1z2[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[7] = (0 != row_y1z2[i + 1]) ? -1.0f : 1.0f;
							const auto surface_pos_4 = func_gen_surface_point(local_voxel_sdf);

							local_voxel_sdf[0] = (0 != row_y0z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[1] = (0 != row_y0z1[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[2] = (0 != row_y1z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[3] = (0 != row_y1z1[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[4] = (0 != row_y0z2[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[5] = (0 != row_y0z2[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[6] = (0 != row_y1z2[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[7] = (0 != row_y1z2[i + 2]) ? -1.0f : 1.0f;
							const auto surface_pos_5 = func_gen_surface_point(local_voxel_sdf);

							local_voxel_sdf[0] = (0 != row_y1z1[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[1] = (0 != row_y1z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[2] = (0 != row_y2z1[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[3] = (0 != row_y2z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[4] = (0 != row_y1z2[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[5] = (0 != row_y1z2[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[6] = (0 != row_y2z2[i]) ? -1.0f : 1.0f;
							local_voxel_sdf[7] = (0 != row_y2z2[i + 1]) ? -1.0f : 1.0f;
							const auto surface_pos_6 = func_gen_surface_point(local_voxel_sdf);

							local_voxel_sdf[0] = (0 != row_y1z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[1] = (0 != row_y1z1[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[2] = (0 != row_y2z1[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[3] = (0 != row_y2z1[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[4] = (0 != row_y1z2[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[5] = (0 != row_y1z2[i + 2]) ? -1.0f : 1.0f;
							local_voxel_sdf[6] = (0 != row_y2z2[i + 1]) ? -1.0f : 1.0f;
							local_voxel_sdf[7] = (0 != row_y2z2[i + 2]) ? -1.0f : 1.0f;
							const auto surface_pos_7 = func_gen_surface_point(local_voxel_sdf);



							// 3x3近傍の中心でZの方向の境界があるか
							if (has_z_diff)
							{
								// 正の方向を向いているか
								const bool face_to_positive = 0 < (shifted_x0y1z0_row & (1 << (i)));

								// 共有頂点は無視して独立して頂点生成してみる
								// 頂点の値を無視して常にセル中心にサーフェイス頂点生成
								auto pos0 = surface_pos_0 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i, j, k), lod_level);
								auto pos1 = surface_pos_1 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i + 1, j, k), lod_level);
								auto pos2 = surface_pos_2 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i, j + 1, k), lod_level);
								auto pos3 = surface_pos_3 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i + 1, j + 1, k), lod_level);

								// 時計回り
								const auto vtx_id0 = vtx.Add(pos0);
								const auto vtx_id1 = vtx.Add(pos1);
								const auto vtx_id2 = vtx.Add(pos3);
								const auto vtx_id3 = vtx.Add(pos2);

								// 法線を計算
								FVector face_normal =  FVector::CrossProduct(pos1 - pos0, pos2 - pos0);
								if (!face_normal.IsNearlyZero())
								{
									face_normal.Normalize();
								}
								else
								{
									face_normal = FVector::CrossProduct(pos2 - pos3, pos1 - pos3);
									face_normal.Normalize();
								}
								if (!face_to_positive)
									face_normal = -face_normal;
								for (auto nrm_i = 0u; nrm_i < 4; ++nrm_i)
									nor.Add(face_normal);

								// カラー
								for (auto col_i = 0u; col_i < 4; ++col_i)
								{
									if ((chunk_reso - 1) == i)
										col.Add(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f).ToFColor(true));
									else if ((chunk_reso - 1) == j)
										col.Add(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f).ToFColor(true));
									else if ((chunk_reso - 1) == k)
										col.Add(FLinearColor(0.0f, 0.0f, 1.0f, 1.0f).ToFColor(true));
									else if (0 == i)
										col.Add(FLinearColor(0.25f, 0.0f, 0.0f, 1.0f).ToFColor(true));
									else if (0 == j)
										col.Add(FLinearColor(0.0f, 0.25f, 0.0f, 1.0f).ToFColor(true));
									else if (0 == k)
										col.Add(FLinearColor(0.0f, 0.0f, 0.25f, 1.0f).ToFColor(true));
									else
										col.Add(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f).ToFColor(true));
								}

								// インデックス
								if (face_to_positive)
								{
									tri.Add(vtx_id0);
									tri.Add(vtx_id2);
									tri.Add(vtx_id1);
									tri.Add(vtx_id0);
									tri.Add(vtx_id3);
									tri.Add(vtx_id2);
								}
								else
								{
									tri.Add(vtx_id0);
									tri.Add(vtx_id1);
									tri.Add(vtx_id2);
									tri.Add(vtx_id0);
									tri.Add(vtx_id2);
									tri.Add(vtx_id3);
								}
							}
							// 3x3近傍の中心でXの方向の境界があるか
							if (has_x_diff)
							{
								// 正の方向を向いているか
								const bool face_to_positive = 0 < (x_row_x0y1z1 & (1 << (i)));

								auto pos0 = surface_pos_0 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i, j, k), lod_level);
								auto pos1 = surface_pos_2 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i, j + 1, k), lod_level);
								auto pos2 = surface_pos_4 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i, j, k + 1), lod_level);
								auto pos3 = surface_pos_6 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i, j + 1, k + 1), lod_level);

								// 頂点
								const auto vtx_id0 = vtx.Add(pos0);
								const auto vtx_id1 = vtx.Add(pos1);
								const auto vtx_id2 = vtx.Add(pos3);
								const auto vtx_id3 = vtx.Add(pos2);

								// 法線を計算
								FVector face_normal = FVector::CrossProduct(pos1 - pos0, pos2 - pos0);
								if (!face_normal.IsNearlyZero())
								{
									face_normal.Normalize();
								}
								else
								{
									face_normal = FVector::CrossProduct(pos2 - pos3, pos1 - pos3);
									face_normal.Normalize();
								}
								if (!face_to_positive)
									face_normal = -face_normal;
								for (auto nrm_i = 0u; nrm_i < 4; ++nrm_i)
									nor.Add(face_normal);

								// カラー
								for (auto col_i = 0u; col_i < 4; ++col_i)
								{
									if ((chunk_reso - 1) == i)
										col.Add(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f).ToFColor(true));
									else if ((chunk_reso - 1) == j)
										col.Add(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f).ToFColor(true));
									else if ((chunk_reso - 1) == k)
										col.Add(FLinearColor(0.0f, 0.0f, 1.0f, 1.0f).ToFColor(true));
									else if (0 == i)
										col.Add(FLinearColor(0.25f, 0.0f, 0.0f, 1.0f).ToFColor(true));
									else if (0 == j)
										col.Add(FLinearColor(0.0f, 0.25f, 0.0f, 1.0f).ToFColor(true));
									else if (0 == k)
										col.Add(FLinearColor(0.0f, 0.0f, 0.25f, 1.0f).ToFColor(true));
									else
										col.Add(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f).ToFColor(true));
								}

								// インデックス
								if (face_to_positive)
								{
									tri.Add(vtx_id0);
									tri.Add(vtx_id2);
									tri.Add(vtx_id1);
									tri.Add(vtx_id0);
									tri.Add(vtx_id3);
									tri.Add(vtx_id2);
								}
								else
								{
									tri.Add(vtx_id0);
									tri.Add(vtx_id1);
									tri.Add(vtx_id2);
									tri.Add(vtx_id0);
									tri.Add(vtx_id2);
									tri.Add(vtx_id3);
								}
							}

							// 3x3近傍の中心でYの方向の境界があるか
							if (has_y_diff)
							{
								// 正の方向を向いているか
								const bool face_to_positive = 0 < (shifted_x0y0z1_row & (1 << (i)));

								auto pos0 = surface_pos_0 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i, j, k), lod_level);
								auto pos1 = surface_pos_1 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i + 1, j, k), lod_level);
								auto pos2 = surface_pos_4 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i, j, k + 1), lod_level);
								auto pos3 = surface_pos_5 * FVector::OneVector * voxel_extent + CalcChunkVoxelCenterPosition(e, FIntVector(i + 1, j, k + 1), lod_level);

								// 時計回り
								const auto vtx_id0 = vtx.Add(pos0);
								const auto vtx_id1 = vtx.Add(pos2);
								const auto vtx_id2 = vtx.Add(pos3);
								const auto vtx_id3 = vtx.Add(pos1);

								// 法線を計算
								FVector face_normal = FVector::CrossProduct(pos2 - pos0, pos1 - pos0);
								if (!face_normal.IsNearlyZero())
								{
									face_normal.Normalize();
								}
								else
								{
									face_normal = FVector::CrossProduct(pos1 - pos3, pos2 - pos3);
									face_normal.Normalize();
								}
								if (!face_to_positive)
									face_normal = -face_normal;
								for (auto nrm_i = 0u; nrm_i < 4; ++nrm_i)
									nor.Add(face_normal);

								// カラー
								for (auto col_i = 0u; col_i < 4; ++col_i)
								{
									if ((chunk_reso - 1) == i)
										col.Add(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f).ToFColor(true));
									else if ((chunk_reso - 1) == j)
										col.Add(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f).ToFColor(true));
									else if ((chunk_reso - 1) == k)
										col.Add(FLinearColor(0.0f, 0.0f, 1.0f, 1.0f).ToFColor(true));
									else if (0 == i)
										col.Add(FLinearColor(0.25f, 0.0f, 0.0f, 1.0f).ToFColor(true));
									else if (0 == j)
										col.Add(FLinearColor(0.0f, 0.25f, 0.0f, 1.0f).ToFColor(true));
									else if (0 == k)
										col.Add(FLinearColor(0.0f, 0.0f, 0.25f, 1.0f).ToFColor(true));
									else
										col.Add(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f).ToFColor(true));
								}

								// インデックス
								if (face_to_positive)
								{
									tri.Add(vtx_id0);
									tri.Add(vtx_id2);
									tri.Add(vtx_id1);
									tri.Add(vtx_id0);
									tri.Add(vtx_id3);
									tri.Add(vtx_id2);
								}
								else
								{
									tri.Add(vtx_id0);
									tri.Add(vtx_id1);
									tri.Add(vtx_id2);
									tri.Add(vtx_id0);
									tri.Add(vtx_id2);
									tri.Add(vtx_id3);
								}
							}
						}

					}
				}

				if (0 < tri.Num())
					mesh_comp->CreateMeshSection(0, vtx, tri, nor, uv0, col, tan, bCreateCollision);
			}
		}
	}

	// デバッグ用のキューブ描画.
	void ANglVoxelEngine::UpdateRenderChunkDebugCube(const TArray<FIntVector>& render_dirty_chunk_id_array)
	{
		// 現状では非ゼロかどうかのみでVoxelの表示をする.
		TArray<std::tuple<int, int, uint32_t>> x_row_visible_bits_work;
		for (auto&& e : render_dirty_chunk_id_array)
		{
			auto&& find_chunk_ptr = voxel_chunk_map_.Find(e);
			if (!find_chunk_ptr)
				continue;

			auto&& find_chunk = *find_chunk_ptr;
			assert(nullptr != find_chunk);

			if (ngl::NglVoxelChunkState::Active != find_chunk->GetState())
				continue;

			UInstancedStaticMeshComponent* mesh_comp = nullptr;
			if (auto&& chunk_mesh = chunk_mesh_component_map_.Find(e))
			{
				mesh_comp = *chunk_mesh;
			}
			else
			{
				mesh_comp = GetChunkMeshComponentFromPool();
				if (mesh_comp)
				{
					chunk_mesh_component_map_.Add(e, mesh_comp);
				}
				else
				{
					UE_LOG(LogTemp, Fatal, TEXT("[ANglVoxelEngine] ChunkMesh Component Pool Empty."));
				}
			}

			if (!mesh_comp)
				continue;// 念の為

			// 一応トランスフォーム設定(EngineがComponentの位置で描画の最適化をするかもしれないので)
			mesh_comp->SetWorldLocation(CalcChunkVoxelCenterPosition(e, FIntVector(0, 0, 0), 0));

			// セットアップ時も改めてシャドウ無効設定
			mesh_comp->SetCastShadow(false);
			mesh_comp->bCastDynamicShadow = false;

			// 表示LODレベル
			const auto lod_level = find_chunk->GetCurrentLodLevel();
			const float lod_voxel_size = 1.0f * static_cast<float>(1 << lod_level);

			const auto chunk_reso = ChunkType::CHUNK_RESOLUTION(lod_level);

			// 事前に表面セルの個数をカウントしてPreAllocateInstancesMemoryで事前にInstanceメモリをアロケートしてからセットアップすることで高速化.
			x_row_visible_bits_work.Empty(x_row_visible_bits_work.Max());// ワーククリア(メモリはそのまま)
			int num_visible_cell = 0;
			for (auto k = 0u; k < chunk_reso; ++k)
			{
				for (auto j = 0u; j < chunk_reso; ++j)
				{
					// Overlap込取得のため +1
					auto* xrow = find_chunk->GetXRowWithOverlap(j + 1, k + 1, lod_level);

					// 簡易に境界のみ表示するため近傍Voxelを参照
					// Overlap込取得で前後を取得
					auto* xrow0 = find_chunk->GetXRowWithOverlap(j, k + 1, lod_level);
					auto* xrow1 = find_chunk->GetXRowWithOverlap(j + 2, k + 1, lod_level);
					auto* xrow2 = find_chunk->GetXRowWithOverlap(j + 1, k, lod_level);
					auto* xrow3 = find_chunk->GetXRowWithOverlap(j + 1, k + 2, lod_level);

					uint32_t visible_bits = 0;

					for (auto i = 0u; i < chunk_reso; ++i)
					{
						const auto x_i_with_overlap = i + 1;

						if (0 < xrow[x_i_with_overlap])
						{
							// 各軸の隣接セルがすべて有効な場合は不可視.
							const auto exist_side_x = xrow[x_i_with_overlap - 1] && xrow[x_i_with_overlap + 1];
							const auto exist_side_y = xrow0[x_i_with_overlap] && xrow1[x_i_with_overlap];
							const auto exist_side_z = xrow2[x_i_with_overlap] && xrow3[x_i_with_overlap];

							if (!(exist_side_x && exist_side_y && exist_side_z))
							{
								visible_bits |= 0x01 << i;
								++num_visible_cell;
							}
						}
					}

					x_row_visible_bits_work.Add(std::tuple<int, int, uint32_t>(j, k, visible_bits));
				}
			}

			// インスタンスの準備
			mesh_comp->ClearInstances();
			// 必要分事前アロケート
			mesh_comp->PreAllocateInstancesMemory(num_visible_cell);

			int inst_count = 0;
			for (const auto& elem : x_row_visible_bits_work)
			{
				const auto row_y = std::get<0>(elem);
				const auto row_z = std::get<1>(elem);
				const auto x_row_visible_bits = std::get<2>(elem);

				for (auto i = 0u; i < chunk_reso; ++i)
				{
					if (0 != (x_row_visible_bits & (1 << i)))
					{
						const auto cell_center_pos_world = CalcChunkVoxelCenterPosition(e, FIntVector(i, row_y, row_z), lod_level);

						// 見やすいように縮小スケールする場合はここ.
						const FVector scale = FVector(0.8 * lod_voxel_size);
						mesh_comp->AddInstance(FTransform(FQuat::Identity, cell_center_pos_world, scale), true);
						++inst_count;
					}
				}
			}
		}
	}
	
	float ANglVoxelEngine::GetVoxelSize(unsigned int lod) const
	{
		return voxel_size_ * static_cast<float>(1 << lod);
	}
	FVector ANglVoxelEngine::CalcChunkVoxelMinPosition(const FIntVector& chunk, const FIntVector& pos, unsigned int lod) const
	{
		const float lod_voxel_size = GetVoxelSize(lod);
		const auto v = (FVector(chunk * ChunkType::CHUNK_RESOLUTION(lod) + pos)) * lod_voxel_size;
		return v;
	}
	FVector ANglVoxelEngine::CalcChunkVoxelCenterPosition(const FIntVector& chunk, const FIntVector& pos, unsigned int lod) const
	{
		const float lod_voxel_size = GetVoxelSize(lod);
		const auto v = (FVector(chunk * ChunkType::CHUNK_RESOLUTION(lod) + pos) + FVector(0.5f)) * lod_voxel_size;
		return v;
	}

	UInstancedStaticMeshComponent* ANglVoxelEngine::GetChunkMeshComponentFromPool()
	{
		if (0 >= chunk_mesh_component_pool_.Num())
		{
			// 生成
			auto new_comp = NewObject<UInstancedStaticMeshComponent>(this);
			new_comp->RegisterComponent();
			new_comp->SetStaticMesh(voxel_mesh_);
			new_comp->SetFlags(RF_Transactional);
			new_comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			// シャドウ無効
			new_comp->SetCastShadow(false);
			new_comp->bCastDynamicShadow = false;

			// マテリアルは後で
			new_comp->SetMaterial(0, material_);
			this->AddInstanceComponent(new_comp);

			chunk_mesh_component_pool_.Push(new_comp);
		}

		// Pop
		auto* comp = chunk_mesh_component_pool_.Pop();
		return comp;
	}
	void ANglVoxelEngine::RestoreChunkMeshComponentToPool(UInstancedStaticMeshComponent* comp)
	{
		if (!comp)
			return;
		chunk_mesh_component_pool_.Add(comp);
	}





