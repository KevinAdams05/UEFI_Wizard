/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_CONSTANTS_H
#define UEFI_WIZARD_CONSTANTS_H


#include <SupportDefs.h>


extern const char* kAppSignature;
extern const char* kAppName;

extern const char* kEFISystemPartitionGUID;
	// "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"

extern const off_t kDefaultESPSize;
	// 256 MiB

// BMessage 'what' codes used between the wizard pages and the worker thread.
enum {
	kMsgPageNext			= 'pgnx',
	kMsgPageBack			= 'pgbk',
	kMsgDiskSelected		= 'dsel',
	kMsgRefreshDisks		= 'drsh',

	kMsgApply				= 'aply',
	kMsgCancel				= 'cncl',

	kMsgWorkerProgress		= 'wkpr',
	kMsgWorkerLog			= 'wklg',
	kMsgWorkerDone			= 'wkdn',
	kMsgWorkerFailed		= 'wkfl',

	kMsgRebootNow			= 'rbnw',
};


#endif	// UEFI_WIZARD_CONSTANTS_H
