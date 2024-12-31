#pragma once

#include <tuple>
#include <atomic>
#include <thread>
#include <chrono>
#include <assert.h>

#include "Async/AsyncWork.h"

namespace naga
{
	class FAsyncTaskBase
	{
	private:
		// 非同期実行される関数.
		// 派生クラスで実装.
		virtual void AsyncUpdate()
		{
			// TODO.
		}
	public:
		class FAsyncFunc : public FNonAbandonableTask
		{
			friend class FAsyncTask<FAsyncFunc>;

		public:
			FAsyncFunc(TFunction<void()> InWork)
				: Work(InWork){}
			void DoWork()
			{
				Work();
			}
			FORCEINLINE TStatId GetStatId() const
			{
				RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncFunc, STATGROUP_ThreadPoolAsyncTasks);
			}
		private:
			TFunction<void()> Work;
		};
	public:
		FAsyncTaskBase();
		virtual ~FAsyncTaskBase();

		// Asyncタスクが完了しているか.
		bool IsDone() const;

		// Asyncタスクの完了を待機.
		void WaitAsyncUpdate();

		// Asyncタスクを起動.
		void StartAsyncUpdate(bool is_async = true);
	private:
		// 派生クラスの非同期実行関数を呼び出す.
		void AsyncUpdateRelay();

	protected:
		TUniquePtr<FAsyncTask<FAsyncFunc>>	async_task_;
	};



	// AsyncTaskの実装サンプル.
	class FAsycTaskImplTest : protected FAsyncTaskBase
	{
	public:
		FAsycTaskImplTest()
		{
		}
		~FAsycTaskImplTest()
		{
			Finalize();
		}

		bool Initialize()
		{
			// Async完了待ち. 念の為.
			WaitAsyncUpdate();

			// TODO
			return true;
		}
		void Finalize()
		{
			// Async完了待ち
			WaitAsyncUpdate();

			// TODO
		}

		// 更新処理とAsyncの管理.
		void SyncUpdate(float delta_sec)
		{
			// Asyncを待機
			WaitAsyncUpdate();

			// フリップ
			flip_index_ = 1 - flip_index_;

			// TODO.


			// ここからAsync起動
			StartAsyncUpdate();
		}
	private:
		// 非同期実行関数
		void AsyncUpdate() override
		{
			const auto async_index = (1 - flip_index_);


			// 処理負荷テスト
			std::this_thread::sleep_for(std::chrono::milliseconds(10));

			// TODO.

		}

	private:
		int	flip_index_ = 0;
	};
}
	