// Fill out your copyright notice in the Description page of Project Settings.


#include "actor_mpm_sim.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"

#include <chrono>
#include <cmath>


// ----------------------------------------------------------------------------------------------------------------
	static FIntVector FloorToIntV3(const FVector& v)
	{
		return FIntVector(FMath::FloorToInt(v.X), FMath::FloorToInt(v.Y), FMath::FloorToInt(v.Z));
	}

//--------------------------------------------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------------------------------------------
ASparseGridTest::ASparseGridTest()
{
	PrimaryActorTick.bCanEverTick = true;


	// 可視化用のInstanceMeshコンポーネント生成
	ismc_ = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VisualizeMeshComponent"));
	ismc_->SetFlags(RF_Transactional);
	ismc_->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ismc_->SetCastShadow(true);
	ismc_->SetupAttachment(RootComponent);
}

void ASparseGridTest::BeginPlay()
{
	Super::BeginPlay();

	Clear();
	
	{
		naga::mpm::SparseVoxelTreeMpmSystem::Desc desc;
		// ある程度動作するデフォルト値を設定.
		desc = naga::mpm::SparseVoxelTreeMpmSystem::GetDefaultDesc();
		desc.voxel_size = cell_size_;
		desc.debug_initial_density = initial_density_;
		desc.debug_debug_elastic_lambda = elastic_lambda_;
		desc.debug_debug_elastic_mu = elastic_mu_;

		sgs_.Initialize(desc);
	}


	{
#if 0
		// 初期に適当に配置
		const auto range_x = particle_pos_init_range_x_;// 100.0f * 50.0f;
		const auto range_y = particle_pos_init_range_y_;// 100.0f * 50.0f;
		const auto range_z = particle_pos_init_range_z_;// 100.0f * 50.0f;
		{
			for (auto i = 0; i < num_init_particle_; ++i)
			{
				const auto rx = FMath::RandRange(-range_x, range_x);
				const auto ry = FMath::RandRange(-range_y, range_y);
				//const auto rz = FMath::RandRange(-range_z, range_z);
				const auto rz = FMath::RandRange(particle_pos_init_offset_z_, range_z + (range_z + particle_pos_init_offset_z_));



				const auto rvx = FMath::RandRange(-100.0f, 100.0f);
				const auto rvy = FMath::RandRange(-100.0f, 100.0f);
				const auto rvz = FMath::RandRange(-100.0f, 100.0f);

				AddNode(FVector(rx, ry, rz), FVector(rvx, rvy, rvz));
			}
		}
#else

		const auto func_gen_box = [this](const FVector& base_pos, const float particle_space, const FIntVector& num_axis_particle)
		{
			for (int bz = 0; bz < num_axis_particle.Z; ++bz)
			{
				for (int by = 0; by < num_axis_particle.Y; ++by)
				{
					for (int bx = 0; bx < num_axis_particle.X; ++bx)
					{
						const auto bp = base_pos + FVector(bx, by, bz) * particle_space;
						AddNode(bp, FVector(0.0f));
					}
				}
			}
		};

		const auto base_pos = FVector(-6000.0f, -6000.0f, 0.0f);
		for (auto k = 0; k < 1; ++k)
		{
			for (auto j = 0; j < 4; ++j)
			{
				for (auto i = 0; i < 4; ++i)
				{
					const auto bpos = base_pos + FVector(550.0f * i, 550.0f * j, 200.0f + 550.0f * k);
					const auto noise_offset = FVector(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f)) * 100.0f;
					func_gen_box(bpos + noise_offset, 50.0f, FIntVector(3, 3, 64));
				}
			}
		}

#endif
	}

	// Set Mesh and Material.
	ismc_->SetStaticMesh(testStaticMesh_);
	ismc_->SetMaterial(0, testMaterial_);


	// Add Default Instance
	FTransform isttr = {};
	ismc_->AddInstance(isttr);
}

void ASparseGridTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//const auto sim_delta_sec = DeltaTime;
	//const auto sim_delta_sec = 1.0f / 60.0f;
	const auto sim_delta_sec = 1.0f / 120.0f;

	{

		auto	progress_time_start = std::chrono::system_clock::now();


		// Gridビルド
		sgs_.Build(particle_position_);
		// GridBrickクリア
		sgs_.ClearBrickData();
		
		// 質量ラスタライズ
		sgs_.RasterizeAndUpdateGrid(sim_delta_sec, particle_position_, particle_velocity_, particle_affine_momentum_, particle_deform_grad_);

		if(false)
		{
			size_t progress_time_ms;
			progress_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - progress_time_start).count();
			UE_LOG(LogTemp, Display, TEXT("SGT Total: %d [micro sec]"), progress_time_ms);
		
			{
				const auto& pool_ = sgs_.pool_;
				const auto& mass_brick_pool_ = sgs_.mass_brick_pool_;

				// 使用メモリ
				size_t total_alloced_mem_size = 0;
				size_t node_alloced_mem_size = 0;
				size_t brick_alloced_mem_size = 0;
				node_alloced_mem_size = pool_.GetAllocatedMemorySize();
				brick_alloced_mem_size = mass_brick_pool_.GetAllocatedMemorySize();
				total_alloced_mem_size = node_alloced_mem_size + brick_alloced_mem_size;

				UE_LOG(LogTemp, Display, TEXT("SGT AllocatedMem Size (Total): %d [byte], %.2f [M byte]"), total_alloced_mem_size, float(total_alloced_mem_size) / (1024.0f*1024.0f));
				UE_LOG(LogTemp, Display, TEXT("SGT AllocatedMem Size (Node) : %d [byte], %.2f [M byte]"), node_alloced_mem_size, float(node_alloced_mem_size) / (1024.0f*1024.0f));
				UE_LOG(LogTemp, Display, TEXT("SGT AllocatedMem Size (Brick): %d [byte], %.2f [M byte]"), brick_alloced_mem_size, float(brick_alloced_mem_size) / (1024.0f*1024.0f));
			}
		}
	}

	// デバッグ描画
	if(true)
	{
		ismc_->ClearInstances();

		const auto mesh_size = testStaticMesh_->GetBoundingBox().GetExtent();

#if 0
		// リーフノードの可視化
		const auto num_level = sgs_.NumLevel();
		const auto leaf_level_index = num_level - 1;
		const auto leaf_level_info = sgs_.GetLevelInfo(leaf_level_index);
		const float leaf_node_size = leaf_level_info.cell_world_size * leaf_level_info.node_reso;

		auto& pool = sgs_.GetNodePool();
		auto num_leaf_handle_max = pool.NumLevelNodeMax(leaf_level_index);
		for (auto i = 0u; i < num_leaf_handle_max; ++i)
		{
			if (auto* leaf_node = pool.GetLevelNodeDirect(leaf_level_index, i))
			{
				// 存在するリーフノードのベース位置に適当なキューブ配置.
				auto leaf_node_base_pos = FVector(leaf_node->base_voxel_ipos * cell_size_) + FVector(leaf_node_size * 0.5f) + sgs_.GetSystemAabbMin();

				FTransform tr{};
				tr.SetTranslation(leaf_node_base_pos);
				tr.SetScale3D(FVector(leaf_node_size*0.45f) / mesh_size);
				ismc_->AddInstanceWorldSpace(tr);
			}
		}
#elif 0
		// Voxelの可視化
		const auto num_level = sgs_.NumLevel();
		const auto leaf_level_index = num_level - 1;
		const auto leaf_level_info = sgs_.GetLevelInfo(leaf_level_index);
		const float leaf_node_size = leaf_level_info.cell_world_size * leaf_level_info.node_reso;
		const auto mesh_scale_base = cell_size_ * 0.5f / mesh_size.X;

		const auto& pool = sgs_.pool_;
		const auto& brick_pool = sgs_.mass_brick_pool_;
		auto num_leaf_handle_max = pool.NumLevelNodeMax(leaf_level_index);
		for (auto i = 0u; i < num_leaf_handle_max; ++i)
		{
			if (auto* leaf_node = pool.GetLevelNodeDirect(leaf_level_index, i))
			{
				const auto* brick = brick_pool.Get(leaf_node->brick_handle);

				auto leaf_node_base_pos = FVector(leaf_node->base_voxel_ipos * cell_size_) + FVector(cell_size_ * 0.5f) + sgs_.GetSystemAabbMin();

				for (auto bz = 0u; bz < leaf_level_info.node_reso; ++bz)
				{
					for (auto by = 0u; by < leaf_level_info.node_reso; ++by)
					{
						for (auto bx = 0u; bx < leaf_level_info.node_reso; ++bx)
						{
							const auto brick_elem_idx = (bx + 1) + (by + 1) * sgs_.brick_reso_include_apron_ + (bz + 1) * sgs_.brick_reso_include_apron_*sgs_.brick_reso_include_apron_;
							const auto& brick_elem = brick[brick_elem_idx];
							if (0 < brick_elem.W)
							{
								FTransform tr{};
								tr.SetTranslation(leaf_node_base_pos + FVector(bx, by, bz) * cell_size_);
								tr.SetScale3D(mesh_scale_base * FVector( FMath::Sqrt( FMath::Min(1.0f, brick_elem.W * 2.0f)) ) );
								ismc_->AddInstanceWorldSpace(tr);
							}
						}
					}
				}
			}
		}
#else
		// パーティクル描画
		for (const auto& e : particle_position_)
		{
			FTransform tr{};
			tr.SetTranslation(e);
			
			//tr.SetScale3D(FVector(cell_size_ * 0.5f) / mesh_size);
			tr.SetScale3D(FVector(cell_size_ * 0.5f * 0.5f) / mesh_size);
			
			ismc_->AddInstance(tr, true);
		}
#endif
	}
}

void ASparseGridTest::AddNode(const FVector& pos, const FVector& vel)
{
	particle_position_.Push(pos);
	particle_velocity_.Push(vel);
	particle_affine_momentum_.Push(naga::mpm::Mtx3x3::Zero());
	particle_deform_grad_.Push(naga::mpm::Mtx3x3::Identity());
	is_dirty_ = true;
}

void ASparseGridTest::Clear()
{
	particle_position_.Empty();
	particle_velocity_.Empty();
	particle_affine_momentum_.Empty();
	particle_deform_grad_.Empty();
	is_dirty_ = true;
}





