/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */


#include "WorkerThread.h"

#include <Message.h>
#include <Path.h>

#include "ArchUtils.h"
#include "Constants.h"
#include "ESPManager.h"
#include "IProgressLogger.h"
#include "LoaderInstaller.h"


// Forwards each progress event to the wizard via a BMessenger. Safe to
// call from a non-window thread because BMessenger::SendMessage uses the
// kernel port mechanism.
class MessengerLogger : public IProgressLogger {
public:
								MessengerLogger(const BMessenger& target)
									:
									fTarget(target)
								{
								}

	virtual void				Step(const char* message)
								{
									_Send("step", message);
								}
	virtual void				Detail(const char* message)
								{
									_Send("detail", message);
								}
	virtual void				Warn(const char* message)
								{
									_Send("warn", message);
								}
	virtual void				Error(const char* message)
								{
									_Send("error", message);
								}

private:
			void				_Send(const char* level,
									const char* message)
								{
									BMessage out(kMsgWorkerLog);
									out.AddString("level", level);
									out.AddString("message", message);
									fTarget.SendMessage(&out);
								}

			BMessenger			fTarget;
};


WorkerThread::WorkerThread(const WorkPlan& plan, const BMessenger& target)
	:
	fPlan(plan),
	fTarget(target),
	fThread(-1)
{
}


WorkerThread::~WorkerThread()
{
	WaitForCompletion();
}


status_t
WorkerThread::Start()
{
	if (fThread >= 0)
		return B_BUSY;

	fThread = spawn_thread(_ThreadEntry, "uefi_setup_worker",
		B_NORMAL_PRIORITY, this);
	if (fThread < 0)
		return fThread;

	return resume_thread(fThread);
}


void
WorkerThread::WaitForCompletion()
{
	if (fThread < 0)
		return;
	status_t exitCode = 0;
	wait_for_thread(fThread, &exitCode);
	fThread = -1;
}


int32
WorkerThread::_ThreadEntry(void* data)
{
	return static_cast<WorkerThread*>(data)->_Run();
}


int32
WorkerThread::_Run()
{
	MessengerLogger logger(fTarget);

	ESPManager manager;
	partition_id espID = -1;
	status_t status = B_OK;

	if (fPlan.createPartition) {
		status = manager.CreateESP(fPlan.devicePath.String(),
			fPlan.espSizeBytes, espID, &logger);
		if (status != B_OK) {
			BMessage failure(kMsgWorkerFailed);
			BString message;
			message.SetToFormat("Failed to create ESP: %s",
				strerror(status));
			failure.AddString("message", message.String());
			fTarget.SendMessage(&failure);
			return status;
		}
	} else {
		espID = fPlan.existingESPID;
		logger.Step("Using existing EFI System Partition");
	}

	BPath mountPath;
	status = manager.Mount(espID, mountPath, &logger);
	if (status != B_OK) {
		BMessage failure(kMsgWorkerFailed);
		BString message;
		message.SetToFormat("Mount failed: %s", strerror(status));
		failure.AddString("message", message.String());
		fTarget.SendMessage(&failure);
		return status;
	}

	BString loaderSourcePath;
	if (ArchUtils::FindLoaderSourcePath(loaderSourcePath) != B_OK) {
		BMessage failure(kMsgWorkerFailed);
		failure.AddString("message",
			"Could not resolve haiku_loader.efi source path.");
		fTarget.SendMessage(&failure);
		return B_ERROR;
	}

	LoaderInstaller installer;
	BPath dest;
	BPath source(loaderSourcePath.String());
	status = installer.Install(mountPath, source, dest, &logger);
	if (status != B_OK) {
		BMessage failure(kMsgWorkerFailed);
		BString message;
		message.SetToFormat("Loader install failed: %s", strerror(status));
		failure.AddString("message", message.String());
		fTarget.SendMessage(&failure);
		return status;
	}

	BMessage done(kMsgWorkerDone);
	done.AddString("destination_path", dest.Path());
	done.AddInt32("esp_id", espID);
	fTarget.SendMessage(&done);

	return B_OK;
}
