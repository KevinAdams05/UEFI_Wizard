/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_WORKER_THREAD_H
#define UEFI_WIZARD_WORKER_THREAD_H


#include <DiskDeviceDefs.h>
#include <Messenger.h>
#include <OS.h>
#include <String.h>
#include <SupportDefs.h>


// Plan describing a single end-to-end ESP setup operation. The wizard
// builds one of these on the Confirm page based on the StatusPage
// inspection result, then hands it to the worker thread.
struct WorkPlan {
	BString			devicePath;			// e.g. /dev/disk/scsi/0/0/0/raw
	bool			createPartition;	// false = use existingESPID
	off_t			espSizeBytes;		// only used when createPartition
	partition_id	existingESPID;		// only used when !createPartition
};


// Runs an ESPManager + LoaderInstaller pipeline on a background thread
// and reports progress / completion via BMessages to the given target.
//
// Messages posted (with 'what' codes from Constants.h):
//   kMsgWorkerLog       — string "level" ("step"/"detail"/"warn"/"error")
//                         and string "message"
//   kMsgWorkerDone      — string "destination_path", int32 "esp_id"
//   kMsgWorkerFailed    — string "message"
class WorkerThread {
public:
								WorkerThread(const WorkPlan& plan,
									const BMessenger& target);
								~WorkerThread();

			status_t			Start();
			void				WaitForCompletion();

private:
	static int32				_ThreadEntry(void* data);
			int32				_Run();

			WorkPlan			fPlan;
			BMessenger			fTarget;
			thread_id			fThread;
};


#endif	// UEFI_WIZARD_WORKER_THREAD_H
