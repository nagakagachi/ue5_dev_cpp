
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Containers/BitArray.h"

#include "physics/sparse_voxel_mpm.h"


#include "actor_mpm_sim.generated.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SparseVoxelによるMPM弾性体シミュレーションテスト.
UCLASS()
class NAGAEXPERIMENT_API ASparseGridTest : public AActor
{
	GENERATED_BODY()

public:
	ASparseGridTest();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;



	UFUNCTION(BlueprintCallable)
		void AddNode(const FVector& pos, const FVector& vel);

	UFUNCTION(BlueprintCallable)
		void Clear();

public:
	// デバッグ表示用メッシュなど
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		class UStaticMesh*	testStaticMesh_ = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		class UMaterial*	testMaterial_ = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		int num_init_particle_ = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool is_move_particle_ = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float particle_pos_init_range_x_ = 2000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float particle_pos_init_range_y_ = 2000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float particle_pos_init_range_z_ = 2000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float particle_pos_init_offset_z_ = 100.0f;


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float debug_plane_range_x_ = 3000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float debug_plane_range_y_ = 3000.0f;



	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float elastic_lambda_ = 16.0f;// 10.0f
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float elastic_mu_ = 2.5f;// 3.0f
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float initial_density_ = 0.3f;// 1.0f

protected:


private:
	class UInstancedStaticMeshComponent* ismc_ = nullptr;
	bool			need_update_ism_ = false;


	bool			is_dirty_ = false;
	TArray<FVector>	particle_position_;
	TArray<FVector>	particle_velocity_;

	TArray<naga::mpm::Mtx3x3> particle_affine_momentum_;
	TArray<naga::mpm::Mtx3x3> particle_deform_grad_;
	
	float			cell_size_ = 100.0f;

	naga::mpm::SparseVoxelTreeMpmSystem		sgs_;
};
