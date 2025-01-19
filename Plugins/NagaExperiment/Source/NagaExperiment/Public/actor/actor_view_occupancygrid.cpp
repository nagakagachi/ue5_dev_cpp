// Fill out your copyright notice in the Description page of Project Settings.


#include "actor_view_occupancygrid.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

#include "Runtime/Core/Public/Async/ParallelFor.h"

#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"

#include <cmath>



//--------------------------------------------------------------------------------------------------------------------------------------
namespace
{
}

AOccupancyGridTest::AOccupancyGridTest()
{
	PrimaryActorTick.bCanEverTick = true;


	// 可視化用のInstanceMeshコンポーネント生成
	{
		ismc_occupancygrid_ = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VisualizeMeshComponent"));
		ismc_occupancygrid_->SetFlags(RF_Transactional);
		ismc_occupancygrid_->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ismc_occupancygrid_->SetupAttachment(RootComponent);
		ismc_occupancygrid_->NumCustomDataFloats = 3;
	}
	{
		ismc_dcgrid_ = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VisualizeMeshComponent_Dcgrid"));
		ismc_dcgrid_->SetFlags(RF_Transactional);
		ismc_dcgrid_->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ismc_dcgrid_->SetupAttachment(RootComponent);
		ismc_dcgrid_->NumCustomDataFloats = 3;
	}
}

void AOccupancyGridTest::BeginPlay()
{
	Super::BeginPlay();

	{
		// Set Mesh and Material.
		ismc_occupancygrid_->SetStaticMesh(testStaticMesh_);
		ismc_occupancygrid_->SetMaterial(0, testMaterial_);
		ismc_occupancygrid_->SetCastShadow(false);
		// Add Default Instance
		FTransform isttr = {};
		ismc_occupancygrid_->AddInstance(isttr);
	}
	{
		// Set Mesh and Material.
		ismc_dcgrid_->SetStaticMesh(testStaticMesh_);
		ismc_dcgrid_->SetMaterial(0, testMaterial_);
		ismc_dcgrid_->SetCastShadow(false);
		// Add Default Instance
		FTransform isttr = {};
		ismc_dcgrid_->AddInstance(isttr);
	}
	
	if (debug_dcgrid_)
	{
		dcgrid_.Initialize();
	}
	
	if (debug_ocgrid_)
	{
		ocgrid_.Initialize(5000.0f);
	}
}

void AOccupancyGridTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);


	// カレントのViewport,カメラ位置カメラ姿勢情報取得.
	FVector2D viewport_size(1, 1);
	FVector view_location{};
	FQuat view_quat{};
	float view_fov_x{};
	GetCurrentViewportInfo(GetWorld(), viewport_size, view_location, view_quat, view_fov_x);

	// ViewFrustum内でレイキャストテスト.
	const float vx = FMath::Tan(view_fov_x * 0.5f);
	const float vy = vx * (viewport_size.Y / viewport_size.X);
	// CameraPoseQuatからUEにおけるRight,Up,Forward軸ベクトルを計算.
	const auto view_right = view_quat.RotateVector(FVector::RightVector);
	const auto view_up = view_quat.RotateVector(FVector::UpVector);
	const auto view_dir = view_quat.RotateVector(FVector::ForwardVector);

	FVector frustum_corner_nn = (-view_right * vx) + (-view_up * vy) + (view_dir * 1.0f);

	FVector frustum_corner_dir_array[4] =
	{
		frustum_corner_nn,
		frustum_corner_nn + (view_right * vx * 2.0f) + (view_up * vy * 0.0f),
		frustum_corner_nn + (view_right * vx * 0.0f) + (view_up * vy * 2.0f),
		frustum_corner_nn + (view_right * vx * 2.0f) + (view_up * vy * 2.0f),
	};

	// デバッグ表示高速化用のフラスタムカリング準備.
	constexpr float frustum_near_range = 100.0f;
	constexpr float frustum_far_range = frustum_near_range + 50000.0f;
	FVector frustum_aabb_min(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector frustum_aabb_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (auto fd : frustum_corner_dir_array)
	{
		const auto n = fd * frustum_near_range;
		const auto f = fd * frustum_far_range;
		frustum_aabb_min = frustum_aabb_min.ComponentMin(n.ComponentMin(f));
		frustum_aabb_max = frustum_aabb_max.ComponentMax(n.ComponentMax(f));
	}
	// Frustum AABB.
	FBox frustum_aabb(frustum_aabb_min + view_location, frustum_aabb_max + view_location);
	// FrustumVolume形状構築.
	FConvexVolume frustum_volume;
	{
		FVector fp[] =
		{
			frustum_corner_dir_array[0] * frustum_near_range + view_location,
			frustum_corner_dir_array[1] * frustum_near_range + view_location,
			frustum_corner_dir_array[2] * frustum_near_range + view_location,
			frustum_corner_dir_array[3] * frustum_near_range + view_location,
			frustum_corner_dir_array[0] * frustum_far_range + view_location,
			frustum_corner_dir_array[1] * frustum_far_range + view_location,
			frustum_corner_dir_array[2] * frustum_far_range + view_location,
			frustum_corner_dir_array[3] * frustum_far_range + view_location
		};
		FIntVector4 plane_vtx_id[] =
		{
			{ 0, 2, 3, 1 }, // near
			{ 4, 5, 7, 6 }, // far
			{ 0, 1, 5, 4 }, // bottom
			{ 2, 6, 7, 3 }, // top
			{ 1, 3, 7, 5 }, // right
			{ 0, 4, 6, 2 }, // left
		};
		for (auto e : plane_vtx_id)
		{
			frustum_volume.Planes.Add(FPlane(fp[e.X], fp[e.Y], fp[e.Z]));
		}
		frustum_volume.Init();// Frustumセットアップ.
	}


	// Occupancy更新用のレイキャストサンプル.
	TArray<std::tuple<FVector, bool>> hit_samples;
	TArray<FVector> hit_samples_normal;
	{
		FCollisionQueryParams col_query_param = {};
		// プレイヤーアクターを無視.
		if (auto plc = GetWorld()->GetFirstPlayerController())
		{

			col_query_param.AddIgnoredActor(plc->GetPawnOrSpectator());
		}
#if 1
		{
			constexpr float k_cast_angle_center_exp = 1.8f;// Frustum内サンプルレイの画面中心密度増加用の指数 [0.0,].
			constexpr float k_cast_angle_rate = 0.98f;// Frustum内サンプルレイの画面中心基準の範囲係数 [0.0, 1.0].
			constexpr int k_per_frame_sample_count = 100;
			for (auto i = 0; i < k_per_frame_sample_count; ++i)
			{
				float raster_center_u = (FMath::FRand() * 2.0f) - 1.0;
				float raster_center_v = (FMath::FRand() * 2.0f) - 1.0;
				raster_center_u = FMath::Pow(FMath::Abs(raster_center_u), k_cast_angle_center_exp) * FMath::Sign(raster_center_u);
				raster_center_v = FMath::Pow(FMath::Abs(raster_center_v), k_cast_angle_center_exp) * FMath::Sign(raster_center_v);
				const auto dir_in_frustum = view_dir + (view_right * vx * raster_center_u*k_cast_angle_rate) + (view_up * vy * raster_center_v*k_cast_angle_rate);
				
				const auto ray_end = dir_in_frustum * 5000.0f + view_location;

				FHitResult hit_r;
				bool is_hit = false;
				// レイキャスト.
				if (GetWorld()->LineTraceSingleByChannel(hit_r, view_location, ray_end, ECollisionChannel::ECC_WorldStatic, col_query_param))
				{
					is_hit = true;
				}
				
				hit_samples.Add(std::make_tuple(is_hit ? hit_r.Location : ray_end, is_hit));
				hit_samples_normal.Add(is_hit ? hit_r.Normal : FVector::UnitX());
			}
		}
#else
		// 1サンプル.
		const auto ray_end = view_dir * 5000.0f + view_location;
		FHitResult hit_r;
		bool is_hit = false;
		// レイキャスト.
		if (GetWorld()->LineTraceSingleByChannel(hit_r, view_location, ray_end, ECollisionChannel::ECC_WorldStatic, col_query_param))
		{
			is_hit = true;
		}
		hit_samples.Add(std::make_tuple(is_hit ? hit_r.Location : ray_end, is_hit));
		hit_samples_normal.Add(is_hit ? hit_r.Normal : FVector::UnitX());
#endif
	}

	
	if(dcgrid_.IsInitialized())
	{
		using SamplePointType = decltype(dcgrid_)::SamplePointInfo;
		TArray<std::tuple<SamplePointType, bool>> hit_samples_dcgrid;

		for(int si = 0; si < hit_samples.Num(); ++si)
		{
			SamplePointType tmp;
			tmp.pos = std::get<0>(hit_samples[si]);
			tmp.dir = hit_samples_normal[si];
			hit_samples_dcgrid.Add(std::make_tuple(tmp, std::get<1>(hit_samples[si])));
		}
		
		dcgrid_.AppendElements(view_location, hit_samples_dcgrid);
		dcgrid_.UpdateSystem(DeltaTime);

		// パーティクルテスト.
		// パーティクルを投入テスト.
		//dcgrid_.AddParticleTest(view_location + view_dir * 100.0f + view_up * -0.0f + view_right * 50.0f, view_dir * 1000.0f);
		constexpr  float particle_spawn_half_extent = 1000.0f;
		const auto particle_spawn_offset = FVector(FMath::RandRange(-1.0f, 1.0f), FMath::RandRange(-1.0f, 1.0f), FMath::RandRange(-1.0f, 1.0f)) * particle_spawn_half_extent;
		dcgrid_.AddParticleTest(
			view_location + view_dir * particle_spawn_half_extent + particle_spawn_offset, view_dir * 100.0f);
		
		dcgrid_.UpdateParticle(DeltaTime);

		const auto k_dcgrid_flip = dcgrid_.FrontBufferIndex();

		// デバッグ表示.
		ismc_dcgrid_->ClearInstances();
		// パーティクル.
		if (debug_dcgrid_ptcl_)
		{
			for (auto i = 0; i < dcgrid_.particle_pool_flag_.Num(); ++i)
			{
				if (!dcgrid_.particle_pool_flag_[i])
					continue;

				FTransform tr(dcgrid_.particle_pool_[i].pos);
				tr.SetScale3D(FVector(0.2f));
				ismc_dcgrid_->AddInstance(tr, true);
			}
		}
		// dcgrid.
		if (debug_dcgrid_cell_)
		{
			const int mip_level = 0;
			// デバッグなのでメンバ直接アクセス.
			for (auto pooli = 0; pooli < dcgrid_.block_pool_[mip_level].sub_pool_.Num(); ++pooli)
			{
				auto* p_subpool = dcgrid_.block_pool_[mip_level].sub_pool_[pooli];
				for (auto i = 0; i < p_subpool->sub_pool.size(); ++i)
				{
					if (!p_subpool->sub_pool_used[i])
						continue;

					const auto& block = p_subpool->sub_pool[i];

					const auto block_pos_i = dcgrid_.CodeToVec3i(block.position_code);
					const auto block_pos = dcgrid_.BlockToWorld(mip_level, FVector(block_pos_i));
					const auto block_center_pos = block_pos + FVector(dcgrid_.k_block_width_ws * 0.5f);

					// RootCellのフラスタムカリング.
					if (!frustum_volume.IntersectBox(block_center_pos, FVector(dcgrid_.k_block_width_ws * 0.5f)))
						continue;

					assert(dcgrid_.k_PoolElemId_invalid != block.cell_brick_addr);
					// CellBrick.
					const auto& cell_brick = dcgrid_.cell_brick_pool_[mip_level].Get(block.cell_brick_addr);

					for (auto celli = 0; celli < dcgrid_.k_cell_brick_vol3d; ++celli)
					{
						const auto cell_vel = cell_brick.vel[k_dcgrid_flip][celli];
						const auto vel_len = cell_vel.Length();
						if (FMath::IsNearlyZero(vel_len))
							continue;


#if 1
						// 速度ベクトルの大きさによるスケール補正.
						const auto base_scale = FVector(1.0f, 0.1f, 0.1f);
						const auto cell_scale_rate = FMath::Lerp(0.09f, 1.0f, FMath::Clamp(vel_len / 90.0f, 0.0f, 1.0f));
						const auto vel_dir_quat = cell_vel.ToOrientationQuat();

						const FVector3f custom_data(cell_vel.GetSafeNormal().GetAbs());
#elif 1
						// 圧力.
						const auto pressure = cell_brick.work_pressure[dcgrid_.pressure_flip_][celli];

						// 発散量でスケール.
						const auto base_scale = FVector(1.0f, 1.0f, 1.0f);
						const auto cell_scale_rate = FMath::Lerp(0.09f, 1.0f, FMath::Clamp(FMath::Abs(pressure) / 1000.0f, 0.0f, 1.0f));
						const auto vel_dir_quat = FQuat::Identity;

						const FVector3f custom_data(FMath::Sign(pressure), -FMath::Sign(pressure), 0.0f);
#else
						const auto divergence = cell_brick.work_divergence[celli];

						// 発散量でスケール.
						const auto base_scale = FVector(1.0f, 1.0f, 1.0f);
						const auto cell_scale_rate = FMath::Lerp(0.09f, 1.0f, FMath::Clamp(FMath::Abs(divergence) / 100.0f, 0.0f, 1.0f));
						const auto vel_dir_quat = FQuat::Identity;

						const FVector3f custom_data(FMath::Sign(divergence), -FMath::Sign(divergence), 0.0f);
#endif

						const auto [cell_lx, cell_ly, cell_lz] = dcgrid_.LocalIndexToBrickLocalId(celli);
						const auto cell_center_pos = (FVector(cell_lx, cell_ly, cell_lz) + 0.5f) * FVector(dcgrid_.k_cell_width_ws) + block_pos;

						FTransform tr(cell_center_pos);
						tr.SetRotation(vel_dir_quat);
						tr.SetScale3D(base_scale * dcgrid_.k_cell_width_ws / 100.0f * 0.95f * cell_scale_rate);
						
						const auto inst_index = ismc_dcgrid_->AddInstance(tr, true);
						const auto cell_visualize_id = block_pos_i;
						ismc_dcgrid_->SetCustomData(inst_index, { custom_data.X, custom_data.Y, custom_data.Z});
					}
				}
			}
		}
	}
	

	if (ocgrid_.IsInitialized())
	{
		// パーティクルを投入テスト.
		ocgrid_.AddParticleTest(view_location + view_dir * 100.0f + view_up * -0.0f + view_right * 50.0f, view_dir * 1000.0f);

		// Occupancyをコリジョンレイで更新.
		ocgrid_.UpdateOccupancy(view_location, hit_samples);

		// デバッグパーティクルの更新.
		ocgrid_.UpdateParticle(DeltaTime);


		// デバッグ表示
		{
			// クリア.
			ismc_occupancygrid_->ClearInstances();
			

			// パーティクル可視化.
			if (debug_ocgrid_ptcl_)
			{
				for (auto i = 0; i < ocgrid_.particle_pool_flag_.Num(); ++i)
				{
					if (!ocgrid_.particle_pool_flag_[i])
						continue;

					FTransform tr(ocgrid_.particle_pool_[i].pos);
					tr.SetScale3D(FVector(0.2f));
					ismc_occupancygrid_->AddInstance(tr, true);
				}
			}

			// OccupancyGrid可視化.
			if (debug_draw_ocgrid_)
			{
				const auto grid_aabb_min_wgs = ocgrid_.bgrid_.grid_aabb_min_wgs_;

				// レイトレースヒット可視化.
				{
					constexpr float k_view_ray_trace_length = 2000.0f;
					FVector chit_pos, chit_normal;
					if (ocgrid_.TraceSingle(chit_pos, chit_normal, view_location, view_location + view_dir * k_view_ray_trace_length))
					{
						// View Rayのヒット位置にデバッグCube描画.
						const auto hit_pos_ws = chit_pos;

						const auto dist_to_hit_pos = FVector::Distance(chit_pos, view_location);
						bool is_out_of_range = k_view_ray_trace_length < dist_to_hit_pos;


						FTransform tr(hit_pos_ws);
						tr.SetScale3D(FVector(0.5f, 0.5f, 4.0f) * 0.2f);
						tr.SetRotation(FQuat::FindBetween(FVector::ZAxisVector, chit_normal));

						const auto inst_index = ismc_occupancygrid_->AddInstance(tr, true);
					}
				}

				if (true)
				{
					constexpr float k_cell_lod_base_distance = 9000.0f;// デバッグ表示LOD基準距離
					constexpr float k_cell_brick_draw_distance = k_cell_lod_base_distance * 0.25;// 最近接でBrick描画する距離.
					constexpr float k_cell_brick_detail_level_value = k_cell_lod_base_distance / k_cell_brick_draw_distance;

					auto AddBrickDrawInstance = [this](naga::GridCellAddrType cell_addr, float cell_size, FVector cell_min_pos_ws, FIntVector root_cell_id, FIntVector cur_cell_id, int cur_depth)
					{
						// Brick描画.
						const auto& brick = ocgrid_.bit_occupancy_brick_pool_[cell_addr];
						if (0 != brick.occupancy_4x4x4)
						{
							const float brick_elem_width = cell_size / 4.0f;
							auto bit_occupancy_444 = brick.occupancy_4x4x4;
							for (int bit_index = 0; 0 != bit_occupancy_444; bit_occupancy_444 = (bit_occupancy_444 >> 1), ++bit_index)
							{
								if (0 == (bit_occupancy_444 & 0x01))
									continue;

								int bxi = bit_index & 0b11;
								int byi = (bit_index >> 2) & 0b11;
								int bzi = (bit_index >> 4) & 0b11;
								const auto brick_center_pos_ls = (FVector(bxi, byi, bzi) + FVector(0.5f)) * brick_elem_width;
								const auto brick_center_pos_ws = brick_center_pos_ls + cell_min_pos_ws;

								FTransform tr(brick_center_pos_ws);
								tr.SetScale3D(FVector(brick_elem_width) / 100.0f * 0.95f);
								const auto inst_index = ismc_occupancygrid_->AddInstance(tr, true);
								const auto cell_visualize_id = FIntVector(root_cell_id.X, root_cell_id.Y, root_cell_id.Z) + cur_cell_id;
								ismc_occupancygrid_->SetCustomData(inst_index, { (float)cell_visualize_id.X, (float)cell_visualize_id.Y , (float)cell_visualize_id.Z });
							}
						}
					};

					// RootCellを巡回してカリングしながらデバッグ表示.
					for (auto zi = 0; zi < ocgrid_.bgrid_.k_root_grid_reso; ++zi)
					{
						for (auto yi = 0; yi < ocgrid_.bgrid_.k_root_grid_reso; ++yi)
						{
							for (auto xi = 0; xi < ocgrid_.bgrid_.k_root_grid_reso; ++xi)
							{
								const auto root_cell_id = FIntVector(xi, yi, zi);
								const auto root_cell_index = ocgrid_.bgrid_.CalcRootCellIndex(FIntVector(xi, yi, zi));
								// RootCell[xi,yi,zi]のMin座標.
								const FVector root_cell_min_pos_ws = ocgrid_.bgrid_.RootGridSpaceToWorld(FVector(root_cell_id));
								const FVector root_cell_center_pos_ws = root_cell_min_pos_ws + FVector(ocgrid_.bgrid_.root_cell_width_ * 0.5f);

								// RootCellのデータ有無で早期スキップ.
								const auto root_cell_data_addr = ocgrid_.bgrid_.root_cell_data_[root_cell_index];
								if (ocgrid_.k_invalid_u32 == root_cell_data_addr)
									continue;
								// RootCellのフラスタムカリング.
								if (!frustum_volume.IntersectBox(root_cell_center_pos_ws, FVector(ocgrid_.bgrid_.root_cell_width_ * 0.5f)))
									continue;
								// RootCellの中心までの距離で簡易カリング.
								if (FMath::Square(20000.0f) <= FVector::DistSquared(root_cell_center_pos_ws, view_location))
									continue;

								// RootCellのみの場合は描画して終了.
								if (0 == ocgrid_.k_multigrid_max_depth)
								{
									// Brick描画.
									AddBrickDrawInstance(root_cell_data_addr, ocgrid_.bgrid_.root_cell_width_, root_cell_min_pos_ws, root_cell_id, root_cell_id, 0);
									continue;
								}

								// RootCellのSubBlockがある場合は降下して描画.
								constexpr auto k_reso = decltype(ocgrid_)::k_child_cell_reso;
								constexpr auto k_cell_max = k_reso - 1;
								int depth = 1;
								int total_reso_scale = k_reso;
								FIntVector cell = FIntVector::ZeroValue;
								FIntVector cell_range = FIntVector(k_cell_max);
								for (;;)
								{
									const auto cell_global = root_cell_id * total_reso_scale + cell;
									const auto cell_size = ocgrid_.bgrid_.root_cell_width_ / total_reso_scale;
									const auto cell_min_pos_ws = ocgrid_.bgrid_.RootGridSpaceToWorld(FVector(cell_global) / total_reso_scale);
									const auto cell_center_pos_ws = cell_min_pos_ws + cell_size * 0.5f;
									const float cell_detail_level = k_cell_lod_base_distance / FVector::Dist(cell_center_pos_ws, view_location);
									const float cell_dist_inv = FMath::Pow(FMath::Min(1.0f, cell_detail_level), 1.0f / 2.0f);
									// セルの距離によって下層深度への移動を制限.(遠景では詳細セルまで下らない).
									const int cell_max_depth_lod = ocgrid_.k_multigrid_max_depth * cell_dist_inv;

									const auto [cell_data, cell_data_depth] = ocgrid_.GetGridCellData(depth, cell_global);
									if (cell_max_depth_lod > depth)
									{
										// デバッグ表示レベルより深度が浅く, Cellが子階層を持っている場合は降下.
										if (depth == cell_data_depth && ocgrid_.k_invalid_u32 != cell_data)
										{
											++depth;
											total_reso_scale *= k_reso;
											cell = cell * k_reso;
											cell_range = cell + FIntVector(k_cell_max);
											continue;
										}
									}

									// 描画.
									if (ocgrid_.k_invalid_u32 != cell_data)
									{
										if (k_cell_brick_detail_level_value > cell_detail_level)
										{
											// Cell描画.
											FTransform tr(cell_center_pos_ws);
											tr.SetScale3D(FVector(cell_size * 0.95f / 100.0f));
											const auto inst_index = ismc_occupancygrid_->AddInstance(tr, true);
											const auto cell_visualize_id = root_cell_id + cell_range;
											ismc_occupancygrid_->SetCustomData(inst_index, { (float)cell_visualize_id.X, (float)cell_visualize_id.Y , (float)cell_visualize_id.Z });
										}
										else if (ocgrid_.k_multigrid_max_depth == depth)
										{
											if(debug_draw_ocgrid_brick_)
											{
												// Brick描画.
												AddBrickDrawInstance(cell_data, cell_size, cell_min_pos_ws, root_cell_id, cell, depth);
											}
											else
											{
												// Cell描画.
												const auto& brick = ocgrid_.bit_occupancy_brick_pool_[cell_data];
												// Brick 64要素の一定以上が占有されていれば描画.
												if(0 != naga::math::BitCount(brick.occupancy_4x4x4))
												{
													const float occupancy_rate = static_cast<float>(naga::math::BitCount(brick.occupancy_4x4x4)) / 64.0f;
													
													FTransform tr(cell_center_pos_ws);
													tr.SetScale3D(FVector(cell_size * FMath::Lerp(0.1f, 0.7f, FMath::Clamp(occupancy_rate * 8.0f, 0.0f, 1.0f)) / 100.0f));
													const auto inst_index = ismc_occupancygrid_->AddInstance(tr, true);

													ismc_occupancygrid_->SetCustomData(inst_index, { occupancy_rate, 0.0f, 0.0f });
												}
											}
										}
									}

									if (0 >= (cell_range - cell).GetMax())
									{
										// 階層のCellをすべて巡回し終えたら上の階層で上昇. 複数階層の末端である可能性があるためループで必要なだけ上昇する.
										for (; 1 < depth;)
										{
											--depth;
											total_reso_scale /= k_reso;
											cell = cell / k_reso;
											cell_range = FIntVector(cell / k_reso) * k_reso + FIntVector(k_cell_max);
											if (0 < (cell_range - cell).GetMax())
												break;
										}
										if (0 >= (cell_range - cell).GetMax())
											break;
									}

									// 同階層でのCell移動.
									auto next_local_cell = cell - (cell_range - FIntVector(k_cell_max));
									++next_local_cell.X;
									if (k_reso <= next_local_cell.X) { next_local_cell.X = 0; ++next_local_cell.Y; }
									if (k_reso <= next_local_cell.Y) { next_local_cell.Y = 0; ++next_local_cell.Z; }

									// 移動.
									cell = next_local_cell + (cell_range - FIntVector(k_cell_max));
								}
							}
						}
					}
				}
			}
		}
	}
}




