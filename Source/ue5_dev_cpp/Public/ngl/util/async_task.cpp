
#include "async_task.h"

namespace ngl
{
	FNglAsyncTask::FNglAsyncTask()
	{
		// タスクセットアップ
		async_task_.Reset(new FAsyncTask<FNglAsyncFunc>([this]() { this->AsyncUpdateRelay(); }));
	}
	FNglAsyncTask::~FNglAsyncTask()
	{
		if (async_task_.IsValid())
			async_task_->EnsureCompletion();
		async_task_.Reset();
	}

	// Asyncタスクが完了しているか.
	bool FNglAsyncTask::IsDone() const
	{
		if (async_task_.IsValid())
		{
			return async_task_->IsDone();
		}
		return true;
	}

	// Asyncタスクの完了を待機.
	void FNglAsyncTask::WaitAsyncUpdate()
	{
		if (async_task_.IsValid())
		{
			async_task_->EnsureCompletion();
		}
	}

	// Asyncタスクを起動.
	void FNglAsyncTask::StartAsyncUpdate(bool is_async)
	{
		if (!IsDone())
			WaitAsyncUpdate();

		if (is_async)
			async_task_->StartBackgroundTask();
		else
			async_task_->StartSynchronousTask();
	}

	// 派生クラスの非同期実行関数を呼び出す.
	void FNglAsyncTask::AsyncUpdateRelay()
	{
		AsyncUpdate();
	}
}
	