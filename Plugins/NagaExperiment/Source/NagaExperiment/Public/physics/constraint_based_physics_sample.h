// @author: @nagakagachi
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "util/entity_buffer.h"

#include "constraint_based_physics_sample.generated.h"


namespace naga
{
	// パーティクル管理
	class ConstraintBasedPhysicsParticleAllocator : public EntityAllocator
	{
	public:
		ConstraintBasedPhysicsParticleAllocator()
		{
		}
		~ConstraintBasedPhysicsParticleAllocator()
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
	class DistanceConstraintAlocator : public EntityAllocator
	{
	public:
		using ConstraintPair = TPair<ConstraintBasedPhysicsParticleAllocator::EntityID, ConstraintBasedPhysicsParticleAllocator::EntityID>;
		// パーティクルペアとその間の拘束距離
		using Constraint = TPair<ConstraintPair, float>;

		TArray<Constraint>	constraint_;

		// 前回の反復時の最終Lambda
		TArray<float>		prev_lambda_;
		// 前回の反復時の最終Lambda勾配 収束チェックのためと勾配のWarmStartとかを試すために保存
		TArray<float>		prev_lambda_grad_;

		//	パーティクルデータと拘束条件インデックスを受け取って該当する拘束条件の諸々の計算をして結果を返す
		//	戻り値は拘束条件インデックスに該当する拘束条件が使用中かどうかの真偽値. 拘束条件バッファの先頭から処理して使用中拘束条件を全て処理したら早期切り上げするために利用.
		bool CalcConstraint(const ConstraintBasedPhysicsParticleAllocator& particles, int constraint_index,
			TPair<int, int>& out_particle_indices, TPair<FVector, FVector>& out_jacobian,
			float& out_constraint_value, float& out_constraint_violation, TPair<float, float>& lambda_min_max, float*& out_prev_lambda_ptr, float*& out_prev_lambda_grad_ptr)
		{
			const float epsiolon = 1e-7f;
			const float projection_limit = 1e7f;
			//const float projection_limit = 1e9f;

			const auto id = GetElementEntityID(constraint_index);
			if (DistanceConstraintAlocator::InvalidID() == id)
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
	class ConstraintBasedPhysicsParticleSystem
	{
	public:
		using ParticleID = ConstraintBasedPhysicsParticleAllocator::EntityID;
		using DistansConstID = DistanceConstraintAlocator::EntityID;

		ConstraintBasedPhysicsParticleSystem();

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



		ConstraintBasedPhysicsParticleAllocator		particles_;
		DistanceConstraintAlocator distnce_const_;
	};
	
}


// Actor.
UCLASS()
class AConstraintBasedPhysicsTest : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AConstraintBasedPhysicsTest();

	UFUNCTION(BlueprintCallable)
		void AddDebugExForceSphere(const FVector& sphere_center, float sphere_radius, float intensity);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	virtual void OnConstruction(const FTransform& transform) override;
	// Called every frame
	virtual void Tick(float DeltaTime) override;

public:

	// 表示用メッシュ.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		class UStaticMesh*	testStaticMesh_;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		class UMaterial*	testMaterial_;

	// LCP Iteration count
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		int	param_constraint_physics_iteration_count_ = 7;

	// LCP Baumgarte rate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0"))
		float	param_constraint_physics_vioration_penarty_bias_rate = 0.25f;

	// LCP Momentum rate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float	param_constraint_physics_lcp_momentum_ = 0.75f;

	// LCP WarmStarting rate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float	param_constraint_physics_lcp_warm_start_rate_ = 0.0f;

	// LCP Grad WarmStarting rate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float	param_constraint_physics_lcp_grad_warm_start_rate_ = 1.0f;

private:
	class UInstancedStaticMeshComponent*	constraint_solve_inst_mesh_comp_;

	naga::ConstraintBasedPhysicsParticleSystem					physics_particle_system_;
};
