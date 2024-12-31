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

	struct NglOccupancyGridLeafData
	{
		constexpr NglOccupancyGridLeafData()
		{
			occupancy_4x4x4 = {};
		}
		constexpr NglOccupancyGridLeafData(uint64_t v)
		{
			occupancy_4x4x4 = v;
		}
		static constexpr NglOccupancyGridLeafData FullBrick()
		{
			return NglOccupancyGridLeafData(~uint64_t(0));
		}
		static constexpr NglOccupancyGridLeafData ZeroBrick()
		{
			return NglOccupancyGridLeafData(uint64_t(0));
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
	class NglOccupancyGrid
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
		NglOccupancyGrid()
		{
		}
		~NglOccupancyGrid()
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
			const NglOccupancyGrid& grid_impl;


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
			const NglOccupancyGrid& grid_impl;

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
			const NglOccupancyGrid& grid_impl;

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
		TArray<NglOccupancyGridLeafData> bit_occupancy_brick_pool_ = {};

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
}