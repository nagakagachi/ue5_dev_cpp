// Fill out your copyright notice in the Description page of Project Settings.


#include "constraint_based_physics_sample.h"

#include "Runtime/Engine/Classes/Components/SphereComponent.h"




namespace naga
{
	ConstraintBasedPhysicsParticleSystem::ConstraintBasedPhysicsParticleSystem()
	{
	}
	ConstraintBasedPhysicsParticleSystem::ParticleID ConstraintBasedPhysicsParticleSystem::AddParticle(const FVector& pos, float mass)
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
	void ConstraintBasedPhysicsParticleSystem::RemoveParticle(ParticleID id)
	{
		int buffer_index = particles_.Delete(id);
	}

	ConstraintBasedPhysicsParticleSystem::DistansConstID ConstraintBasedPhysicsParticleSystem::AddDistanceConstraint(ParticleID p0, ParticleID p1, float distance)
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

		distnce_const_.constraint_[buffer_index] = DistanceConstraintAlocator::Constraint(DistanceConstraintAlocator::ConstraintPair(p0, p1), distance);

		// warm start値は0リセット. 毎フレームクリアしてそのフレームで有効な拘束を追加する場合は何らかの形でキャッシュしておいて検索する必要がある.
		distnce_const_.prev_lambda_[buffer_index] = 0.0f;
		distnce_const_.prev_lambda_grad_[buffer_index] = 0.0f;

		return newID;
	}
	void ConstraintBasedPhysicsParticleSystem::RemoveDistanceConstraint(DistansConstID id)
	{
		distnce_const_.Delete(id);
	}

	void ConstraintBasedPhysicsParticleSystem::SolveConstraint(float delta_sec, int iteration_count, float vioration_penarty_bias_rate, float momentum_rate,
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
				if (ConstraintBasedPhysicsParticleAllocator::InvalidID() != id)
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
			if (ConstraintBasedPhysicsParticleAllocator::InvalidID() != id)
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
			if (ConstraintBasedPhysicsParticleAllocator::InvalidID() != id)
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



// Sets default values
ANglConstraintBasedPhysicsTest::ANglConstraintBasedPhysicsTest()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	USphereComponent* rootComp = CreateDefaultSubobject<USphereComponent>("RootCollider");
	RootComponent = Cast<USceneComponent>(rootComp);
}

void ANglConstraintBasedPhysicsTest::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
}

// Called when the game starts or when spawned
void ANglConstraintBasedPhysicsTest::BeginPlay()
{
	Super::BeginPlay();

	// 表示用メッシュ.
	constraint_solve_inst_mesh_comp_ = NewObject<UInstancedStaticMeshComponent>(this);
	constraint_solve_inst_mesh_comp_->RegisterComponent();
	constraint_solve_inst_mesh_comp_->SetStaticMesh(testStaticMesh_);
	constraint_solve_inst_mesh_comp_->SetFlags(RF_Transactional);
	constraint_solve_inst_mesh_comp_->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	constraint_solve_inst_mesh_comp_->SetMaterial(0, testMaterial_);
	this->AddInstanceComponent(constraint_solve_inst_mesh_comp_);


#if 1
	// テスト用オブジェクトの配置座標.
	auto base_offset = FVector(-4000.0f, 1000.0f, 1000.0f);
	const auto base_offset_step = FVector(500.0f, 0.0f, 0.0f);

#if 1
	{

		auto p0 = physics_particle_system_.AddParticle(FVector(0, 0, 0) + base_offset, 0);
		auto p1 = physics_particle_system_.AddParticle(FVector(50, 0, 0) + base_offset, 1);

		auto c0 = physics_particle_system_.AddDistanceConstraint(p0, p1, 50);

		base_offset += base_offset_step;
	}
	{

		auto p0 = physics_particle_system_.AddParticle(FVector(0, 0, 0) + base_offset, 0);
		auto p1 = physics_particle_system_.AddParticle(FVector(50, 0, 0) + base_offset, 1);
		auto p2 = physics_particle_system_.AddParticle(FVector(100, 0, 0) + base_offset, 1);

		auto c0 = physics_particle_system_.AddDistanceConstraint(p0, p1, 50);
		auto c1 = physics_particle_system_.AddDistanceConstraint(p1, p2, 50);

		base_offset += base_offset_step;
	}
	{

		auto p0 = physics_particle_system_.AddParticle(FVector(0, 0, 0) + base_offset, 0);
		auto p1 = physics_particle_system_.AddParticle(FVector(50, 0, 0) + base_offset, 1);
		auto p2 = physics_particle_system_.AddParticle(FVector(100, 0, 0) + base_offset, 1);
		auto p3 = physics_particle_system_.AddParticle(FVector(150, 0, 0) + base_offset, 1);

		auto c0 = physics_particle_system_.AddDistanceConstraint(p0, p1, 50);
		auto c1 = physics_particle_system_.AddDistanceConstraint(p1, p2, 50);
		auto c2 = physics_particle_system_.AddDistanceConstraint(p2, p3, 50);

		base_offset += base_offset_step;
	}
	{

		auto p0 = physics_particle_system_.AddParticle(FVector(0, 0, 0) + base_offset, 0);
		auto p1 = physics_particle_system_.AddParticle(FVector(50, 0, 0) + base_offset, 1);
		auto p2 = physics_particle_system_.AddParticle(FVector(100, 0, 0) + base_offset, 1);
		auto p3 = physics_particle_system_.AddParticle(FVector(150, 0, 0) + base_offset, 1);
		auto p4 = physics_particle_system_.AddParticle(FVector(200, 0, 0) + base_offset, 1);

		auto c0 = physics_particle_system_.AddDistanceConstraint(p0, p1, 50);
		auto c1 = physics_particle_system_.AddDistanceConstraint(p1, p2, 50);
		auto c2 = physics_particle_system_.AddDistanceConstraint(p2, p3, 50);
		auto c3 = physics_particle_system_.AddDistanceConstraint(p3, p4, 50);

		base_offset += base_offset_step;
	}
	{

		auto p0 = physics_particle_system_.AddParticle(FVector(0, 0, 0) + base_offset, 0);
		auto p1 = physics_particle_system_.AddParticle(FVector(50, 0, 0) + base_offset, 1);
		auto p2 = physics_particle_system_.AddParticle(FVector(100, 0, 0) + base_offset, 1);
		auto p3 = physics_particle_system_.AddParticle(FVector(150, 0, 0) + base_offset, 1);
		auto p4 = physics_particle_system_.AddParticle(FVector(200, 0, 0) + base_offset, 1);

		auto p5 = physics_particle_system_.AddParticle(FVector(250, 0, 0) + base_offset, 1);
		auto p6 = physics_particle_system_.AddParticle(FVector(300, 0, 0) + base_offset, 1);
		auto p7 = physics_particle_system_.AddParticle(FVector(350, 0, 0) + base_offset, 1);
		auto p8 = physics_particle_system_.AddParticle(FVector(400, 0, 0) + base_offset, 1);
		auto p9 = physics_particle_system_.AddParticle(FVector(450, 0, 0) + base_offset, 1);

		auto c0 = physics_particle_system_.AddDistanceConstraint(p0, p1, 50);
		auto c1 = physics_particle_system_.AddDistanceConstraint(p1, p2, 50);
		auto c2 = physics_particle_system_.AddDistanceConstraint(p2, p3, 50);
		auto c3 = physics_particle_system_.AddDistanceConstraint(p3, p4, 50);
		auto c4 = physics_particle_system_.AddDistanceConstraint(p4, p5, 50);
		auto c5 = physics_particle_system_.AddDistanceConstraint(p5, p6, 50);
		auto c6 = physics_particle_system_.AddDistanceConstraint(p6, p7, 50);
		auto c7 = physics_particle_system_.AddDistanceConstraint(p7, p8, 50);
		auto c8 = physics_particle_system_.AddDistanceConstraint(p8, p9, 50);

		base_offset += base_offset_step;
	}
	{
		auto prev_id = physics_particle_system_.AddParticle(FVector(0, 0, 0) + base_offset, 0);
		for (int i = 0; i < 30; ++i)
		{
			auto cur_id = physics_particle_system_.AddParticle(FVector(50 * (i + 1), 0, 0) + base_offset, 1);

			auto c0 = physics_particle_system_.AddDistanceConstraint(prev_id, cur_id, 50);

			prev_id = cur_id;
		}
		base_offset += base_offset_step;
	}
	// カゴ形状セットアップ.
	{
		{
			auto p0 = physics_particle_system_.AddParticle(FVector(0, 0, 0) + base_offset, 0);
			auto p1 = physics_particle_system_.AddParticle(FVector(-500, 0, 559) + base_offset, 1);
			auto p2 = physics_particle_system_.AddParticle(FVector(-500, 500, -250) + base_offset, 1);
			auto p3 = physics_particle_system_.AddParticle(FVector(-500, -500, -250) + base_offset, 1);


			auto c0 = physics_particle_system_.AddDistanceConstraint(p0, p1, -1);
			auto c1 = physics_particle_system_.AddDistanceConstraint(p0, p2, -1);
			auto c2 = physics_particle_system_.AddDistanceConstraint(p0, p3, -1);


			auto c3 = physics_particle_system_.AddDistanceConstraint(p1, p2, -1);
			auto c4 = physics_particle_system_.AddDistanceConstraint(p2, p3, -1);
			auto c5 = physics_particle_system_.AddDistanceConstraint(p3, p1, -1);

			base_offset += base_offset_step;
		}
	}
#endif
#if 1
	// 連結カゴ形状セットアップ.
	{
		auto additional_offset = FVector(0, 0, 1000);

		naga::EntityAllocator::EntityID branch_root0;
		{

			auto p0 = physics_particle_system_.AddParticle(FVector(0, 0, 0) + base_offset + additional_offset, 0);
			auto p1 = physics_particle_system_.AddParticle(FVector(50, 0, 0) + base_offset + additional_offset, 1);
			auto p2 = physics_particle_system_.AddParticle(FVector(100, 0, 0) + base_offset + additional_offset, 1);
			auto p3 = physics_particle_system_.AddParticle(FVector(150, 0, 0) + base_offset + additional_offset, 1);
			auto p4 = physics_particle_system_.AddParticle(FVector(200, 0, 0) + base_offset + additional_offset, 1);

			auto p5 = physics_particle_system_.AddParticle(FVector(250, 0, 0) + base_offset + additional_offset, 1);
			auto p6 = physics_particle_system_.AddParticle(FVector(300, 0, 0) + base_offset + additional_offset, 1);
			auto p7 = physics_particle_system_.AddParticle(FVector(350, 0, 0) + base_offset + additional_offset, 1);
			auto p8 = physics_particle_system_.AddParticle(FVector(400, 0, 0) + base_offset + additional_offset, 1);
			auto p9 = physics_particle_system_.AddParticle(FVector(450, 0, 0) + base_offset + additional_offset, 1);

			auto c0 = physics_particle_system_.AddDistanceConstraint(p0, p1, 50);
			auto c1 = physics_particle_system_.AddDistanceConstraint(p1, p2, 50);
			auto c2 = physics_particle_system_.AddDistanceConstraint(p2, p3, 50);
			auto c3 = physics_particle_system_.AddDistanceConstraint(p3, p4, 50);
			auto c4 = physics_particle_system_.AddDistanceConstraint(p4, p5, 50);
			auto c5 = physics_particle_system_.AddDistanceConstraint(p5, p6, 50);
			auto c6 = physics_particle_system_.AddDistanceConstraint(p6, p7, 50);
			auto c7 = physics_particle_system_.AddDistanceConstraint(p7, p8, 50);
			auto c8 = physics_particle_system_.AddDistanceConstraint(p8, p9, 50);

			branch_root0 = p9;

			base_offset += base_offset_step;
		}

		{
			naga::EntityAllocator::EntityID branch_root1;
			{
				auto p1 = physics_particle_system_.AddParticle(FVector(559, 0, -500) + base_offset + additional_offset, 1);
				auto p2 = physics_particle_system_.AddParticle(FVector(-250, 500, -500) + base_offset + additional_offset, 1);
				auto p3 = physics_particle_system_.AddParticle(FVector(-250, -500, -500) + base_offset + additional_offset, 1);


				auto c0 = physics_particle_system_.AddDistanceConstraint(branch_root0, p1, -1);
				auto c1 = physics_particle_system_.AddDistanceConstraint(branch_root0, p2, -1);
				auto c2 = physics_particle_system_.AddDistanceConstraint(branch_root0, p3, -1);


				auto c3 = physics_particle_system_.AddDistanceConstraint(p1, p2, -1);
				auto c4 = physics_particle_system_.AddDistanceConstraint(p2, p3, -1);
				auto c5 = physics_particle_system_.AddDistanceConstraint(p3, p1, -1);


				branch_root1 = p3;

				base_offset += base_offset_step;
			}

			{
				auto p1 = physics_particle_system_.AddParticle(FVector(-500, 0, 559) + base_offset + additional_offset, 1);
				auto p2 = physics_particle_system_.AddParticle(FVector(-500, 500, -250) + base_offset + additional_offset, 1);
				auto p3 = physics_particle_system_.AddParticle(FVector(-500, -500, -250) + base_offset + additional_offset, 1);


				auto c0 = physics_particle_system_.AddDistanceConstraint(branch_root0, p1, -1);
				auto c1 = physics_particle_system_.AddDistanceConstraint(branch_root0, p2, -1);
				auto c2 = physics_particle_system_.AddDistanceConstraint(branch_root0, p3, -1);


				auto c3 = physics_particle_system_.AddDistanceConstraint(p1, p2, -1);
				auto c4 = physics_particle_system_.AddDistanceConstraint(p2, p3, -1);
				auto c5 = physics_particle_system_.AddDistanceConstraint(p3, p1, -1);

				base_offset += base_offset_step;
			}
		}
	}
#endif
#if 1
	// 連結かご形状セットアップ.
	{
		auto additional_offset = FVector(0, 0, 1000);
		naga::EntityAllocator::EntityID branch_root0;
		naga::EntityAllocator::EntityID branch_root1;
		naga::EntityAllocator::EntityID branch_root2;
		{
			auto p0 = physics_particle_system_.AddParticle(FVector(0, 0, 0) + base_offset + additional_offset, 0);
			auto p1 = physics_particle_system_.AddParticle(FVector(559, 0, -500) + base_offset + additional_offset, 1);
			auto p2 = physics_particle_system_.AddParticle(FVector(-250, 500, -500) + base_offset + additional_offset, 1);
			auto p3 = physics_particle_system_.AddParticle(FVector(-250, -500, -500) + base_offset + additional_offset, 1);


			auto c0 = physics_particle_system_.AddDistanceConstraint(p0, p1, -1);
			auto c1 = physics_particle_system_.AddDistanceConstraint(p0, p2, -1);
			auto c2 = physics_particle_system_.AddDistanceConstraint(p0, p3, -1);


			auto c3 = physics_particle_system_.AddDistanceConstraint(p1, p2, -1);
			auto c4 = physics_particle_system_.AddDistanceConstraint(p2, p3, -1);
			auto c5 = physics_particle_system_.AddDistanceConstraint(p3, p1, -1);

			branch_root0 = p1;
			branch_root1 = p2;
			branch_root2 = p3;

			base_offset += base_offset_step;
		}

		{
			auto local_offset = FVector(0, 0, -500);

			auto p1 = physics_particle_system_.AddParticle(FVector(559, 0, -500) + local_offset + base_offset + additional_offset, 1);
			auto p2 = physics_particle_system_.AddParticle(FVector(-250, 500, -500) + local_offset + base_offset + additional_offset, 1);
			auto p3 = physics_particle_system_.AddParticle(FVector(-250, -500, -500) + local_offset + base_offset + additional_offset, 1);


			auto c0 = physics_particle_system_.AddDistanceConstraint(branch_root0, p1, -1);
			auto c1 = physics_particle_system_.AddDistanceConstraint(branch_root0, p2, -1);
			auto c2 = physics_particle_system_.AddDistanceConstraint(branch_root0, p3, -1);


			auto c3 = physics_particle_system_.AddDistanceConstraint(p1, p2, -1);
			auto c4 = physics_particle_system_.AddDistanceConstraint(p2, p3, -1);
			auto c5 = physics_particle_system_.AddDistanceConstraint(p3, p1, -1);
		}
#if 1
		{
			auto local_offset = FVector(0, 0, -500);

			auto p1 = physics_particle_system_.AddParticle(FVector(559, 0, -500) + local_offset + base_offset + additional_offset, 1);
			auto p2 = physics_particle_system_.AddParticle(FVector(-250, 500, -500) + local_offset + base_offset + additional_offset, 1);
			auto p3 = physics_particle_system_.AddParticle(FVector(-250, -500, -500) + local_offset + base_offset + additional_offset, 1);


			auto c0 = physics_particle_system_.AddDistanceConstraint(branch_root1, p1, -1);
			auto c1 = physics_particle_system_.AddDistanceConstraint(branch_root1, p2, -1);
			auto c2 = physics_particle_system_.AddDistanceConstraint(branch_root1, p3, -1);


			auto c3 = physics_particle_system_.AddDistanceConstraint(p1, p2, -1);
			auto c4 = physics_particle_system_.AddDistanceConstraint(p2, p3, -1);
			auto c5 = physics_particle_system_.AddDistanceConstraint(p3, p1, -1);
		}
		{
			auto local_offset = FVector(0, 0, -500);

			auto p1 = physics_particle_system_.AddParticle(FVector(559, 0, -500) + local_offset + base_offset + additional_offset, 1);
			auto p2 = physics_particle_system_.AddParticle(FVector(-250, 500, -500) + local_offset + base_offset + additional_offset, 1);
			auto p3 = physics_particle_system_.AddParticle(FVector(-250, -500, -500) + local_offset + base_offset + additional_offset, 1);


			auto c0 = physics_particle_system_.AddDistanceConstraint(branch_root2, p1, -1);
			auto c1 = physics_particle_system_.AddDistanceConstraint(branch_root2, p2, -1);
			auto c2 = physics_particle_system_.AddDistanceConstraint(branch_root2, p3, -1);


			auto c3 = physics_particle_system_.AddDistanceConstraint(p1, p2, -1);
			auto c4 = physics_particle_system_.AddDistanceConstraint(p2, p3, -1);
			auto c5 = physics_particle_system_.AddDistanceConstraint(p3, p1, -1);
		}
#endif

		base_offset += base_offset_step;
	}
#endif

#endif
}

// Called every frame
void ANglConstraintBasedPhysicsTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);


	const auto k_delta_sec = 1.0f / 60.0f; // DeltaTime;


	physics_particle_system_.SolveConstraint(
		k_delta_sec,
		param_constraint_physics_iteration_count_,
		param_constraint_physics_vioration_penarty_bias_rate,
		param_constraint_physics_lcp_momentum_,
		param_constraint_physics_lcp_warm_start_rate_,
		param_constraint_physics_lcp_grad_warm_start_rate_);


	// 必要分インスタンス
	int numExistInstance = constraint_solve_inst_mesh_comp_->GetNumRenderInstances();
	// 距離高速の可視化用
	int check_enable_cnt = 0;
	for (int i = 0; i < physics_particle_system_.distnce_const_.NumMaxElement() && check_enable_cnt < physics_particle_system_.distnce_const_.NumEnableElement(); ++i)
	{
		// 距離高速ID
		auto constraint_id = physics_particle_system_.distnce_const_.GetElementEntityID(i);
		if (naga::EntityAllocator::InvalidID() == constraint_id)
			continue;

		// 距離拘束
		auto& constraint_instance = physics_particle_system_.distnce_const_.constraint_[i];

		// 距離高速を構成するパーティクルのID
		auto p0 = constraint_instance.Key.Key;
		auto p1 = constraint_instance.Key.Value;
		auto const_distance = constraint_instance.Value;

		// 距離拘束セットアップ
		auto p0_i = physics_particle_system_.particles_.GetBufferIndex(p0);
		auto p1_i = physics_particle_system_.particles_.GetBufferIndex(p1);

		if (0 > p0_i || 0 > p1_i)
			continue;

		auto v0 = physics_particle_system_.particles_.position_[p0_i];
		auto v1 = physics_particle_system_.particles_.position_[p1_i];

		float	len = 0.0f;
		FVector	dir = FVector::ZeroVector;
		(v1 - v0).ToDirectionAndLength(dir, len);
		if (1e-6 >= len)
			continue;

		// Actorローカル座標からワールドへ.
		FTransform trans((v0 + v1)*0.5f + GetActorLocation());

		// 表示用の長さ
		trans.SetScale3D(FVector(const_distance*0.9 / 100.0f, 0.18, 0.18));

		trans.SetRotation(dir.Rotation().Quaternion());

		if (numExistInstance <= i)
		{
			constraint_solve_inst_mesh_comp_->AddInstance(trans);
		}
		else
		{
			constraint_solve_inst_mesh_comp_->UpdateInstanceTransform(i, trans);
		}
	}
	constraint_solve_inst_mesh_comp_->MarkRenderStateDirty();
}

void ANglConstraintBasedPhysicsTest::AddDebugExForceSphere(const FVector& sphere_center, float sphere_radius, float intensity)
{
	physics_particle_system_.AddDebugExForceSphere(FSphere(sphere_center - GetActorLocation(), sphere_radius), intensity);
}
