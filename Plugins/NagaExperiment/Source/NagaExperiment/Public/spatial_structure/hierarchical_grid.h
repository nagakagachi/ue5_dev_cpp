#pragma once

/*
	RayTrace機能付きの階層グリッド構造

	階層無しと階層化バージョン.

	Raytrace中の階層移動やヒット処理はTemplateで与える.
	
*/

#include "Math/IntVector.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Box.h"

#include <cstdint>
#include <assert.h>
#include <chrono>
#include <array>
#include <tuple>
#include <bitset>
#include <unordered_map>

#include "util/math_util.h"

namespace naga
{
	// GridRayTraceのRayに関するUniformパラメータ.
	struct GridRayTraceRayUniform
	{
		GridRayTraceRayUniform() = default;
		GridRayTraceRayUniform(
			FVector		_ray_origin,
			float		_ray_length,
			FVector		_ray_dir,
			FVector		_ray_dir_inv
		)
			: ray_origin(_ray_origin), ray_length(_ray_length), ray_dir	(_ray_dir), ray_dir_inv(_ray_dir_inv)
		{}

		FVector		ray_origin;		// Ray始点 (Grid Space).
		float		ray_length;		// Ray長さ (Grid Space).
		FVector		ray_dir;		// Ray方向 normalized (Grid Space).
		FVector		ray_dir_inv;	// Ray方向逆数(Grid Space).
	};
	// GridRayTraceの訪問Cellに関するUniformパラメータ.
	struct GridRayTraceVisitCellUniform
	{
		GridRayTraceVisitCellUniform() = default;
		GridRayTraceVisitCellUniform(
			FIntVector	_cell_id,
			int			_depth,
			int			_resolution_per_root_cell,
			float		_ray_t,
			uint32_t	_root_cell_data)
			: cell_id(_cell_id), depth(_depth), resolution_per_root_cell(_resolution_per_root_cell), ray_t(_ray_t), root_cell_data(_root_cell_data)
		{}

		FIntVector	cell_id;					// 訪問CellID. Grid領域でローカル, depthに応じた解像度でのCellID.
		int			depth;						// 現在のDepth. 0 がルート.
		int			resolution_per_root_cell;	// 到達CellのRootCellあたりの解像度. RootCell一つが4x4x4分割されていてその一つに到達した場合は 4 が設定される.
		float		ray_t;						// RayとCellの交点のt値 [0.0, 1.0]. GridRayTraceRayUniform::ray_length に対する割合.
		uint32_t	root_cell_data;				// 現在のCellが所属するRootCellの任意データ.
	};

	
	// デフォルトのCellヒット処理, Payloadの定義.
	//	必要に応じて同様のシグネチャでヒット処理を実装する.
	struct DefaultTraceCellHitProcess
	{
		struct Payload
		{
			// トレース中に更新するヒット座標.
			FVector hit_pos;
			// トレース中に更新するt値.
			float	ray_t = FLT_MAX;
		};
		// Cellとのヒット処理. システムからレイの基本情報とトレース対象のCell情報, レイのPayloadを受け取って判定やPayload更新をする.
		bool operator()(const GridRayTraceRayUniform& ray_uniform, const GridRayTraceVisitCellUniform& visit_cell_param, Payload& ray_payload)
		{
			if (visit_cell_param.root_cell_data == ~0u)
				return false;

			if (ray_payload.ray_t <= visit_cell_param.ray_t)
				return false;
			// t 更新.
			ray_payload.ray_t = visit_cell_param.ray_t;
			// ヒット位置更新. ここではGrid空間座標.
			ray_payload.hit_pos = ray_uniform.ray_origin + ray_uniform.ray_dir * ray_uniform.ray_length * ray_payload.ray_t;
			// trueを返してトレース終了.
			// DDAによるNear-Far順トレースであるため早期棄却しても良いはず.
			return true;
		}
	};

	// デフォルトの階層Gridの深度移動チェック処理.
	//	必要に応じて同様のシグネチャで深度移動チェック処理を実装する.
	struct DefaultTraceCellDepthDescendingChecker
	{
		// Cellとのヒット処理. システムからレイの基本情報とトレース対象のCell情報, レイのPayloadを受け取って判定やPayload更新をする.
		constexpr bool operator()(const GridRayTraceRayUniform& ray_uniform, const GridRayTraceVisitCellUniform& visit_cell_param)
		{
			// デフォルトでは深度移動は無し.
			return false;
		}
	};

	// Raytrace Grid構造. 階層無しフラットバージョン.
	// DDA によるトレース.
	//	固定解像度(template).
	//	Trace時のCell訪問処理を関数オブジェクトでカスタマイズ可能.
	//	RootCell毎にu32データを保持してTrace中の訪問Cell処理に引数で渡す.
	template<int GridReso = 8>
	struct RayTracingGrid
	{
	public:
		static constexpr int k_root_grid_reso = GridReso;
		static constexpr float k_cell_width_default = 400.0f;

	public:
		// Per Cell Data.
		// NOTE. メンバとして直接確保するとこのクラスを持つUObjectについてUnrealHeaderToolで自動生成されるコード側でビルドエラーが発生する(警告だがエラー扱い).
		std::vector<uint32_t> root_cell_data_;

		float root_cell_width_ = k_cell_width_default;
		float root_cell_width_inv_ = 1.0f / k_cell_width_default;
	
		// Grid Center Position WorldSpace.
		FVector		grid_center_pos_ws_ = {};
		// Grid Space AABB Min Position WorldSpace.
		FVector		grid_aabb_min_ws_ = {};

		// Grid AABB Min Postion WorldGridSpace.
		FIntVector	grid_aabb_min_wgs_ = {};
	
	public:
		RayTracingGrid()
		{
			root_cell_data_.resize(k_root_grid_reso * k_root_grid_reso * k_root_grid_reso);
			Reset();
		}
		bool Initialize(float cell_width = k_cell_width_default)
		{
			root_cell_width_ = cell_width;
			root_cell_width_inv_ = 1.0f / cell_width;

			// 移動によるシフトコピーなどは後で.
			grid_center_pos_ws_ = {};
			grid_aabb_min_wgs_ = math::FVectorFloorToInt(grid_center_pos_ws_ * root_cell_width_inv_ - (k_root_grid_reso / 2.0f));
			grid_aabb_min_ws_ = FVector(grid_aabb_min_wgs_) * root_cell_width_;
		
			Reset();

			return true;
		}
		void Reset()
		{
			std::fill_n(root_cell_data_.begin(), root_cell_data_.size(), ~0u);
		}
		// World to RootGridSpace.
		constexpr FVector WorldToRootGridSpace(const FVector& world_position) const
		{
			return world_position * root_cell_width_inv_ - FVector(this->grid_aabb_min_wgs_);// なるべく小さい桁且つ整数ベースで計算.
		}
		// RootGridSpace to World.
		constexpr FVector RootGridSpaceToWorld(const FVector& root_grid_position) const
		{
			return (root_grid_position + FVector(this->grid_aabb_min_wgs_)) * root_cell_width_;
		}
		constexpr bool IsInner(const FIntVector cell) const
		{
			return math::IsInnerWithPositive(cell, FIntVector(k_root_grid_reso - 1));
		}
		constexpr int CalcRootCellIndex(const FIntVector& root_cell_id) const
		{
			return root_cell_id.X + (root_cell_id.Y * k_root_grid_reso) + (root_cell_id.Z * k_root_grid_reso * k_root_grid_reso);
		}

		// DDAによるトレース. Cell到達順はRay始点に近い順.
		//	FINE_STEP_ON_DIAGONAL_CASE : レイ方向が完全に対角線で各軸要素が同じ場合, セル移動ステップで接触するセルを厳密に処理するか. 
		//									セル中心からずれた (1,1,1)向きのレイの場合などでこのフラグがfalseの場合は本来は角がヒットしているセルをスキップしてしまう.
		//	ray_origin		: レイ始点ワールド座標.
		//	ray_end			: レイ終点ワールド座標.
		//	inout_payload	: レイ毎に更新されるPayloadデータ.
		//	hit_cell_process: Cellでのヒット処理を実行する関数オブジェクト. RayUniform,VisitUniform,Payloadを受け取り, Payloadの更新してトレースを終了するならtrueを返す.
		template<bool FINE_STEP_ON_DIAGONAL_CASE = true, typename TraceCellHitProcessType = DefaultTraceCellHitProcess, typename PayloadType = TraceCellHitProcessType::Payload>
		void TraceSimpleDDA(const FVector& ray_origin, const FVector& ray_end, PayloadType& inout_payload, TraceCellHitProcessType hit_cell_process = {}) const
		{
			// 内部用関数.
			constexpr auto CalcSafeDirInverse = [](const FVector& ray_dir) -> FVector
			{
				return FVector((FMath::IsNearlyZero(ray_dir.X)) ? FLT_MAX : 1.0f / ray_dir.X, (FMath::IsNearlyZero(ray_dir.Y)) ? FLT_MAX : 1.0f / ray_dir.Y, (FMath::IsNearlyZero(ray_dir.Z)) ? FLT_MAX : 1.0f / ray_dir.Z);
			};
			// 内部用関数.
			constexpr auto CalcClampedRay = [](const FVector& aabb_min, const FVector& aabb_max, const FVector& ray_begin, const FVector& ray_end)
				-> std::tuple<bool, FVector,FVector>
			{
				const FVector ray_d_c = ray_end - ray_begin;
				const FVector ray_dir_c = ray_d_c.GetSafeNormal();
				const float ray_len_c = ray_dir_c.Dot(ray_d_c);

				// Inv Dir.
				const FVector ray_dir_c_inv = FVector((FMath::IsNearlyZero(ray_dir_c.X)) ? FLT_MAX : 1.0f / ray_dir_c.X, (FMath::IsNearlyZero(ray_dir_c.Y)) ? FLT_MAX : 1.0f / ray_dir_c.Y, (FMath::IsNearlyZero(ray_dir_c.Z)) ? FLT_MAX : 1.0f / ray_dir_c.Z);

				const FVector t_to_min = (aabb_min - ray_begin) * ray_dir_c_inv;
				const FVector t_to_max = (aabb_max - ray_begin) * ray_dir_c_inv;
				const auto t_near = FVector::Min(t_to_min, t_to_max).GetMax();
				const auto t_far = FVector::Max(t_to_min, t_to_max).GetMin();

				// GridBoxとの交点が存在しなければ早期終了.
				// t_farが負-> 遠方点から外向きで外れ, t_farよりt_nearのほうが大きい->直線が交差していない, t_nearがレイの長さより大きい->届いていない.
				if (0.0f > t_far || t_near >= t_far || ray_len_c < t_near)
					return std::make_tuple(false, FVector{}, FVector{});

				// Grid内にクランプしたトレース始点終点.
				// 以降はこの始点終点で処理.
				const FVector ray_s_clamped_c = (0.0 > t_near) ? ray_begin : ray_begin + t_near * ray_dir_c;
				const FVector ray_e_clamped_c = (ray_len_c < t_far) ? ray_end : ray_begin + t_far * ray_dir_c;
				return std::make_tuple(true, ray_s_clamped_c, ray_e_clamped_c);
			};


			// 始点がGrid内部から開始している仮定で検証中.

			const FVector grid_box_min = FVector::ZeroVector;
			const FVector grid_box_max = FVector(k_root_grid_reso);
			const FIntVector grid_box_cell_max = FIntVector(k_root_grid_reso - 1);

			const FVector ray_begin_c = (ray_origin * root_cell_width_inv_) - FVector(grid_aabb_min_wgs_);
			const FVector ray_end_c = (ray_end * root_cell_width_inv_) - FVector(grid_aabb_min_wgs_);
		
			// Grid空間内にクランプした始点終点と交差判定取得.
			const auto [is_hit, ray_p0_clamped_c, ray_p1_clamped_c] = CalcClampedRay(grid_box_min, grid_box_max, ray_begin_c, ray_end_c);
			if (!is_hit)
				return;

			const auto ray_d_c = ray_p1_clamped_c - ray_p0_clamped_c;
			const auto ray_dir_c = ray_d_c.GetSafeNormal();
			const auto ray_d_c_len = ray_dir_c.Dot(ray_d_c);
			const auto ray_d_c_len_inv = 1.0 / ray_d_c_len;

			// Inv Dir.
			const auto ray_dir_c_inv = CalcSafeDirInverse(ray_dir_c);
			const auto dir_sign = ray_dir_c.GetSignVector();// +1 : positive component or zero, -1 : negative component.
			const auto cell_delta = dir_sign * ray_dir_c_inv;


			// 0ベースでのトラバースCell範囲.
			const FIntVector trace_begin_cell = math::FIntVectorMin(math::FVectorFloorToInt(ray_p0_clamped_c), grid_box_cell_max);
			const FIntVector trace_end_cell = math::FIntVectorMin( math::FVectorFloorToInt(ray_p1_clamped_c), grid_box_cell_max);
			const FIntVector trance_cell_range = math::FIntVectorAbs(trace_end_cell - trace_begin_cell);


			// 始点からの最初のステップt.
			const FVector cell_delta_offset = ((math::FVectorFloor(ray_p0_clamped_c) + FVector::Max(dir_sign, FVector::ZeroVector) - ray_p0_clamped_c) * ray_dir_c_inv).GetAbs();

			// トレースUniformパラメータ.
			const GridRayTraceRayUniform ray_uniform(ray_p0_clamped_c, ray_d_c_len, ray_dir_c, ray_dir_c_inv);

			FIntVector total_cell_step = FIntVector::ZeroValue;
			FIntVector prev_cell_step = FIntVector::ZeroValue;
			FVector cell_delta_accum = FVector::ZeroVector;
			for (;;)
			{
				// DO.
				const auto cur_cell_delta = cell_delta_accum.GetMin();
				// 到達Cell
				const auto trace_cell_id = trace_begin_cell + FIntVector(dir_sign) * total_cell_step;

				// ヒット判定処理パラメータ構築. このトレースではcur_cell_deltaが直接レイ上の位置になるため, レイの長さで正規化した値を渡す.
				const GridRayTraceVisitCellUniform visit_cell_param(trace_cell_id, 0, 1, cur_cell_delta * ray_d_c_len_inv, root_cell_data_[CalcRootCellIndex(trace_cell_id)]);

				// CellId範囲チェックとは別にt値のチェック. CellID範囲チェックだけでは広いルート階層換算での終了判定なので実際には線分の範囲外になっても継続してしまうため.
				if (1.0f <= visit_cell_param.ray_t)
					return;


				// ヒット判定処理呼び出し.
				if (hit_cell_process(ray_uniform, visit_cell_param, inout_payload))
					return;// ヒット判定関数がtrueを返した段階でトレース終了.

				// Next.
				{
					cell_delta_accum = cell_delta_offset + FVector(total_cell_step) * cell_delta;

					// xyzで最小値コンポーネントを探す.
					prev_cell_step = math::FVectorCompareLessEqual(cell_delta_accum, FVector::Min(FVector(cell_delta_accum.Y, cell_delta_accum.Z, cell_delta_accum.X), FVector(cell_delta_accum.Z, cell_delta_accum.X, cell_delta_accum.Y)));
					if constexpr (FINE_STEP_ON_DIAGONAL_CASE)
					{
						// 厳密にセルを巡回するために最小コンポーネントが複数あった場合に一つに制限する(XYZの順で優先.). 
						// この処理をしない場合は (0,0,0)の中心からズレたラインで(1,0,0)などを経由せずに(1,1,1)に移動する.
						auto tmp = prev_cell_step.X;
						prev_cell_step.Y = (0 < tmp) ? 0 : prev_cell_step.Y;
						tmp += prev_cell_step.Y;
						prev_cell_step.Z = (0 < tmp) ? 0 : prev_cell_step.Z;
					}
					// ステップは整数ベースで進める.
					total_cell_step += prev_cell_step;

					// 範囲チェックとbreak.
					if (trance_cell_range.X < total_cell_step.X || trance_cell_range.Y < total_cell_step.Y || trance_cell_range.Z < total_cell_step.Z)
						break;
				}
			}
		}
	};

	// ------------------------------------------------------------------------------------------------------------------------------------------------------


	// ------------------------------------------------------------------------------------------------------------------------------------------------------

	// Raytrace HierarchicalGrid.
	//	ルートから階層構造を持つMultiGridに対して階層移動DDAレイトレース機能をサポートする.
	//	クラス自体はルート階層分の解像度のデータのみを持ち, 実際の子階層の情報へのアクセスはtemplateで与える.
	template<int RootGridReso = 8>
	struct RayTracingHierarchicalGrid
	{
	public:
		static constexpr int k_root_grid_reso = RootGridReso;

		static constexpr float k_root_cell_width_default = 400.0f;

	public:
		// Root階層のCell毎の任意データ. 用途としては最上位で粗く分割した上でそれぞれの領域に割り当てたデータへの参照用情報に使う等.
		// NOTE. メンバとして直接確保するとこのクラスを持つUObjectについてUnrealHeaderToolで自動生成されるコード側でビルドエラーが発生する(警告だがエラー扱い).
		std::vector<uint32_t> root_cell_data_;

		float root_cell_width_ = k_root_cell_width_default;
		float root_cell_width_inv_ = 1.0f / k_root_cell_width_default;

		// Grid Center Position WorldSpace.
		FVector		grid_center_pos_ws_ = {};
		// Grid Space AABB Min Position WorldSpace.
		FVector		grid_aabb_min_ws_ = {};
		// Grid AABB Min Postion WorldGridSpace.
		FIntVector	grid_aabb_min_wgs_ = {};

	public:
		RayTracingHierarchicalGrid()
		{
			root_cell_data_.resize(k_root_grid_reso * k_root_grid_reso * k_root_grid_reso);
			Reset();
		}
		bool Initialize(float cell_width = k_root_cell_width_default)
		{
			root_cell_width_ = cell_width;
			root_cell_width_inv_ = 1.0f / cell_width;
		
			// 移動によるシフトコピーなどは後で.
			grid_center_pos_ws_ = {};
			this->grid_aabb_min_wgs_ = math::FVectorFloorToInt(grid_center_pos_ws_ * root_cell_width_inv_ - (k_root_grid_reso / 2.0f));
			grid_aabb_min_ws_ = FVector(this->grid_aabb_min_wgs_) * root_cell_width_;

			Reset();

			return true;
		}
		void Reset()
		{
			std::fill_n(root_cell_data_.begin(), root_cell_data_.size(), ~0u);
		}
		// World to RootGridSpace.
		constexpr FVector WorldToRootGridSpace(const FVector& world_position) const
		{
			return world_position * root_cell_width_inv_ - FVector(this->grid_aabb_min_wgs_);// なるべく小さい桁且つ整数ベースで計算.
		}
		// RootGridSpace to World.
		constexpr FVector RootGridSpaceToWorld(const FVector& root_grid_position) const
		{
			return (root_grid_position + FVector(this->grid_aabb_min_wgs_)) * root_cell_width_;
		}
		constexpr bool IsInner(const FIntVector cell) const
		{
			return math::IsInnerWithPositive(cell, FIntVector(k_root_grid_reso - 1));
		}
		constexpr int CalcRootCellIndex(const FIntVector& root_cell_id) const
		{
			return root_cell_id.X + (root_cell_id.Y * k_root_grid_reso) + (root_cell_id.Z * k_root_grid_reso * k_root_grid_reso);
		}

		// DDAによるトレース. Cell到達順はRay始点に近い順.
		// 進行中に到達したセルの内部グリッドを再帰無しでトラバースする. 多階層グリッドトレース.
		//	FINE_STEP_ON_DIAGONAL_CASE	: レイ方向が完全に対角線で各軸要素が同じ場合, セル移動ステップで接触するセルを厳密に処理するか. 
		//									セル中心からずれた (1,1,1)向きのレイの場合などでこのフラグがfalseの場合は本来は角がヒットしているセルをスキップしてしまう.
		//	ResolutionPerChildCell		: ルート下に階層的に配置されるCell一つの解像度. ルートにRootGridReso^3の子Cellが存在し, それぞれの子CellはChildCellResoで更に子Cellを持つ. グリッド全域の解像度ではない点に注意.
		//	MaxDepth					: 階層の最大深度. 0でルートのみ階層無し. 階層無しの場合はTraceGridの階層関係処理が無効化される.
		//
		//	ray_origin		: レイ始点ワールド座標.
		//	ray_end			: レイ終点ワールド座標.
		//	inout_payload	: レイ毎に更新されるPayloadデータ.
		//	hit_cell_process: Cellでのヒット処理を実行する関数オブジェクト. RayUniform,VisitUniform,Payloadを受け取り, Payloadの更新してトレースを終了するならtrueを返す.
		template<bool FINE_STEP_ON_DIAGONAL_CASE = true,
			int ResolutionPerChildCell = 4, int MaxDepth = 1,
			typename TraceCellDepthDescendingCheckerType = DefaultTraceCellDepthDescendingChecker,
			typename TraceCellHitProcessType = DefaultTraceCellHitProcess,
			typename PayloadType = TraceCellHitProcessType::Payload
		>
		void TraceHierarchicalGrid(
			const FVector& ray_origin, const FVector& ray_end,
			PayloadType& inout_payload, TraceCellHitProcessType hit_cell_process = {}, TraceCellDepthDescendingCheckerType depth_descending_checker = {}
		) const
		{
			// 内部用関数.
			constexpr auto CalcSafeDirInverse = [](const FVector& ray_dir) -> FVector
			{
				return FVector((FMath::IsNearlyZero(ray_dir.X)) ? FLT_MAX : 1.0f / ray_dir.X, (FMath::IsNearlyZero(ray_dir.Y)) ? FLT_MAX : 1.0f / ray_dir.Y, (FMath::IsNearlyZero(ray_dir.Z)) ? FLT_MAX : 1.0f / ray_dir.Z);
			};
			// 内部用関数.
			constexpr auto CalcClampedRay = [](const FVector& aabb_min, const FVector& aabb_max, const FVector& ray_begin, const FVector& ray_end)
				-> std::tuple<bool, FVector, FVector>
			{
				const FVector ray_d_c = ray_end - ray_begin;
				const FVector ray_dir_c = ray_d_c.GetSafeNormal();
				const float ray_len_c = ray_dir_c.Dot(ray_d_c);

				// Inv Dir.
				const FVector ray_dir_c_inv = FVector((FMath::IsNearlyZero(ray_dir_c.X)) ? FLT_MAX : 1.0f / ray_dir_c.X, (FMath::IsNearlyZero(ray_dir_c.Y)) ? FLT_MAX : 1.0f / ray_dir_c.Y, (FMath::IsNearlyZero(ray_dir_c.Z)) ? FLT_MAX : 1.0f / ray_dir_c.Z);

				const FVector t_to_min = (aabb_min - ray_begin) * ray_dir_c_inv;
				const FVector t_to_max = (aabb_max - ray_begin) * ray_dir_c_inv;
				const auto t_near = FVector::Min(t_to_min, t_to_max).GetMax();
				const auto t_far = FVector::Max(t_to_min, t_to_max).GetMin();

				// GridBoxとの交点が存在しなければ早期終了.
				// t_farが負-> 遠方点から外向きで外れ, t_farよりt_nearのほうが大きい->直線が交差していない, t_nearがレイの長さより大きい->届いていない.
				if (0.0f > t_far || t_near >= t_far || ray_len_c < t_near)
					return std::make_tuple(false, FVector{}, FVector{});

				// Grid内にクランプしたトレース始点終点.
				// 以降はこの始点終点で処理.
				const FVector ray_s_clamped_c = (0.0 > t_near) ? ray_begin : ray_begin + t_near * ray_dir_c;
				const FVector ray_e_clamped_c = (ray_len_c < t_far) ? ray_end : ray_begin + t_far * ray_dir_c;
				return std::make_tuple(true, ray_s_clamped_c, ray_e_clamped_c);
			};

			const FVector ray_begin_c = WorldToRootGridSpace(ray_origin);
			const FVector ray_end_c = WorldToRootGridSpace(ray_end);

			// Grid空間内にクランプした始点終点と交差判定取得.
			const auto [is_hit, ray_p0_clamped_c, ray_p1_clamped_c] = CalcClampedRay(FVector::ZeroVector, FVector(k_root_grid_reso), ray_begin_c, ray_end_c);
			if (!is_hit)
				return;

			const FVector ray_d_cellf = ray_p1_clamped_c - ray_p0_clamped_c;
			const FIntVector ray_sign_i = FIntVector(ray_d_cellf.GetSignVector());// 1 : possitive or zero, -1 : negative.
			const FVector ray_dir = ray_d_cellf.GetSafeNormal();
			const FVector ray_dir_inv = CalcSafeDirInverse(ray_dir);
			const auto ray_len = ray_d_cellf.Length();

			const FIntVector ray_begin_celli = math::FVectorFloorToInt(ray_p0_clamped_c);

			// ここまでの情報を元に第一象限方向への問題に変換する.
			// 
			// dir_sign方向でミラーリングした際の原点相当のCell. dir{-,+} の場合は {1, 0} となり, 元のCellの右側のCell位置を指す.
			const FIntVector abs_base_celli = ray_begin_celli + math::FIntVectorMax(FIntVector::ZeroValue, ray_sign_i * (-1));
			// ミラートレースベクトル.
			const FVector abs_d_cellf = ray_d_cellf.GetAbs();
			// ミラートレースベクトルの逆数(ゼロ除算対策付き).
			const FVector  abs_d_cellf_inv = CalcSafeDirInverse(abs_d_cellf);
			// ミラー後の abs_base_celliを基準とした場合の位置.
			const FVector abs_ray_begin_cellf = FVector(abs_base_celli) + (ray_p0_clamped_c - FVector(abs_base_celli)).GetAbs();
			const FVector abs_ray_end_cellf = FVector(abs_base_celli) + (ray_p1_clamped_c - FVector(abs_base_celli)).GetAbs();
			// ミラートレースのCell範囲. 個別にInt化(Floorの代用)をとる点に注意.
			const FIntVector abs_ray_range_celli = FIntVector(abs_ray_end_cellf) - FIntVector(abs_ray_begin_cellf);

			// 現在の深度の解像度からT値オフセットを再計算.
			const auto CalcAbsTvalueOffset = [abs_ray_begin_cellf, abs_d_cellf_inv](int cur_toral_reso) -> FVector
			{
				return (math::FVectorFloor(abs_ray_begin_cellf * cur_toral_reso) + FVector::OneVector - abs_ray_begin_cellf * cur_toral_reso) * abs_d_cellf_inv / float(cur_toral_reso);
			};

			// トレースUniformパラメータ.
			const GridRayTraceRayUniform ray_uniform(ray_p0_clamped_c, ray_len, ray_dir, ray_dir_inv);

			// ステップ中で更新される変数群.
			FIntVector abs_cur_begin_celli = FIntVector(abs_ray_begin_cellf);
			FIntVector abs_cur_range = abs_ray_range_celli;
			FIntVector abs_total_cell_step = FIntVector::ZeroValue;
			FIntVector abs_prev_cell_step = FIntVector::ZeroValue;
			FVector abs_delta_accum = FVector::ZeroVector;
			int cur_depth = 0;
			int total_reso_scale = 1;
			FVector abs_cur_delta_offset = CalcAbsTvalueOffset(total_reso_scale);

			// トレースステップ
#if defined(UE_BUILD_DEBUG) && 1
			constexpr int safe_step_max = 65535;
			for (int safe_step_i = 0; safe_step_i <= safe_step_max; ++safe_step_i) {
				check(safe_step_i < safe_step_max);// Debugでは念のためループ数制限し, 更に異常ループと思われる場合はassert.
#else
			for (;;) {
#endif
				// ---------------------------------------
				// 現在のCell.
				const auto cur_cell_delta = abs_delta_accum.GetMin();
				const auto cur_cell = FIntVector(ray_p0_clamped_c * total_reso_scale) + ray_sign_i * abs_total_cell_step;// 到達した実際のCell(Mirrorではない点に注意). 正の領域での計算なのでFloorより高速なInt変換で代用.
				const auto cur_cell_in_root = cur_cell / total_reso_scale;// 所属RootCell.

				const auto& root_data = root_cell_data_[CalcRootCellIndex(cur_cell_in_root)];

				const float total_depth_reso_scale_inv = 1.0f / total_reso_scale;
				const auto abs_delta_accum_next = abs_cur_delta_offset + FVector(abs_total_cell_step) * abs_d_cellf_inv * total_depth_reso_scale_inv;


				// ヒット判定処理パラメータ構築.
				const GridRayTraceVisitCellUniform visit_cell_param(cur_cell, cur_depth, total_reso_scale, cur_cell_delta, root_data);

				// CellId範囲チェックとは別にt値のチェック. CellID範囲チェックだけでは広いルート階層換算での終了判定なので実際には線分の範囲外になっても継続してしまうため.
				if (1.0f <= visit_cell_param.ray_t)
					return;

				// ---------------------------------------
				// 階層移動.
				// チェッカーで下のレイヤに移動するか判定して真なら移動.
				if constexpr (0 < MaxDepth)
				{
					if (MaxDepth > cur_depth && depth_descending_checker(ray_uniform, visit_cell_param))
					{
						++cur_depth;// 深度増加.
						total_reso_scale *= ResolutionPerChildCell;// 子階層解像度積算.

						// 新たな深度の解像度CellでのT値オフセットを再計算.
						abs_cur_delta_offset = CalcAbsTvalueOffset(total_reso_scale);

						// 1階層潜った際に入っているべき親Cellの範囲. 基本的にこれらはMirror空間.
						const auto clamp_min = (abs_cur_begin_celli + abs_total_cell_step) * ResolutionPerChildCell;
						const auto clamp_max = clamp_min + FIntVector(ResolutionPerChildCell - 1);

						// 基準Cellを現在の深度に更新.
						abs_cur_begin_celli = FIntVector(abs_ray_begin_cellf * total_reso_scale);// 正の領域なのでFloorより高速なInt変換で代用.

						auto abs_tmp_cell = FIntVector((ray_dir.GetAbs() * cur_cell_delta * ray_len + abs_ray_begin_cellf) * total_reso_scale);// 正の領域なのでFloorより高速なInt変換で代用.
						abs_tmp_cell = math::FIntVectorClamp(abs_tmp_cell, clamp_min, clamp_max);

						abs_total_cell_step = abs_tmp_cell - abs_cur_begin_celli;
						abs_cur_range = clamp_max - abs_cur_begin_celli;

						continue;// 潜ったらcontinue;
					}
				}
				// ---------------------------------------
			


				// ヒット判定処理呼び出し.
				if (hit_cell_process(ray_uniform, visit_cell_param, inout_payload))
					return;// ヒット判定関数がtrueを返した段階でトレース終了.


				// ---------------------------------------
				// デルタからCell移動方向を計算するために最小値コンポーネントを探す.
				abs_prev_cell_step = math::FVectorCompareLessEqual(abs_delta_accum_next, FVector::Min(FVector(abs_delta_accum_next.Y, abs_delta_accum_next.Z, abs_delta_accum_next.X), FVector(abs_delta_accum_next.Z, abs_delta_accum_next.X, abs_delta_accum_next.Y)));
				if constexpr (FINE_STEP_ON_DIAGONAL_CASE)
				{
					// 厳密にセルを巡回するために最小コンポーネントが複数あった場合に一つに制限する(XYZの順で優先.). 
					// この処理をしない場合は (0,0,0)の中心からズレたラインで(1,0,0)などを経由せずに(1,1,1)に移動する.
					auto tmp = abs_prev_cell_step.X;
					abs_prev_cell_step.Y = (0 < tmp) ? 0 : abs_prev_cell_step.Y;
					tmp += abs_prev_cell_step.Y;
					abs_prev_cell_step.Z = (0 < tmp) ? 0 : abs_prev_cell_step.Z;
				}

				// ステップは整数ベースで進める.
				abs_total_cell_step += abs_prev_cell_step;
				// デルタ更新.
				abs_delta_accum = abs_delta_accum_next;


				// トレースが現在の範囲を超えた場合 (階層有り).
				if constexpr (0 < MaxDepth)
				{
					for (; 0 <= cur_depth && 0 < (abs_total_cell_step - abs_cur_range).GetMax();)
					{
						if (0 == cur_depth)
						{
							return;// 深度0ならそのまま終了.
						}
						else
						{
							// 深度1以上なら一つ浅い深度へ移動.
							total_reso_scale /= ResolutionPerChildCell;
							--cur_depth;// 深度減少.

							// 新たな深度の解像度CellでのT値オフセットを再計算.
							abs_cur_delta_offset = CalcAbsTvalueOffset(total_reso_scale);

							auto tmp_global_trace_cell = (abs_total_cell_step + abs_cur_begin_celli) / ResolutionPerChildCell;
							// 基準Cellを現在の深度に更新.
							abs_cur_begin_celli = abs_cur_begin_celli / ResolutionPerChildCell;// 除算のみ.
							abs_total_cell_step = math::FIntVectorAbs(tmp_global_trace_cell - abs_cur_begin_celli);

							// Cellレンジを現在の深度に更新. 深度0だけレンジが異なるので特別扱い.
							if (0 == cur_depth)
								abs_cur_range = abs_ray_range_celli;
							else
							{
								// ここのフローは要確認.
								const auto clamp_max = tmp_global_trace_cell + FIntVector(ResolutionPerChildCell - 1);
								abs_cur_range = clamp_max - abs_cur_begin_celli;

							}
							// 階層有りのセルの境界でt値が一つ前になってしまうのでcontinueしない. ただし2階層以上の連続Upの動作は未確認.
						}
					}
				}
				// トレースが現在の範囲を超えた場合 (階層無し).
				if constexpr (0 >= MaxDepth)
				{
					if (0 < (abs_total_cell_step - abs_cur_range).GetMax())
						return;
				}
			}
		}	
	
		// DDAによるトレースのルート層のみバージョン. Cell到達順はRay始点に近い順.
		template<bool FINE_STEP_ON_DIAGONAL_CASE = true,
			typename TraceCellHitProcessType = DefaultTraceCellHitProcess,
			typename PayloadType = TraceCellHitProcessType::Payload
		>
		void TraceGridOnlyRoot(
			const FVector& ray_origin, const FVector& ray_end,
			PayloadType& inout_payload, TraceCellHitProcessType hit_cell_process = {}
		) const
		{
			TraceMultiGrid<FINE_STEP_ON_DIAGONAL_CASE, 1, 0>(ray_origin, ray_end, inout_payload, hit_cell_process);
		}
	};



	using GridCellAddrType = uint32_t;

	struct OccupancyGridLeafData
	{
		constexpr OccupancyGridLeafData()
		{
			occupancy_4x4x4 = {};
		}
		constexpr OccupancyGridLeafData(uint64_t v)
		{
			occupancy_4x4x4 = v;
		}
		static constexpr OccupancyGridLeafData FullBrick()
		{
			return OccupancyGridLeafData(~uint64_t(0));
		}
		static constexpr OccupancyGridLeafData ZeroBrick()
		{
			return OccupancyGridLeafData(uint64_t(0));
		}

		int Get(int x, int y, int z) const
		{
			const uint64_t s = x + (y << 2) + (z << 4);
			return occupancy_4x4x4 & (uint64_t(1) << s);
		}

		template<bool SetBit>
		void Set(int x, int y, int z)
		{
			const uint64_t s = x + (y << 2) + (z << 4);
			if constexpr (SetBit)
			{
				occupancy_4x4x4 = occupancy_4x4x4 | (uint64_t(1) << s);
			}
			else
			{
				occupancy_4x4x4 = occupancy_4x4x4 & (~(uint64_t(1) << s));
			}
		}
		void Set1(int x, int y, int z)
		{
			Set<true>(x, y, z);
		}
		void Set0(int x, int y, int z)
		{
			Set<false>(x, y, z);
		}

		uint64_t occupancy_4x4x4 = {};
	};

	// Raytrace階層Grid構造を使用してシーンのOccpancyGridを構築するクラス.
	class HierarchicalOccupancyGrid
	{
	public:
		static constexpr GridCellAddrType k_invalid_u32 = ~GridCellAddrType(0);

		// MultigridのRoot下Cell一つの分割数.
		static constexpr int k_child_cell_reso = 4;
		// MultigridのRoot下Cell一つの要素数.
		static constexpr int k_child_cell_vol3d = k_child_cell_reso * k_child_cell_reso * k_child_cell_reso;
		// Multigridのレイヤ数. 0でルートのみ.
		static constexpr int k_multigrid_max_depth = 2;


		// セルデータ.
		struct CellData
		{
			// SubCellへのアドレス情報. 固定サイズ.
			std::array<GridCellAddrType, k_child_cell_vol3d> child_addr;

		public:
			CellData() = default;
			CellData(GridCellAddrType filled)
			{
				child_addr.fill(filled);
			}
			// すべての要素を無効値で埋めたオブジェクトを取得.
			static CellData GetInvalidFilled()
			{
				static CellData v(k_invalid_u32);
				return v;
			}
		};
		static constexpr auto k_sizeof_ChildCell = sizeof(CellData);


	public:
		HierarchicalOccupancyGrid()
		{
		}
		~HierarchicalOccupancyGrid()
		{
		}

		// cell_width : RootCell一つのワールドスペースサイズ.
		bool Initialize(float cell_width = 1200.0f)
		{
			bgrid_.Initialize(cell_width);
			// RootCell毎の最大分割数. ChildCell4分割で深度2なら16.
			leaf_cell_space_reso_ = std::pow(k_child_cell_reso, k_multigrid_max_depth);
			// 最下層Cellのワールド空間サイズ
			bottom_cell_width_ws_ = cell_width / leaf_cell_space_reso_;
			// リーフのBrick内要素のワールド空間サイズ
			brick_elem_width_ws_ = bottom_cell_width_ws_ / 4.0f;

			is_initialized_ = true;
			return true;
		}
		bool IsInitialized() const
		{
			return is_initialized_;
		}

		// 視点からのレイヒット地点にOccupanncyを登録する.
		//	Octree管理.
		void UpdateOccupancy(const FVector& sample_ray_origin, const TArray<std::tuple<FVector, bool>>& sample_ray_end_and_ishit)
		{
			// 移動によるシフトコピーなどは後で.


			auto AllocNewCellOrBrick = [this](int depth)
			{
				int new_element_index = -1;
				if (depth < k_multigrid_max_depth)
				{
					// リーフ以外ではCell割当.
					auto pool_index = cell_pool_flag_.FindAndSetFirstZeroBit();
					if (INDEX_NONE == pool_index)
					{
						pool_index = cell_pool_flag_.Num();
						cell_pool_flag_.Add(true);
						cell_pool_.Add(CellData::GetInvalidFilled());// Cellはすべて無効要素で初期化.
					}
					else
					{
						cell_pool_flag_[pool_index] = true;
						cell_pool_[pool_index] = CellData::GetInvalidFilled();
					}
					new_element_index = pool_index;
				}
				else
				{
					// リーフではBrick割当.
					auto pool_index = bit_occupancy_brick_pool_flag_.FindAndSetFirstZeroBit();
					if (INDEX_NONE == pool_index)
					{
						pool_index = bit_occupancy_brick_pool_flag_.Num();
						bit_occupancy_brick_pool_flag_.Add(true);
						bit_occupancy_brick_pool_.Add({});// Brickはゼロクリア.
					}
					else
					{
						bit_occupancy_brick_pool_flag_[pool_index] = true;
						bit_occupancy_brick_pool_[pool_index] = {};
					}
					new_element_index = pool_index;
				}
				return new_element_index;
			};

			for (int i = 0; i < sample_ray_end_and_ishit.Num(); ++i)
			{
				// ヒットサンプルのみ.
				if (!std::get<1>(sample_ray_end_and_ishit[i]))
					continue;

				// サンプル位置のCell及びBrick書き込み.
				const auto root_cell = bgrid_.WorldToRootGridSpace(std::get<0>(sample_ray_end_and_ishit[i]));
				const auto root_cell_i = math::FVectorFloorToInt(root_cell);// 0をまたいで負の場合があるためint丸めでは丸め方向が一貫しないためFloor.
				const auto root_cell_frac = root_cell - FVector(root_cell_i);
				if (bgrid_.IsInner(root_cell_i))
				{
					// ルートデータにdepth1のCellを割り当て.
					auto root_cell_index = bgrid_.CalcRootCellIndex(root_cell_i);
					if (k_invalid_u32 == bgrid_.root_cell_data_[root_cell_index])
					{
						// CellまたはBrick割当.
						bgrid_.root_cell_data_[root_cell_index] = AllocNewCellOrBrick(0);
					}

					// depth1以降の CellまたはBrick の割当.
					auto cell_addr = bgrid_.root_cell_data_[root_cell_index];
					auto cell_frac = root_cell_frac;
					for (int depth_i = 1; depth_i <= k_multigrid_max_depth; ++depth_i)
					{
						const auto child_cell_pos = cell_frac * k_child_cell_reso;// Fracを分割数乗算してCell位置計算, そこからのFracを更に計算してリーフまで潜っていく.
						const auto child_cell_pos_i = FIntVector(child_cell_pos);// 0をまたがないはずなのでFloorより高速なint丸め.
						cell_frac = child_cell_pos - FVector(child_cell_pos_i);// さらに子CellのFracへ更新.
						const auto child_cell_index = (child_cell_pos_i.X) + (child_cell_pos_i.Y * k_child_cell_reso) + (child_cell_pos_i.Z * k_child_cell_reso * k_child_cell_reso);
						if (k_invalid_u32 == cell_pool_[cell_addr].child_addr[child_cell_index])
						{
							cell_pool_[cell_addr].child_addr[child_cell_index] = AllocNewCellOrBrick(depth_i);
						}
						cell_addr = cell_pool_[cell_addr].child_addr[child_cell_index];// 子階層へ移動.
					}
					// NOTE. cell_addrにはリーフCellに割り当てられたBrickアドレスが格納されている.


					// 検索または新規割当された4x4x4 Brickへ書き込み.
					const auto brick_pos = cell_frac * 4;// リーフCellでのFracを利用してBrick内位置を計算.
					const auto brick_pos_i = FIntVector(brick_pos);// 0をまたがないはずなのでFloorより高速んはint丸めで済ませる.
					// or で専有ビット書き込み.
					auto& brick = bit_occupancy_brick_pool_[cell_addr];
					brick.Set1(brick_pos_i.X, brick_pos_i.Y, brick_pos_i.Z);
				}
			}
		}
		
		// Occlupancyを考慮して移動するパーティクルテスト. パーティクル投入.
		void AddParticleTest(const FVector& pos, const FVector& vel)
		{
			constexpr int k_particle_max_test = 1000;

			// Cell確保.
			auto pool_index = particle_pool_flag_.FindAndSetFirstZeroBit();
			if (INDEX_NONE == pool_index)
			{
				// プール最大でクランプ.
				if (k_particle_max_test <= particle_pool_flag_.Num())
					return;
				pool_index = particle_pool_flag_.Num();
		
				particle_pool_flag_.Add(true);
				particle_pool_.Add({});
			}
			else
			{
				particle_pool_flag_[pool_index] = true;// 有効化.
			}
			particle_pool_.EmplaceAt(pool_index, ParticleTestData{ pos, 0.0f, vel });
		}

		// Occlupancyを考慮して移動するパーティクルテスト. パーティクル更新.
		void UpdateParticle(float delta_sec)
		{
			const auto k_gravity = -FVector(0.0f, 0.0f, 9.81f * 100.0f);
			constexpr float k_collision_offset = 0.5f;
			constexpr float k_restitution = 0.75f;

			for (auto i = 0; i < particle_pool_flag_.Num(); ++i)
			{
				if (!particle_pool_flag_[i])
					continue;

				// 重力.
				particle_pool_[i].vel += k_gravity * delta_sec;

				auto candidate_pos = particle_pool_[i].pos + particle_pool_[i].vel * delta_sec;
				if (!particle_pool_[i].vel.IsNearlyZero())
				{
					FVector trace_hit_pos, trace_hit_normal;
					if (TraceSingle(trace_hit_pos, trace_hit_normal, particle_pool_[i].pos, candidate_pos))
					{
						particle_pool_[i].vel = particle_pool_[i].vel - (1.0f + k_restitution) * trace_hit_normal * FVector::DotProduct(trace_hit_normal, particle_pool_[i].vel);
						//particle_pool_[i].vel = FVector::Zero();

						// ヒット時刻の補正などはしていない.
						// 反射後の再ヒットを回避するためオフセット.
						candidate_pos = trace_hit_pos + (trace_hit_normal * k_collision_offset);
					}
				}
				particle_pool_[i].pos = candidate_pos;
				particle_pool_[i].life_sec += delta_sec;

				if (20.0f < particle_pool_[i].life_sec)
				{
					particle_pool_flag_[i] = false;
				}
			}
		}

		// MultiGridの深度移動チェック.
		struct TraceCellDepthDescendingCheckerForMultiGrid
		{
			const HierarchicalOccupancyGrid& grid_impl;


			// Cellとのヒット処理. システムからレイの基本情報とトレース対象のCell情報, レイのPayloadを受け取って判定やPayload更新をする.
			const bool operator()(const GridRayTraceRayUniform& ray_uniform, const GridRayTraceVisitCellUniform& visit_cell_param)
			{
				// 現在深度で指定Cellのデータが存在すれば降下する.
				const auto [cell_data, cell_depth] = grid_impl.GetGridCellData<false>(visit_cell_param.depth, visit_cell_param.cell_id);
				// cellにデータがあれば下層へ移動.
				return grid_impl.k_invalid_u32 != cell_data;
			}
		};

		// MultiGrid Cellヒット処理とそのPayloadの定義.
		struct MultiGridTraceCellHitProcess
		{
			const HierarchicalOccupancyGrid& grid_impl;

			struct Payload
			{
				FVector hit_pos;		// トレース中に更新するヒット座標.
				float	ray_t = FLT_MAX;// トレース中に更新するt値.
				int		depth = 0;		// ヒットした深度.
			};
			// Cellとのヒット処理. システムからレイの基本情報とトレース対象のCell情報, レイのPayloadを受け取って判定やPayload更新をする.
			bool operator()(const GridRayTraceRayUniform& ray_uniform, const GridRayTraceVisitCellUniform& visit_cell_param, Payload& ray_payload)
			{
				if (visit_cell_param.root_cell_data == ~0u)
					return false;

				if (ray_payload.ray_t <= visit_cell_param.ray_t)
					return false;

				// Gridの実体から該当DepthのCell情報を読み出し.
				const auto [cell_data, cell_depth] = grid_impl.GetGridCellData(visit_cell_param.depth, visit_cell_param.cell_id);
				// CellがEmpty(無効)ならヒットなし.
				if (cell_data == ~0u)
					return false;

				// t 更新.
				ray_payload.ray_t = visit_cell_param.ray_t;
				// ヒット位置更新. ここではGrid空間座標.
				ray_payload.hit_pos = ray_uniform.ray_origin + ray_uniform.ray_dir * ray_uniform.ray_length * ray_payload.ray_t;
				// 深度.
				ray_payload.depth = visit_cell_param.depth;

				// ヒット面の法線テスト. 必要ならPayloadに乗せる.
				{
					// ヒット位置のBrickCell中心からの相対位置で法線計算.
					const auto brick_elem_center = (FVector(visit_cell_param.cell_id) + FVector(0.5)) / visit_cell_param.resolution_per_root_cell;
					const auto hitpos_from_center = (ray_payload.hit_pos - brick_elem_center);
					const auto hitpos_from_center_sign = hitpos_from_center.GetSignVector();
					const auto hitpos_from_center_abs = hitpos_from_center.GetAbs();
					// 中心からのベクトルで最大要素軸を法線として返す.
					const auto hitpos_from_center_abs_max_cmp = math::FVectorCompareGreater(hitpos_from_center_abs,
						FVector::Max(FVector(hitpos_from_center_abs.Y, hitpos_from_center_abs.Z, hitpos_from_center_abs.X), FVector(hitpos_from_center_abs.Z, hitpos_from_center_abs.X, hitpos_from_center_abs.Y)));

					const auto hit_normal = (FVector(hitpos_from_center_abs_max_cmp) * hitpos_from_center_sign).GetSafeNormal();// すべて等値でもSafeNormalで一応ベクトルが返る.
				}
		
				// trueを返してトレース終了.
				// DDAによるNear-Far順トレースであるため早期棄却しても良いはず.
				return true;
			}
		};

		// MultiGrid Brick Cellヒット処理とそのPayloadの定義. MultiGridTraceCellHitProcessとは違い更にBrick内OccupancyCellとのヒットを取る.
		struct MultiGridTraceCellBrickHitProcess
		{
			const HierarchicalOccupancyGrid& grid_impl;

			struct Payload
			{
				FVector hit_pos;		// トレース中に更新するヒット座標.
				float	ray_t = FLT_MAX;// トレース中に更新するt値.
				FVector hit_normal;		// トレース中に更新するヒット法線(テスト中).
				int		depth = 0;		// ヒットした深度.
			};
			// Cellとのヒット処理. システムからレイの基本情報とトレース対象のCell情報, レイのPayloadを受け取って判定やPayload更新をする.
			bool operator()(const GridRayTraceRayUniform& ray_uniform, const GridRayTraceVisitCellUniform& visit_cell_param, Payload& ray_payload)
			{
				constexpr int k_brick_size = 4;
				constexpr int k_brick_max_range = k_brick_size - 1;
				// ベクトルの要素逆数ベクトルを返す. 0除算はFLT_MAX.
				static constexpr auto CalcSafeDirInverse = [](const FVector& ray_dir) -> FVector
				{
					return FVector((FMath::IsNearlyZero(ray_dir.X)) ? FLT_MAX : 1.0f / ray_dir.X, (FMath::IsNearlyZero(ray_dir.Y)) ? FLT_MAX : 1.0f / ray_dir.Y, (FMath::IsNearlyZero(ray_dir.Z)) ? FLT_MAX : 1.0f / ray_dir.Z);
				};

				// リーフのみ処理.
				if (grid_impl.k_multigrid_max_depth != visit_cell_param.depth)
					return false;

				if (ray_payload.ray_t <= visit_cell_param.ray_t)
					return false;

				// Gridの実体から該当DepthのCell情報を読み出し.
				const auto [cell_data, cell_depth] = grid_impl.GetGridCellData(visit_cell_param.depth, visit_cell_param.cell_id);
				// CellがEmpty(無効)ならヒットなし.
				if (cell_data == ~0u)
					return false;

				check(cell_data < static_cast<uint32_t>(grid_impl.bit_occupancy_brick_pool_.Num()));
				const auto& brick = grid_impl.bit_occupancy_brick_pool_[cell_data];
				// BrickのBinaryOccupancyが空の場合はスキップ
				if (0 == brick.occupancy_4x4x4)
					return false;


				// ヒット座標(Cell空間)
				const auto trace_pos_c = ray_uniform.ray_origin + ray_uniform.ray_length * ray_uniform.ray_dir * (visit_cell_param.ray_t);// float誤差で微小にセルの整数に届かない場合があるので注意.
				// ヒット座標のCell内ローカル座標[0, 1]. float誤差で整数Cellに届いていない場合があるため clamp(0,1)を取っている点に注意.
				// trace_pos_cはRootGrid空間であるため, 到達Cellの階層における解像度スケールを乗じて cell_id と同じ空間に持っていく.
				const auto trace_pos_c_frac = math::FVectorClamp(trace_pos_c * visit_cell_param.resolution_per_root_cell - FVector(visit_cell_param.cell_id), FVector::ZeroVector, FVector::OneVector);

				const auto brick_aabb_t_min = (FVector::ZeroVector - trace_pos_c_frac) * ray_uniform.ray_dir_inv;
				const auto brick_aabb_t_max = (FVector::OneVector - trace_pos_c_frac) * ray_uniform.ray_dir_inv;
				const auto t0 = FVector::Min(brick_aabb_t_min, brick_aabb_t_max).GetMax();
				const auto t1 = FVector::Max(brick_aabb_t_min, brick_aabb_t_max).GetMin();

				// Cellから外部に出る地点.
				const auto cell_out_frac = FVector::Max(FVector::ZeroVector, trace_pos_c_frac + t1 * ray_uniform.ray_dir);

				const auto brick_rd = (cell_out_frac - trace_pos_c_frac) * k_brick_size;
				const auto brick_rd_inv = CalcSafeDirInverse(brick_rd);

				//Brick内t値の全体t値に対するスケール. 到達Cell自体の解像度スケールとBrickの解像度を考慮する.
				const auto t_scale = (1.0f / (k_brick_size * visit_cell_param.resolution_per_root_cell)) * (brick_rd.Length() / ray_uniform.ray_length);

				const auto dir_sign = ray_uniform.ray_dir.GetSignVector();
				const auto delta = FVector::Min(dir_sign * brick_rd_inv, FVector::OneVector) * t_scale;

				const auto begin_cell_pos = math::FVectorClamp(trace_pos_c_frac * k_brick_size, FVector::ZeroVector, FVector(k_brick_size - FLT_EPSILON));
				const auto begin_cell = math::FIntVectorMin(FIntVector(k_brick_max_range), math::FVectorFloorToInt(begin_cell_pos));
				const auto end_cell = math::FIntVectorMin(FIntVector(k_brick_max_range), math::FVectorFloorToInt(cell_out_frac * k_brick_size));
				const auto cell_range = math::FIntVectorAbs(end_cell - begin_cell);
				const auto t_max_base = ((FVector(begin_cell) + FVector::Max(dir_sign, FVector::ZeroVector) - begin_cell_pos) * brick_rd_inv).GetAbs() * t_scale;

				float last_delta = 0.0f;
				FIntVector total_step_cell = FIntVector::ZeroValue;
				FIntVector prev_step = FIntVector::ZeroValue;
				for (;;)
				{
					const auto trace_cell_id = begin_cell + FIntVector(dir_sign) * total_step_cell;
					const auto cell_index = (trace_cell_id.X) + (trace_cell_id.Y * k_brick_size) + (trace_cell_id.Z * k_brick_size * k_brick_size);

					const auto total_delta = visit_cell_param.ray_t + (last_delta);

					// CellId範囲チェックとは別にt値のチェック. CellID範囲チェックだけでは広いルート階層換算での終了判定なので実際には線分の範囲外になっても継続してしまうため.
					if (1.0f <= total_delta)
						return false;

					if (brick.occupancy_4x4x4 & (uint64_t(1) << uint64_t(cell_index)))
					{
						// t更新.
						ray_payload.ray_t = total_delta;
						// ヒット位置更新. Grid空間座標. 微少値でCell外になる場合があるためEpsilon加算.
						ray_payload.hit_pos = (ray_payload.ray_t + FLT_EPSILON) * ray_uniform.ray_dir * ray_uniform.ray_length + ray_uniform.ray_origin;
						// 深度.
						ray_payload.depth = visit_cell_param.depth;

						// ヒット面の法線.
						{
							// ヒット位置のBrickCell中心からの相対位置で法線計算.
							const auto brick_elem_center = (((FVector(trace_cell_id) + FVector(0.5)) / k_brick_size) + FVector(visit_cell_param.cell_id)) / visit_cell_param.resolution_per_root_cell;
							const auto hitpos_from_center = (ray_payload.hit_pos - brick_elem_center);
							const auto hitpos_from_center_sign = hitpos_from_center.GetSignVector();
							const auto hitpos_from_center_abs = hitpos_from_center.GetAbs();
							// 中心からのベクトルで最大要素軸を法線として返す.
							const auto hitpos_from_center_abs_max_cmp = math::FVectorCompareGreater(hitpos_from_center_abs,
								FVector::Max(FVector(hitpos_from_center_abs.Y, hitpos_from_center_abs.Z, hitpos_from_center_abs.X), FVector(hitpos_from_center_abs.Z, hitpos_from_center_abs.X, hitpos_from_center_abs.Y)));

							ray_payload.hit_normal = (FVector(hitpos_from_center_abs_max_cmp) * hitpos_from_center_sign).GetSafeNormal();// すべて等値でもSafeNormalで一応ベクトルが返る.
						}

						// ヒット. 始点から順にトレースしているためClosestHitは最初のHitで良いはず. ヒットをとりながら積算するような場合はこの挙動を変える.
						return true;
					}
					// Next Step.
					const auto next_t = t_max_base + FVector(total_step_cell) * delta;
					// xyzで最小値コンポーネントを探す.
					prev_step = math::FVectorCompareLessEqual(next_t, FVector::Min(FVector(next_t.Y, next_t.Z, next_t.X), FVector(next_t.Z, next_t.X, next_t.Y)));// Equal無しだとすべて等値だった場合に進行できないため.
					if constexpr (true)
					{
						// 厳密にセルを巡回するために最小コンポーネントが複数あった場合に一つに制限する(XYZの順で優先.). 
						// この処理をしない場合は (0,0,0)の中心からズレたラインで(1,0,0)などを経由せずに(1,1,1)に移動する.
						auto tmp = prev_step.X;
						prev_step.Y = (0 < tmp) ? 0 : prev_step.Y;
						tmp += prev_step.Y;
						prev_step.Z = (0 < tmp) ? 0 : prev_step.Z;
					}
					// ステップは整数ベースで進める.
					total_step_cell += prev_step;
					last_delta = next_t.GetMin();

					// 範囲チェック.
					if (0 < (total_step_cell - cell_range).GetMax()) break;
				}

				// ヒットせず.
				return false;
			}
		};




		bool TraceSingle(FVector& out_hit_pos_ws, FVector& out_hit_normal_ws, const FVector& ray_origin_ws, const FVector& ray_end_ws) const
		{
			// MultiGrid Brickトレース
			MultiGridTraceCellBrickHitProcess cell_hit_process = { *this };

			decltype(cell_hit_process)::Payload payload = {};
			// MultiGridの深度移動判定.
			TraceCellDepthDescendingCheckerForMultiGrid depth_descending = { *this };

			// マルチグリッドトレーステスト.
			bgrid_.TraceHierarchicalGrid<true, k_child_cell_reso, k_multigrid_max_depth>(ray_origin_ws, ray_end_ws, payload, cell_hit_process, depth_descending);

			// t値が初期値より小さくなっていればヒットしている.
			if (FLT_MAX > payload.ray_t)
			{
				// Gric空間の結果をWolrdSpaceに変換して返す.
				out_hit_pos_ws = bgrid_.RootGridSpaceToWorld(payload.hit_pos);
				out_hit_normal_ws = payload.hit_normal;

				return true;
			}
			return false;
		}

		// Cellデータの取得. 呼び出すたびにRootから探索.
		//	depth:0, XYZ -> RootのXYZ位置のデータ, depth:1, XYZ -> Root直下のCellのXYZ位置のデータ.
		//	GetReachableによって指定Cellに到達しなかった場合の戻り値が変わる.
		//		false : 指定Cellへ到達できなければInvalidを返却.
		//		true  : 指定Cellへ到達できなければ途中までの結果を返却.
		template<bool GetReachable = false>
		std::tuple<GridCellAddrType, int> GetGridCellData(int depth, const FIntVector& cell) const
		{
			check(k_multigrid_max_depth >= depth);
			const int cell_total_reso = std::pow(k_child_cell_reso, depth);// Root直下の1Cellあたりの最大分割数. TODO. tempateなどで事前計算したい.
			// RootCell.
			const FIntVector root_cell = cell / cell_total_reso;
			if (0 > root_cell.GetMin() || bgrid_.k_root_grid_reso <= root_cell.GetMax())
				return { k_invalid_u32, 0 };// Root範囲外.

			auto cell_addr = bgrid_.root_cell_data_[bgrid_.CalcRootCellIndex(root_cell)];
			FIntVector local_cell = cell - (root_cell * cell_total_reso);// Root直下Cell内でのローカル位置.

			int cur_reso = cell_total_reso;

			// ループ内共通処理ラムダ.
			auto LoopInner = [&cur_reso, &local_cell]()
			{
				cur_reso /= k_child_cell_reso;
				const FIntVector cur_depth_cell = local_cell / cur_reso;
				local_cell = local_cell - (cur_depth_cell * cur_reso);

				check(0 <= cur_depth_cell.GetMin() && k_child_cell_reso > cur_depth_cell.GetMax());// 一応チェック.
				const auto local_cell_index = cur_depth_cell.X + (cur_depth_cell.Y * k_child_cell_reso) + (cur_depth_cell.Z * k_child_cell_reso * k_child_cell_reso);

				return local_cell_index;
			};

			if constexpr (!GetReachable)
			{
				// 指定のCellに到達できなかった場合はInvalidを返すバージョン.
				int depth_i = 0;
				for (; depth_i < depth && k_invalid_u32 != cell_addr; ++depth_i)
				{
					auto local_cell_index = LoopInner();
					cell_addr = cell_pool_[cell_addr].child_addr[local_cell_index];
				}
				return { cell_addr, depth_i };
			}
			else
			{
				// 探索中に到達した最下層のCellデータを返すバージョン.
				int depth_i = 1;
				auto ret_data = cell_addr;
				auto ret_depth = 0;
				for (; depth_i <= depth; ++depth_i)
				{
					auto local_cell_index = LoopInner();
					cell_addr = cell_pool_[cell_addr].child_addr[local_cell_index];

					if (k_invalid_u32 == cell_addr)
						break;// 次の階層のデータがなければ途中で終了.
					ret_data = cell_addr;
					ret_depth = depth_i;
				}
				return { ret_data, ret_depth };
			}
		}
		
		//------------------------------------------------------------------------------------
		bool is_initialized_ = false;
		
		RayTracingHierarchicalGrid<64> bgrid_ = {};
		int						leaf_cell_space_reso_ = 1;
		float					bottom_cell_width_ws_ = 1.0f;
		float					brick_elem_width_ws_ = 1.0f;

		// Cellプール.
		TBitArray<> cell_pool_flag_ = {};
		TArray<CellData> cell_pool_ = {};

		// リーフのBrickプール.
		TBitArray<> bit_occupancy_brick_pool_flag_ = {};
		TArray<OccupancyGridLeafData> bit_occupancy_brick_pool_ = {};

		TBitArray<> particle_pool_flag_ = {};
		struct ParticleTestData
		{
			FVector		pos;
			float		life_sec;
			FVector		vel;
		};
		TArray<ParticleTestData> particle_pool_;
	};
	// ------------------------------------------------------------------------------------------------------------------------------------------------




	// ------------------------------------------------------------------------------------------------------------------------------------------------
	// 格子ベース流体用SparseGrid構造.
	//	近傍処理などのため階層化はなし. 実態はHashGrid.
	template<int k_num_level = 1>
	class SparseGridFluid
	{
	public:
		static constexpr int k_max_level = k_num_level - 1;
		static_assert(0 <= k_max_level);


		// 原論文では Block下に 2^3 Subblock があり, 各Subblockが 2^3 Cell を持つことで, 4^3 Cell の塊となっていたが, 構造の簡易化のため Blockが直接 4^3 Cell を持つ形式で検証する.
		static constexpr int k_cell_brick_reso = 4;
		static constexpr int k_cell_brick_vol3d = k_cell_brick_reso * k_cell_brick_reso * k_cell_brick_reso;
		static constexpr int k_cell_brick_max_index = k_cell_brick_reso - 1;

		// 現状はCell(シミュレーション解像度)のサイズは固定.
		static constexpr float k_cell_width_ws = 100.0f;
		static constexpr float k_cell_width_inv_ws = 1.0f / k_cell_width_ws;
		// CellをまとめるBlockのサイズ.
		static constexpr float k_block_width_ws = k_cell_brick_reso * k_cell_width_ws;
		static constexpr float k_block_width_inv_ws = 1.0f / k_block_width_ws;


		// ----------------------------------------------------------------------------------------------------------------------------------
			using Vec3iCode = uint32_t;
			static constexpr uint32_t k_Vec3iCode_bitwidth_x = 11;
			static constexpr uint32_t k_Vec3iCode_bitwidth_y = 11;
			static constexpr uint32_t k_Vec3iCode_bitwidth_z = 10;
			static_assert((sizeof(Vec3iCode) * 8) >= (k_Vec3iCode_bitwidth_x + k_Vec3iCode_bitwidth_y + k_Vec3iCode_bitwidth_z));

			static constexpr uint32_t k_Vec3iCode_mask_x = (1u << k_Vec3iCode_bitwidth_x) - 1;
			static constexpr uint32_t k_Vec3iCode_mask_y = (1u << k_Vec3iCode_bitwidth_y) - 1;
			static constexpr uint32_t k_Vec3iCode_mask_z = (1u << k_Vec3iCode_bitwidth_z) - 1;


			static constexpr Vec3iCode Vec3iToCode(FIntVector pi)
			{
				return (pi.X & k_Vec3iCode_mask_x) | ((pi.Y & k_Vec3iCode_mask_y) << (k_Vec3iCode_bitwidth_x)) | ((pi.Z & k_Vec3iCode_mask_z) << (k_Vec3iCode_bitwidth_x + k_Vec3iCode_bitwidth_y));
			}
			static constexpr FIntVector CodeToVec3i(Vec3iCode code)
			{
				return { (code & k_Vec3iCode_mask_x), ((code >> k_Vec3iCode_bitwidth_x) & k_Vec3iCode_mask_y), ((code >> (k_Vec3iCode_bitwidth_x + k_Vec3iCode_bitwidth_y)) & k_Vec3iCode_mask_z) };
			}
		// ----------------------------------------------------------------------------------------------------------------------------------
			static constexpr uint32_t BrickLocalIdToLocalIndex(FIntVector cell_brick_local_id)
			{
				return (cell_brick_local_id.X) + (cell_brick_local_id.Y * k_cell_brick_reso) + (cell_brick_local_id.Z * k_cell_brick_reso * k_cell_brick_reso);
			}
			static constexpr std::tuple<int,int,int> LocalIndexToBrickLocalId(uint32_t index)
			{
				return { index % k_cell_brick_reso, index / (k_cell_brick_reso) % k_cell_brick_reso, index / (k_cell_brick_reso * k_cell_brick_reso) };
			}
		// ----------------------------------------------------------------------------------------------------------------------------------


		// ----------------------------------------------------------------------------------------------------------------------------------
		// 可変長で確保効率とメモリ局所性をある程度考慮したPool. オブジェクトをSubPool単位で確保してPoolを伸長する.
			template<typename T, int k_elem_count>
			struct SubPool
			{
				std::array<T, k_elem_count>	sub_pool;
				std::bitset<k_elem_count>	sub_pool_used;

				int FindUnused() const
				{
					// 素直に.
					for (auto i = 0; i < sub_pool_used.size(); ++i)
					{
						if (!sub_pool_used[i])
							return i;
					}
					return -1;// 無し.
				}
			};

			using PoolElemId = uint32_t;// SubPoolとその内部要素を識別するID.
			static constexpr PoolElemId k_PoolElemId_invalid = ~PoolElemId(0);
		
			// k_subpool_elem_count_log2 : Subpool一つが確保する要素数のLog2を指定する. 7 -> 2^7=128要素.
			// 要素は PoolElemId で識別するが, 実際にはただのインデックスで, 内部的にどのSubpoolに対応するかを下位ビットをマスクしているだけである.
			template<typename T, int k_subpool_elem_count_log2 = 7>
			struct Pool
			{
				static constexpr uint32_t k_subpool_elem_count = 1 << k_subpool_elem_count_log2;
				// 0 <-> k_subpool_elem_count-1 のインデックスを保持するのに必要なビット数.
				static constexpr uint32_t k_subpool_index_mask = (1 << k_subpool_elem_count_log2) - 1;


				using SubPoolType = SubPool<T, k_subpool_elem_count>;
				using SubPoolId = uint32_t;//SubPool識別ID.
				using SubPoolElemIndex = uint32_t;//SubPool内要素インデックス.


				static constexpr PoolElemId ToPoolElemId(SubPoolId subpool, SubPoolElemIndex local_index)
				{
					// 単純に下位 k_subpool_elem_count_log2 ビットがSubpool内インデックス, 上位ビット部がSubpoolインデックス.
					return (uint32_t(subpool) << k_subpool_elem_count_log2) | (local_index & k_subpool_index_mask);
				}
				static constexpr std::tuple<SubPoolId, SubPoolElemIndex> fromPoolElemId(PoolElemId ref_id)
				{
					// 参照IDからpool番号と内部インデックスを復元.
					return { ref_id >> k_subpool_elem_count_log2, ref_id & k_subpool_index_mask };
				}

				PoolElemId Alloc()
				{
					// 空きのあるSubPool検索.
					auto poolid = subpool_full_flag_.Find(false);
					if (INDEX_NONE == poolid)
					{
						// 空きがなければSubPoolを新規追加.
						poolid = sub_pool_.Num();
						// SubPool追加.
						sub_pool_.Add( new SubPoolType() );
						// SubPoolのフラグはすべてクリア.
						sub_pool_[poolid]->sub_pool_used.reset();

						subpool_full_flag_.Add(false);// fullフラグも追加.
					}
					// SubPool内の空き検索.
					const auto local_index = sub_pool_[poolid]->FindUnused();
					assert(INDEX_NONE != local_index);
					sub_pool_[poolid]->sub_pool_used[local_index] = true;// 使用状態に.
					subpool_full_flag_[poolid] = sub_pool_[poolid]->sub_pool_used.all();// SubPoolのFullフラグ更新.

					return ToPoolElemId(poolid, local_index);
				}
				void Dealloc(PoolElemId id)
				{
					assert(k_PoolElemId_invalid != id);
					const auto [poolid, index] = fromPoolElemId(id);
					assert(k_subpool_elem_count > index);
					assert(sub_pool_.Num() > poolid);
					assert(sub_pool_[poolid]->sub_pool_used[index]);

					sub_pool_[poolid]->sub_pool_used[index] = false;
					subpool_full_flag_[poolid] = sub_pool_[poolid]->sub_pool_used.all();// SubPoolのFullフラグ更新.
				}

				// Used状態を取得.
				bool IsUsed(PoolElemId id) const
				{
					assert(k_PoolElemId_invalid != id);
					const auto [poolid, index] = fromPoolElemId(id);
					assert(k_subpool_elem_count > index);
					return sub_pool_[poolid]->sub_pool_used[index];
				}

				// Getter.
				const T& Get(PoolElemId id) const
				{
					assert(k_PoolElemId_invalid != id);
					const auto [poolid, index] = fromPoolElemId(id);
					assert(k_subpool_elem_count > index);
					assert(sub_pool_[poolid]->sub_pool_used[index]);
					return sub_pool_[poolid]->sub_pool[index];
				}
				// Getter.
				T& Get(PoolElemId id)
				{
					assert(k_PoolElemId_invalid != id);
					const auto [poolid, index] = fromPoolElemId(id);
					assert(k_subpool_elem_count > index);
					assert(sub_pool_[poolid]->sub_pool_used[index]);
					return sub_pool_[poolid]->sub_pool[index];
				}

				Pool() = default;
				~Pool()
				{
					// Subpool要素解放.
					for (auto* p : sub_pool_)
					{
						assert(nullptr != p);
						delete p;
					}
					sub_pool_ = {};
					subpool_full_flag_ = {};
				}

				// 要素最大数. 確保済みSubpool数 * Subpool内要素数.
				// 0ベースの単一インデックスで全要素を巡回する場合などに利用. その場合のGet()への引数はそのままインデックス値を渡せば内部の対応Subpool計算は正常動作する.
				uint32_t Num() const
				{
					return sub_pool_.Num() * k_subpool_elem_count;
				}

				// Cell SubPool のFull状態チェック.
				TBitArray<>				subpool_full_flag_ = {};
				// Cell SubPool へのポインタ配列.
				TArray<SubPoolType*>	sub_pool_ = {};
			};
		// ----------------------------------------------------------------------------------------------------------------------------------


		// NxNxN のCellBrick.
		struct CellBrick
		{
			// Cellは塊で管理.
			// 並列処理用にダブルバッファ.
			std::array<std::array<FVector, k_cell_brick_vol3d>, 2> vel;


			// Divergence用ワーク.
			std::array<float, k_cell_brick_vol3d>					work_divergence;
			// Pressure用ワーク. Pingpongのためにダブルバッファ.
			std::array<std::array<float, k_cell_brick_vol3d>, 2>	work_pressure;


			// 近傍のBlockへの参照. CellBrickではなくBlockである点に注意(位置情報等も欲しいため). 26近傍+自身の親で27.
			std::array<PoolElemId, 27>	block_link;
		};

		// CellBrickを保持するBlock.
		struct Block
		{
			// HashKeyとなるCodeも保持. 自身の位置を復元する必要がありそうなため.
			Vec3iCode position_code = {};
			// CellBrickへの参照. 参照先はリニアバッファ上の位置かも知れないし, ObjectPoolのIDかもしれない.
			PoolElemId cell_brick_addr = k_PoolElemId_invalid;
		};



		// 現実装ではシミュレーション空間の範囲を原点中心で固定.
		static constexpr int k_sim_space_block_min_x = -(int(k_Vec3iCode_mask_x) / 2.0f) - 0.5f;
		static constexpr int k_sim_space_block_min_y = -(int(k_Vec3iCode_mask_y) / 2.0f) - 0.5f;
		static constexpr int k_sim_space_block_min_z = -(int(k_Vec3iCode_mask_z) / 2.0f) - 0.5f;
		inline static const FIntVector	k_sim_space_block_min_i = { k_sim_space_block_min_x, k_sim_space_block_min_y, k_sim_space_block_min_z };
		inline static const FVector		k_sim_space_block_min_f = { k_sim_space_block_min_x, k_sim_space_block_min_y, k_sim_space_block_min_z };

		// world座標を指定のMipレベルにおけるBlock座標に変換.
		//	mip_level=0				-> 最も精細な解像度.
		//	mip_level=k_max_level	-> 最も粗い解像度.
		FVector WorldToBlock(int mip_level, FVector world_pos) const
		{
			const auto block_pos_f = world_pos * k_block_width_inv_ws - k_sim_space_block_min_f;
			return block_pos_f / (1 << mip_level);// Mip0から指定Mipでの位置へ変換.
		}
		// 特定のMipレベルにおけるBlock座標をWorld座標に変換.
		FVector BlockToWorld(int mip_level, FVector block_pos) const
		{
			const auto block_pos_f = block_pos * (1 << mip_level);// Mip0相当でのBlock位置.
			return (block_pos_f + k_sim_space_block_min_f)* k_block_width_ws;
		}

	public:
		SparseGridFluid()
		{
		}
		~SparseGridFluid()
		{
			Finalize();
		}

		bool Initialize()
		{
			is_initialized_ = true;
			return true;
		}
		bool IsInitialized() const
		{
			return is_initialized_;
		}

		bool Finalize()
		{
			// テストも兼ねて全部破棄.
			{
				for (int mip = 0; mip < k_num_level; ++mip)
				{
					for (auto pooli = 0; pooli < cell_brick_pool_[mip].sub_pool_.Num(); ++pooli)
					{
						for (auto i = 0; i < cell_brick_pool_[mip].sub_pool_[pooli]->sub_pool.size(); ++i)
						{
							if (!cell_brick_pool_[mip].sub_pool_[pooli]->sub_pool_used[i])
								continue;
							// ID作成
							const auto alloc_id = cell_brick_pool_[mip].ToPoolElemId(pooli, i);
							// 解放.
							cell_brick_pool_[mip].Dealloc(alloc_id);
						}
					}
					// 全破棄チェック.
					assert(0 == cell_brick_pool_[mip].subpool_full_flag_.CountSetBits());
					for (auto i = 0; i < cell_brick_pool_[mip].subpool_full_flag_.Num(); ++i)
					{
						if (cell_brick_pool_[mip].subpool_full_flag_[i])
						{
							assert(false);
						}
					}
				}
			}
			{
				for (int mip = 0; mip < k_num_level; ++mip)
				{
					auto& pool = block_pool_[mip];
					// テストも兼ねて全部破棄.
					for (auto pooli = 0; pooli < pool.sub_pool_.Num(); ++pooli)
					{
						for (auto i = 0; i < pool.sub_pool_[pooli]->sub_pool.size(); ++i)
						{
							if (!pool.sub_pool_[pooli]->sub_pool_used[i])
								continue;
							// ID作成
							const auto alloc_id = pool.ToPoolElemId(pooli, i);
							// 解放.
							pool.Dealloc(alloc_id);
						}
					}

					// 全破棄チェック.
					assert(0 == pool.subpool_full_flag_.CountSetBits());
					for (auto i = 0; i < pool.subpool_full_flag_.Num(); ++i)
					{
						if (pool.subpool_full_flag_[i])
						{
							assert(false);
						}
					}
				}
			}
			// HashTableクリア.
			for (auto i = 0; i < grid_hash_.size(); ++i)
			{
				grid_hash_[i].clear();
			}
			return true;
		}

		// ワールド座標サンプルでシミュレーション空間を通過.
		struct SamplePointInfo
		{
			FVector pos{};
			FVector dir{};
		};
		// Grid構造に要素を追加する.
		//	現在はカメラからのレイキャストヒット位置に要素を追加して物体表面に分布するようにしている.
		void AppendElements(const FVector& sample_ray_origin, const TArray<std::tuple<SamplePointInfo, bool>>& sample_ray_end_and_ishit)
		{
			const auto k_front = FrontBufferIndex();
			const auto k_back = BackBufferIndex();

			const FIntVector k_block_max(k_Vec3iCode_mask_x, k_Vec3iCode_mask_y, k_Vec3iCode_mask_z);
			// サンプル点の追加.
			for (const auto& sample : sample_ray_end_and_ishit)
			{
				const auto& [hit_info, hit] = sample;

				// ヒットしたサンプルのみ.
				if (!hit)
					continue;

				const auto block_pos_f = WorldToBlock(0, hit_info.pos);
				const auto block_pos_i = naga::math::FVectorFloorToInt(block_pos_f);

				// 範囲チェック.
				if (0 > block_pos_i.GetMin() || 0 > (k_block_max - block_pos_i).GetMin())
					continue;

				// Blockの位置Code.
				const auto block_code = Vec3iToCode(block_pos_i);

				const int mip_level = 0;
				auto find_block = grid_hash_[mip_level].find(block_code);// 検索.
				if (grid_hash_[mip_level].end() == find_block)
				{
					// Blocl割当.
					const auto alloc_block_id = block_pool_[mip_level].Alloc(); assert(k_PoolElemId_invalid != alloc_block_id);

					// CellBrickの割当.
					const auto alloc_cell_brick_id = cell_brick_pool_[mip_level].Alloc(); assert(k_PoolElemId_invalid != alloc_cell_brick_id);
					{
						// Brick初期化.
						auto& brick = cell_brick_pool_[mip_level].Get(alloc_cell_brick_id);
						
						// 近傍BlockLinkをクリア. 素直にアクセスするため27要素としているが工夫できるところはありそう.
						brick.block_link.fill(k_PoolElemId_invalid);

						brick.vel[k_front].fill(FVector::ZeroVector);// 速度ゼロクリア.
						brick.vel[k_back].fill(FVector::ZeroVector);// 速度ゼロクリア.
					}

					// Block初期化.
					auto& new_block = block_pool_[mip_level].Get(alloc_block_id);
					new_block =
					{
						block_code,
						alloc_cell_brick_id,
					};

					// HashTable登録.
					grid_hash_[mip_level][block_code] = alloc_block_id;
					// 後段処理のために再検索.
					find_block = grid_hash_[mip_level].find(block_code);
				}
				assert(grid_hash_[mip_level].end() != find_block);// 念のため.ind_block

				// Blockに対する処理.
				{
					// block内frac
					const auto block_frac = block_pos_f - FVector(block_pos_i);
					// cell_brick 4x4x4 内のcell座標.
					const auto local_cell_id =  naga::math::FVectorFloorToInt(block_frac * k_cell_brick_reso); assert(k_cell_brick_reso > local_cell_id.X && k_cell_brick_reso > local_cell_id.Y && k_cell_brick_reso > local_cell_id.Z);
					const auto local_cell_index = BrickLocalIdToLocalIndex(local_cell_id); assert(k_cell_brick_vol3d > local_cell_index);
					const auto block_id = find_block->second; assert(k_PoolElemId_invalid != block_id);

					auto& block = block_pool_[mip_level].Get(block_id);

					// CellBrick取得.
					auto& cell_brick = cell_brick_pool_[mip_level].Get(block.cell_brick_addr);
					
					// Cellの速度を上書き(値はテスト用).
					//cell_brick.vel[k_front][local_cell_index] = FVector(1.0f, 0.0f, 0.0f) * 1000.0f; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
					// 視線方向の速度でテスト.
					auto test_vel = hit_info.pos - sample_ray_origin;
					//cell_brick.vel[k_front][local_cell_index] = test_vel.GetSafeNormal() * 1000.0f; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
					test_vel = test_vel - test_vel.Dot(hit_info.dir)*hit_info.dir*1.1f;
					cell_brick.vel[k_front][local_cell_index] = test_vel.GetSafeNormal() * 1000.0f; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

					// 向きベクトルのXY平面投影で速度設定.
					//cell_brick.vel[k_front][local_cell_index] = ((hit_info.pos - sample_ray_origin)*FVector(1.0f, 1.0f, 0.0f)).GetSafeNormal() * 1000.0f; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
					// 固定方向.
					//cell_brick.vel[k_front][local_cell_index] = FVector::XAxisVector * 1000.0f; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
				}
			}
		}

		// グリッド構造の流体計算.
		void UpdateSystem(float delta_sec)
		{
			static constexpr float k_debug_duffusion_rate = 0.5f;
			//static constexpr float k_debug_duffusion_rate = 0.0f;
			static constexpr float k_debug_Attenuation_rate = 0.999f;
			
			const auto k_front = FrontBufferIndex();
			const auto k_back = BackBufferIndex();
			flip_ = k_back;// ここでフリップ.

			auto	progress_time_start = std::chrono::system_clock::now();

			const int mip_level = 0;


			// 要素の最大数. Pool管理での最大数なので抜けや末尾の不使用要素が含まれる.
			const uint32_t block_count = block_pool_[mip_level].Num();


			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			// 隣接情報 block_link の構築.
			{
				// 単方向13近傍方向.
				// この方向の近傍Nodeを探索して自身と相手の近傍情報を変更するのであればDataRaceが起きないため, Node単位並列化が可能になる.
				// 反対方向は別のNodeが処理をするので全Nodeの処理が終われば全方向(26方向)のApron処理が完了している.
				const FIntVector k_oneway_neighbor_dir[13] =
				{
					// 正のX,Y,Z方向の面で接している近傍.
					FIntVector(1,0,0), FIntVector(0,1,0), FIntVector(0,0,1),

					// 辺で接している近傍.
					FIntVector(1,1,0), FIntVector(1,-1,0),
					FIntVector(0,1,1), FIntVector(0,-1,1),
					FIntVector(1,0,1), FIntVector(1,0,-1),

					// 角で接している近傍
					FIntVector(1,1,1), FIntVector(1,-1,1), FIntVector(1,1,-1), FIntVector(1,-1,-1),
				};
				constexpr auto k_num_oneway_neighbor_dir = std::size(k_oneway_neighbor_dir);

				// PoolのSubPool毎に
				auto process_update_neighbor_link = [&](int i)
				{
					uint32_t block_addr = i;
					if (!block_pool_[mip_level].IsUsed(block_addr))
						return;

					auto& block = block_pool_[mip_level].Get(block_addr);
					const auto block_pos_i = CodeToVec3i(block.position_code);
					const auto block_pos = BlockToWorld(mip_level, FVector(block_pos_i));
					assert(k_PoolElemId_invalid != block.cell_brick_addr);
					auto& cell_brick = cell_brick_pool_[mip_level].Get(block.cell_brick_addr);

					// 自分自身を持つBlockの参照も一応 0,0,0 に格納.
					const auto self_block_index_self = (0 + 1) + ((0 + 1) * 3) + ((0 + 1) * 3 * 3);
					cell_brick.block_link[self_block_index_self] = block_addr;


					// 近傍Blockについて相互に情報更新.

					// 単方向近傍の探索.
					for (auto di = 0; di < k_num_oneway_neighbor_dir; ++di)
					{
						// 自身から見たこの方向の近傍Blockのインデックス. 事前計算とかできそう.
						const auto neighbor_index_self = (k_oneway_neighbor_dir[di].X + 1) + ((k_oneway_neighbor_dir[di].Y + 1) * 3) + ((k_oneway_neighbor_dir[di].Z + 1) * 3 * 3);
						// 相手から見た自身の近傍インデックス. 方向ベクトルの反転から計算.
						const auto neighbor_index_neighbor = (-k_oneway_neighbor_dir[di].X + 1) + ((-k_oneway_neighbor_dir[di].Y + 1) * 3) + ((-k_oneway_neighbor_dir[di].Z + 1) * 3 * 3);

						const auto neightbor_pos_i = block_pos_i + k_oneway_neighbor_dir[di];

						// 近傍Blockの問い合わせ.
						auto n_find = grid_hash_[mip_level].find(Vec3iToCode(neightbor_pos_i));
						if ((n_find != grid_hash_[mip_level].end()))
						{
							const auto neighbor_block_addr = n_find->second;// 近傍BlockID
							const auto neighbor_cell_brick_addr = block_pool_[mip_level].Get(neighbor_block_addr).cell_brick_addr;
							// 相手側のCellの近傍情報書き換えも実行するため参照取得.
							auto& neighbor_cell_brick = cell_brick_pool_[mip_level].Get(neighbor_cell_brick_addr);


							cell_brick.block_link[neighbor_index_self] = neighbor_block_addr;// 自身の近傍情報に相手を書き込み.
							neighbor_cell_brick.block_link[neighbor_index_neighbor] = block_addr;// 自身の近傍情報に相手を書き込み. 全てのBlockが並列実行されていても, 単方向近傍のアクセスなので競合は発生しない!.
						}
						else
						{
							// 該当近傍は存在しない.

							cell_brick.block_link[neighbor_index_self] = k_PoolElemId_invalid;// 無効値.
						}
					}

				};

	#	if 1
				// 並列.
				ParallelFor( block_count, process_update_neighbor_link );
	#	else
				// 直列.
				for (auto i = 0u; i < block_count; ++i)
				{ process_update_neighbor_link(i); }
	#	endif
			}




			// 注目CellBrickの0,0,0を基準とした近傍Cell位置から対応するBlockとCell位置を返す.
			// CellBrickにキャッシュされた近傍BlockLinkを利用する.
			// rel_cell_pos=(-1,0,0) の場合CellBrickの0,0,0のX方向-1のCellに関する情報を返す.
			auto GetNeighborCellBrickIdAndPos = [&](CellBrick& cell_brick, const FIntVector& rel_cell_pos)
			-> std::tuple<PoolElemId, FIntVector>
			{
				const FIntVector comp_min_bound = naga::math::FVectorCompareLess(rel_cell_pos, FIntVector::ZeroValue);
				const FIntVector comp_max_bound = naga::math::FVectorCompareGreater(rel_cell_pos, FIntVector(k_cell_brick_max_index));
				// 負の範囲外なら-1, 正の範囲外なら+, 範囲内なら0.
				const FIntVector comp_stat = comp_max_bound - comp_min_bound;

				const FIntVector neighbor_cellbrick_lookup = comp_stat + FIntVector(1, 1, 1);// -1,0,1 に +1 して　0,1,2 のインデックスとする.
				// 直列インデックスへ.
				const int neighbor_cellbrick_lookup_index = neighbor_cellbrick_lookup.X + (neighbor_cellbrick_lookup.Y * 3) + (neighbor_cellbrick_lookup.Z * 3 * 3);
				const PoolElemId neighbor_cellbrick_id = cell_brick.block_link[neighbor_cellbrick_lookup_index];

				constexpr int k_neighbor_brick_offset[3] = { k_cell_brick_reso, 0, -k_cell_brick_reso };
				// 近傍BrickにおけるCell座標を計算するためのオフセット. -1=>負側の近傍での 3 に対応させるための値.
				const FIntVector neighbor_cell_offset = FIntVector(k_neighbor_brick_offset[neighbor_cellbrick_lookup.X], k_neighbor_brick_offset[neighbor_cellbrick_lookup.Y], k_neighbor_brick_offset[neighbor_cellbrick_lookup.Z]);
				const FIntVector neighbor_cell_pos = rel_cell_pos + neighbor_cell_offset;
				return { neighbor_cellbrick_id, neighbor_cell_pos };
			};



			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			// diffusion.
			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			auto diffusion_process = [&](int i)
			{
				uint32_t pool_id = i;
				if (!block_pool_[mip_level].IsUsed(pool_id))
					return;

				const auto& block = block_pool_[mip_level].Get(pool_id);
				const auto block_pos_i = CodeToVec3i(block.position_code);
				const auto block_pos = BlockToWorld(mip_level, FVector(block_pos_i)); assert(k_PoolElemId_invalid != block.cell_brick_addr);
				auto& cell_brick = cell_brick_pool_[mip_level].Get(block.cell_brick_addr);

				// 全Cellについて一様に近傍Cellを探索する. 近傍CellBrickへのアクセスが不要な範囲でも追加のコストがかかっているため高速化の余地あり.
				{
					for (int cz = 0; cz < k_cell_brick_reso; ++cz)
					{
						for (int cy = 0; cy < k_cell_brick_reso; ++cy)
						{
							for (int cx = 0; cx < k_cell_brick_reso; ++cx)
							{
								int num_valid_neighbor = 0;
								FVector neighbor_sum = FVector::ZeroVector;
								// 軸ごとの垂直近傍6つで計算,
								for (int oni = 0; oni < 6; ++oni)
								{
									const auto axis_sign = (oni & 0x01) * 2 - 1;// -1, +1
									const auto axis_component = (oni >> 1) & 0b11;// 0:x, 1:y, 2:z
									const auto neighbor_offset = FIntVector((0b001 >> axis_component) & 0x01, (0b010 >> axis_component) & 0x01, (0b100 >> axis_component) & 0x01) * axis_sign;

									auto [n_cellbrick_id, n_pos] = GetNeighborCellBrickIdAndPos(cell_brick, FIntVector(cx, cy, cz) + neighbor_offset);

									if (k_PoolElemId_invalid == n_cellbrick_id)
										continue;

									const auto& ref_cellbrick = cell_brick_pool_[mip_level].Get(n_cellbrick_id);
									const auto nci = BrickLocalIdToLocalIndex(n_pos);

									neighbor_sum += ref_cellbrick.vel[k_front][nci];
									num_valid_neighbor++;
								}

								// -----------------------------------------------------------------------------------
								//const auto neighbor_avg = neighbor_sum / (3 * 3 * 3);// 無効Cellを真空とする場合.
								const auto neighbor_avg = (0 < num_valid_neighbor) ? neighbor_sum / (num_valid_neighbor) : neighbor_sum;// 無効Cellを壁とする場合.
								// -----------------------------------------------------------------------------------

								const auto ci = BrickLocalIdToLocalIndex({ cx, cy, cz });
								// 拡散
								FVector next_vel = cell_brick.vel[k_front][ci] + (neighbor_avg - cell_brick.vel[k_front][ci]) * delta_sec * k_debug_duffusion_rate;
								// 減衰
								next_vel = next_vel + (next_vel * k_debug_Attenuation_rate - next_vel) * delta_sec;
								cell_brick.vel[k_back][ci] = next_vel;
							}
						}
					}
				}
			};

	#	if 1
			// 並列.
			ParallelFor( block_count, diffusion_process );
	#	else
			// 直列.
			for (auto i = 0u; i < block_count; ++i)
			{ diffusion_process(i); }
	#	endif



			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			// divergence.
			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			const auto vel_flip = k_back;
			auto divergence_process = [&, vel_flip](int i)
			{
				uint32_t pool_id = i;
				if (!block_pool_[mip_level].IsUsed(pool_id))
					return;

				const auto& block = block_pool_[mip_level].Get(pool_id);
				const auto block_pos_i = CodeToVec3i(block.position_code);
				const auto block_pos = BlockToWorld(mip_level, FVector(block_pos_i)); assert(k_PoolElemId_invalid != block.cell_brick_addr);
				auto& cell_brick = cell_brick_pool_[mip_level].Get(block.cell_brick_addr);

				// 全Cellについて一様に近傍Cellを探索する. 近傍CellBrickへのアクセスが不要な範囲でも追加のコストがかかっているため高速化の余地あり.
				{
					for (int cz = 0; cz < k_cell_brick_reso; ++cz)
					{
						for (int cy = 0; cy < k_cell_brick_reso; ++cy)
						{
							for (int cx = 0; cx < k_cell_brick_reso; ++cx)
							{
								const auto ci = BrickLocalIdToLocalIndex({cx, cy, cz});
								const auto cell_vel = cell_brick.vel[vel_flip][ci];

								float cell_divergence = 0.0f;
								// 軸ごとの垂直近傍6つで計算,
								for (int oni = 0; oni < 6; ++oni)
								{
									const auto axis_sign = (oni & 0x01) * 2 - 1;// -1, +1
									const auto axis_component = (oni >> 1) & 0b11;// 0:x, 1:y, 2:z
									const auto neighbor_offset = FIntVector( (0b001 >> axis_component)&0x01, (0b010 >> axis_component)&0x01, (0b100 >> axis_component)&0x01) * axis_sign;

									auto [n_cellbrick_id, n_pos] = GetNeighborCellBrickIdAndPos(cell_brick, FIntVector(cx, cy, cz) + neighbor_offset);

									if (k_PoolElemId_invalid == n_cellbrick_id)
										continue;

									const auto& ref_cellbrick = cell_brick_pool_[mip_level].Get(n_cellbrick_id);
									const auto nci = BrickLocalIdToLocalIndex(n_pos);

									// Divergence.
									// 中央差分であるため後で 1/2 することに注意.
									cell_divergence += (neighbor_offset.X * ref_cellbrick.vel[vel_flip][nci].X) + (neighbor_offset.Y * ref_cellbrick.vel[vel_flip][nci].Y) + (neighbor_offset.Z * ref_cellbrick.vel[vel_flip][nci].Z);
								}

								// 中央差分のため 1/2 
								cell_brick.work_divergence[ci] = cell_divergence * 0.5f;
							}
						}
					}
				}
			};

	#	if 1
			// 並列.
			ParallelFor( block_count, divergence_process );
	#	else
			// 直列.
			for (auto i = 0u; i < block_count; ++i)
			{ divergence_process(i); }
	#	endif



			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			// pressure buffer reset.
			//	poisson equation iterationの前にバッファクリア. 別のループのついでにやったほうが効率良いかも.
			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			auto pressure_prepare_process = [&](int i)
			{
				uint32_t pool_id = i;
				if (!block_pool_[mip_level].IsUsed(pool_id))
					return;

				const auto& block = block_pool_[mip_level].Get(pool_id);
				const auto block_pos_i = CodeToVec3i(block.position_code);
				const auto block_pos = BlockToWorld(mip_level, FVector(block_pos_i)); assert(k_PoolElemId_invalid != block.cell_brick_addr);
				auto& cell_brick = cell_brick_pool_[mip_level].Get(block.cell_brick_addr);

				//cell_brick.work_pressure[pressure_flip_].fill(0.0f);// 0クリア.

				// 初回のdivergence項を設定することで全体として効率化の意図.

				for (auto ci = 0; ci < k_cell_brick_vol3d; ++ci)
				{
					const float divergence = cell_brick.work_divergence[ci];
					const float initial_pressure_value = (0.0f - (divergence / delta_sec)) / 6.0f;
					cell_brick.work_pressure[pressure_flip_][ci] = initial_pressure_value;
				}

			};
	#	if 1
			// 並列.
			ParallelFor(block_count, pressure_prepare_process);
	#	else
			// 直列.
			for (auto i = 0u; i < block_count; ++i)
			{
				pressure_prepare_process(i);
			}
	#	endif


			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			// pressure.
			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			auto pressure_process = [&, delta_sec](int i)
			{
				uint32_t pool_id = i;
				if (!block_pool_[mip_level].IsUsed(pool_id))
					return;

				const auto& block = block_pool_[mip_level].Get(pool_id);
				const auto block_pos_i = CodeToVec3i(block.position_code);
				const auto block_pos = BlockToWorld(mip_level, FVector(block_pos_i)); assert(k_PoolElemId_invalid != block.cell_brick_addr);
				auto& cell_brick = cell_brick_pool_[mip_level].Get(block.cell_brick_addr);

				// 全Cellについて一様に近傍Cellを探索する. 近傍CellBrickへのアクセスが不要な範囲でも追加のコストがかかっているため高速化の余地あり.
				{
					for (int cz = 0; cz < k_cell_brick_reso; ++cz)
					{
						for (int cy = 0; cy < k_cell_brick_reso; ++cy)
						{
							for (int cx = 0; cx < k_cell_brick_reso; ++cx)
							{
								const auto ci = BrickLocalIdToLocalIndex({ cx, cy, cz });
								const auto cell_vel = cell_brick.vel[vel_flip][ci];


								// divergence.
								const float divergence = cell_brick.work_divergence[ci];
								const float pressure_prev = cell_brick.work_pressure[pressure_flip_][ci];

								float accum_pressure = 0.0f;
								// 軸ごとの垂直近傍6つで計算,
								for (int oni = 0; oni < 6; ++oni)
								{
									const auto axis_sign = (oni & 0x01) * 2 - 1;// -1, +1
									const auto axis_component = (oni >> 1) & 0b11;// 0:x, 1:y, 2:z
									const auto neighbor_offset = FIntVector((0b001 >> axis_component) & 0x01, (0b010 >> axis_component) & 0x01, (0b100 >> axis_component) & 0x01) * axis_sign;

									auto [n_cellbrick_id, n_pos] = GetNeighborCellBrickIdAndPos(cell_brick, FIntVector(cx, cy, cz) + neighbor_offset);

									if (k_PoolElemId_invalid == n_cellbrick_id)
									{
										// 無効Cellの場合. 圧力差無し(壁)とする場合は自身のPressureをコピー.
										accum_pressure += pressure_prev;
									}
									else
									{
										const auto& ref_cellbrick = cell_brick_pool_[mip_level].Get(n_cellbrick_id);
										const auto nci = BrickLocalIdToLocalIndex(n_pos);

										accum_pressure += ref_cellbrick.work_pressure[pressure_flip_][nci];
									}
								}

								// difference method of Pressure Poisson Equation.
								const float pressure_next = (accum_pressure - (divergence / delta_sec)) / 6.0f;

								// 結果を次のフリップバッファに書き込み.
								cell_brick.work_pressure[1 - pressure_flip_][ci] = pressure_next;
							}
						}
					}
				}
			};
			
			// Pressure Poisson Equation 反復数.
			const int k_num_pressure_iteration = 3;
			for (int pressure_itr = 0; pressure_itr < k_num_pressure_iteration; ++pressure_itr)
			{
	#	if 1
				// 並列.
				ParallelFor(block_count, pressure_process);
	#	else
				// 直列.
				for (auto i = 0u; i < block_count; ++i)
				{
					divergence_process(i);
				}
	#	endif

				// フリップ.
				pressure_flip_ = 1 - pressure_flip_;
			}



			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			// apply pressure.
			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			auto pressure_apply_process = [&](int i)
			{
				uint32_t pool_id = i;
				if (!block_pool_[mip_level].IsUsed(pool_id))
					return;

				const auto& block = block_pool_[mip_level].Get(pool_id);
				const auto block_pos_i = CodeToVec3i(block.position_code);
				const auto block_pos = BlockToWorld(mip_level, FVector(block_pos_i)); assert(k_PoolElemId_invalid != block.cell_brick_addr);
				auto& cell_brick = cell_brick_pool_[mip_level].Get(block.cell_brick_addr);

				// 全Cellについて一様に近傍Cellを探索する. 近傍CellBrickへのアクセスが不要な範囲でも追加のコストがかかっているため高速化の余地あり.
				{
					for (int cz = 0; cz < k_cell_brick_reso; ++cz)
					{
						for (int cy = 0; cy < k_cell_brick_reso; ++cy)
						{
							for (int cx = 0; cx < k_cell_brick_reso; ++cx)
							{
								const auto ci = BrickLocalIdToLocalIndex({ cx, cy, cz });

								const auto self_pressure = cell_brick.work_pressure[pressure_flip_][ci];

								FVector accum_pressure_grad = FVector::ZeroVector;
								// 軸ごとの垂直近傍6つで計算,
								for (int oni = 0; oni < 6; ++oni)
								{
									const auto axis_sign = (oni & 0x01) * 2 - 1;// -1, +1
									const auto axis_component = (oni >> 1) & 0b11;// 0:x, 1:y, 2:z
									const auto neighbor_offset = FIntVector((0b001 >> axis_component) & 0x01, (0b010 >> axis_component) & 0x01, (0b100 >> axis_component) & 0x01) * axis_sign;

									auto [n_cellbrick_id, n_pos] = GetNeighborCellBrickIdAndPos(cell_brick, FIntVector(cx, cy, cz) + neighbor_offset);

									if (k_PoolElemId_invalid == n_cellbrick_id)
									{
										// 無効Cellの場合. 圧力差無しとする場合は自身のPressureと同値として勾配無し.
										accum_pressure_grad += FVector(neighbor_offset) * self_pressure;
									}
									else
									{
										const auto& ref_cellbrick = cell_brick_pool_[mip_level].Get(n_cellbrick_id);
										const auto nci = BrickLocalIdToLocalIndex(n_pos);
										// 中央差分勾配のため中心からの相対方向でベクトルとする.
										accum_pressure_grad += FVector(neighbor_offset) * (ref_cellbrick.work_pressure[pressure_flip_][nci]);
									}
								}

								// Pressure.
								// 圧力の低い方への勾配となるので負.
								// 中央差分のため 1/2 
								cell_brick.vel[vel_flip][ci] += -accum_pressure_grad * 0.5f * delta_sec;
							}
						}
					}
				}
			};

	#	if 1
			// 並列.
			ParallelFor(block_count, pressure_apply_process);
	#	else
			// 直列.
			for (auto i = 0u; i < block_count; ++i)
			{
				pressure_apply_process(i);
			}
	#	endif



			// 計測.
			if (true)
			{
				static int s_debug_print_interval_counter = 0;
				if (0 == (s_debug_print_interval_counter % 30))
				{
					// 計算時間
					size_t progress_time_ms;
					progress_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - progress_time_start).count();
					UE_LOG(LogTemp, Display, TEXT("UpdateSystem: %d [microsec]"), progress_time_ms);
				}

				++s_debug_print_interval_counter;
				if (1000000 < s_debug_print_interval_counter)
					s_debug_print_interval_counter = 0;
			}
		}




		// パーティクル投入テスト.
		void AddParticleTest(const FVector& pos, const FVector& vel)
		{
			constexpr int k_particle_max_test = 1000;
			// Cell確保.
			auto pool_index = particle_pool_flag_.FindAndSetFirstZeroBit();
			if (INDEX_NONE == pool_index)
			{
				// プール最大でクランプ.
				if (k_particle_max_test <= particle_pool_flag_.Num())
					return;
				pool_index = particle_pool_flag_.Num();

				particle_pool_flag_.Add(true);
				particle_pool_.Add({});
			}
			else
			{
				particle_pool_flag_[pool_index] = true;// 有効化.
			}
			particle_pool_.EmplaceAt(pool_index, ParticleTestData{ pos, 0.0f, vel });
		}
		void UpdateParticle(float delta_sec)
		{

			auto update_process = [&](int i)
			{
				if (!particle_pool_flag_[i])
					return;

				const int mip_level = 0;
				const int vel_flip = FrontBufferIndex();

				auto candidate_pos = particle_pool_[i].pos + particle_pool_[i].vel * delta_sec;

				// 速度場.
				const auto block_pos_f = WorldToBlock(0, candidate_pos);
				const auto block_pos_i = naga::math::FVectorFloorToInt(block_pos_f);
				auto find_block = grid_hash_[mip_level].find(Vec3iToCode(block_pos_i));
				if (find_block != grid_hash_[mip_level].end())
				{
					// block内frac
					const auto block_frac = block_pos_f - FVector(block_pos_i);
					// cell_brick 4x4x4 内のcell座標.
					const auto local_cell_id = naga::math::FVectorFloorToInt(block_frac * k_cell_brick_reso); assert(k_cell_brick_reso > local_cell_id.X && k_cell_brick_reso > local_cell_id.Y && k_cell_brick_reso > local_cell_id.Z);
					const auto local_cell_index = BrickLocalIdToLocalIndex(local_cell_id); assert(k_cell_brick_vol3d > local_cell_index);
					const auto block_id = find_block->second; assert(k_PoolElemId_invalid != block_id);

					auto& block = block_pool_[mip_level].Get(block_id);

					// CellBrick取得.
					auto& cell_brick = cell_brick_pool_[mip_level].Get(block.cell_brick_addr);
					// 適当に速度場に寄せる.
					particle_pool_[i].vel += (cell_brick.vel[vel_flip][local_cell_index] - particle_pool_[i].vel) * FMath::Clamp(5.0f * delta_sec, 0.0f, 1.0f);
					//particle_pool_[i].vel += cell_brick.vel[vel_flip][local_cell_index] * (1.5f) * delta_sec;
				}


				particle_pool_[i].pos = candidate_pos;
				particle_pool_[i].life_sec += delta_sec;

				if (20.0f < particle_pool_[i].life_sec)
				{
					particle_pool_flag_[i] = false;
				}
			};

	#	if 1
			// 並列.
			ParallelFor(particle_pool_flag_.Num(), update_process);
	#	else
			// 直列.
			for (auto i = 0u; i < particle_pool_flag_.Num(); ++i)
			{
				update_process(i);
			}
	#	endif
		}




		int FrontBufferIndex() const { return flip_; }
		int BackBufferIndex() const { return 1 - flip_; }

		int flip_ = 0;

		int pressure_flip_ = 0;

		bool is_initialized_ = false;

		// TODO. 直接MapのValueにBlockを持つよりPoolIDを持たせるたいかもしれない, Block自体が小さいなら問題ないかも.
		std::array<std::unordered_map<Vec3iCode, PoolElemId>, k_num_level> grid_hash_;

		// MipLevel毎のBlockのpool
		std::array<Pool<Block, 7>, k_num_level> block_pool_ = {};

		// MipLevel毎のCellBrickのPool.
		std::array<Pool<CellBrick, 7>, k_num_level> cell_brick_pool_ = {};


		// テスト用のパーティクル.
		TBitArray<> particle_pool_flag_ = {};
		struct ParticleTestData
		{
			FVector		pos;
			float		life_sec;
			FVector		vel;
		};
		TArray<ParticleTestData> particle_pool_;

	};

		
}