
#include "async_task.h"

namespace naga
{
	FAsyncTaskBase::FAsyncTaskBase()
	{
		// タスクセットアップ
		async_task_.Reset(new FAsyncTask<FAsyncFunc>([this]() { this->AsyncUpdateRelay(); }));
	}
	FAsyncTaskBase::~FAsyncTaskBase()
	{
		if (async_task_.IsValid())
			async_task_->EnsureCompletion();
		async_task_.Reset();
	}

	// Asyncタスクが完了しているか.
	bool FAsyncTaskBase::IsDone() const
	{
		if (async_task_.IsValid())
		{
			return async_task_->IsDone();
		}
		return true;
	}

	// Asyncタスクの完了を待機.
	void FAsyncTaskBase::WaitAsyncUpdate()
	{
		if (async_task_.IsValid())
		{
			async_task_->EnsureCompletion();
		}
	}

	// Asyncタスクを起動.
	void FAsyncTaskBase::StartAsyncUpdate(bool is_async)
	{
		if (!IsDone())
			WaitAsyncUpdate();

		if (is_async)
			async_task_->StartBackgroundTask();
		else
			async_task_->StartSynchronousTask();
	}

	// 派生クラスの非同期実行関数を呼び出す.
	void FAsyncTaskBase::AsyncUpdateRelay()
	{
		AsyncUpdate();
	}
}
	