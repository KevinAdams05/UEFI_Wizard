/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 *
 * The validated API sequence behind this implementation lives in
 * docs/phase2-findings.md §2. In short:
 *
 *   1. GetPartitioningInfo only works inside a PrepareModifications session.
 *   2. CreateChild and Initialize must commit separately — the kernel
 *      auto-detects stale FS signatures from previously-deleted partitions
 *      and the InitializeJob then refuses with B_BAD_VALUE.
 *   3. FAT init parameters: "fat 0;\nname \"<volname>\";\n".
 *   4. BPartition::DeleteChild takes Index(), NOT a partition_id.
 *   5. Real disks may report parent->ContentType() == "Intel Partition
 *      Map" and still have a working ESP — classify by the partition
 *      itself, not the parent disk-system name.
 */


#include "ESPManager.h"

#include <stdio.h>
#include <string.h>

#include <DiskDeviceRoster.h>
#include <DiskDeviceTypes.h>
#include <PartitioningInfo.h>

#include <vector>

#include "Constants.h"
#include "IProgressLogger.h"


// "EFI system data" is the human-readable type string that both the GPT
// add-on (kTypeMap) and the MBR add-on (PartitionMap.cpp:141, type byte
// 0xEF) accept on CreateChild and translate into the appropriate on-disk
// type identifier.
static const char* kEFIPartitionType = "EFI system data";


// ---------------------------------------------------------------------------
// Internal helper: RAII wrapper around BDiskDevice::PrepareModifications
// and Cancel/Commit. Mirrors DriveSetup's ModificationPreparer
// (src/apps/drivesetup/MainWindow.cpp:182).
// ---------------------------------------------------------------------------
namespace {

class ModificationSession {
public:
								ModificationSession(BDiskDevice* disk)
									:
									fDisk(disk),
									fStatus(disk->PrepareModifications())
								{
								}

								~ModificationSession()
								{
									if (fStatus == B_OK)
										fDisk->CancelModifications();
								}

			status_t			InitCheck() const { return fStatus; }

			status_t			Commit()
								{
									status_t status
										= fDisk->CommitModifications();
									if (status == B_OK)
										fStatus = B_ERROR;
									return status;
								}

private:
			BDiskDevice*		fDisk;
			status_t			fStatus;
};


// Convenience: log a Step using printf-style formatting through BString.
void
LogStepF(IProgressLogger* logger, const char* fmt, ...)
{
	if (logger == NULL)
		return;
	BString message;
	va_list args;
	va_start(args, fmt);
	message.SetToFormatVarArgs(fmt, args);
	va_end(args);
	logger->Step(message.String());
}


void
LogDetailF(IProgressLogger* logger, const char* fmt, ...)
{
	if (logger == NULL)
		return;
	BString message;
	va_list args;
	va_start(args, fmt);
	message.SetToFormatVarArgs(fmt, args);
	va_end(args);
	logger->Detail(message.String());
}


void
LogErrorF(IProgressLogger* logger, const char* fmt, ...)
{
	if (logger == NULL)
		return;
	BString message;
	va_list args;
	va_start(args, fmt);
	message.SetToFormatVarArgs(fmt, args);
	va_end(args);
	logger->Error(message.String());
}


// Walk the children of the given disk and locate a partition that already
// looks like a valid ESP (type "EFI system data", FAT content, ≥1 MiB,
// not read-only). Returns the partition_id or -1 if none.
partition_id
FindExistingESP(BDiskDevice* disk, off_t& outSize)
{
	for (int32 i = 0; i < disk->CountChildren(); i++) {
		BPartition* child = disk->ChildAt(i);
		const char* type = child->Type();
		const char* contentType = child->ContentType();

		if (type == NULL || contentType == NULL)
			continue;
		if (strcmp(type, kEFIPartitionType) != 0)
			continue;
		// Accept any FAT variant. UEFI firmware reads FAT12/16/32 alike.
		if (strcmp(contentType, kPartitionTypeFAT32) != 0
			&& strcmp(contentType, kPartitionTypeFAT16) != 0
			&& strcmp(contentType, kPartitionTypeFAT12) != 0) {
			continue;
		}
		if (child->IsReadOnly())
			continue;
		if (child->ContentSize() < 1024 * 1024)
			continue;

		outSize = child->ContentSize();
		return child->ID();
	}

	outSize = 0;
	return -1;
}


// Find the largest partitionable space on the disk.
status_t
FindLargestFreeSpace(BDiskDevice* disk, off_t& outOffset, off_t& outSize)
{
	BPartitioningInfo info;
	status_t status = disk->GetPartitioningInfo(&info);
	if (status != B_OK)
		return status;

	off_t bestOffset = 0;
	off_t bestSize = 0;
	for (int32 i = 0; i < info.CountPartitionableSpaces(); i++) {
		off_t offset = 0;
		off_t size = 0;
		if (info.GetPartitionableSpaceAt(i, &offset, &size) != B_OK)
			continue;
		if (size > bestSize) {
			bestSize = size;
			bestOffset = offset;
		}
	}

	outOffset = bestOffset;
	outSize = bestSize;
	return B_OK;
}


}	// unnamed namespace


// ---------------------------------------------------------------------------
// ESPManager
// ---------------------------------------------------------------------------
ESPManager::ESPManager()
{
}


ESPManager::~ESPManager()
{
}


status_t
ESPManager::Inspect(const char* devicePath, off_t requestedESPBytes,
	ESPInspection& outResult, IProgressLogger* logger)
{
	outResult.state = ESP_STATE_INSPECTION_FAILED;
	outResult.espPartitionID = -1;
	outResult.espSizeBytes = 0;
	outResult.freeContiguousBytes = 0;
	outResult.freeContiguousOffset = 0;
	outResult.partitioningSystem = "";
	outResult.diagnostic = "";

	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* matched = NULL;

	if (roster.GetPartitionForPath(devicePath, &device, &matched) != B_OK
		|| matched == NULL) {
		outResult.diagnostic.SetToFormat("No disk matched path '%s'.",
			devicePath);
		LogErrorF(logger, "%s", outResult.diagnostic.String());
		return B_ENTRY_NOT_FOUND;
	}

	const char* contentType = device.ContentType();
	if (contentType != NULL)
		outResult.partitioningSystem = contentType;

	if (contentType == NULL) {
		outResult.state = ESP_STATE_NO_PARTITIONING_SYSTEM;
		outResult.diagnostic
			= "Disk has no partitioning system (raw / unformatted).";
		return B_OK;
	}

	bool isGPT = strcmp(contentType, kPartitionTypeEFI) == 0;
	bool isMBR = strcmp(contentType, kPartitionTypeIntel) == 0;
	if (!isGPT && !isMBR) {
		outResult.state = ESP_STATE_UNSUPPORTED_PARTITIONING;
		outResult.diagnostic.SetToFormat(
			"Unsupported partitioning system: %s.", contentType);
		return B_OK;
	}

	off_t existingSize = 0;
	partition_id existing = FindExistingESP(&device, existingSize);
	if (existing >= 0) {
		outResult.state = ESP_STATE_VALID;
		outResult.espPartitionID = existing;
		outResult.espSizeBytes = existingSize;
		outResult.diagnostic = "Existing EFI System Partition found.";
		return B_OK;
	}

	// No ESP yet — measure free space. GetPartitioningInfo requires an
	// active PrepareModifications session.
	ModificationSession session(&device);
	if (session.InitCheck() != B_OK) {
		outResult.diagnostic.SetToFormat(
			"PrepareModifications failed: %s", strerror(session.InitCheck()));
		LogErrorF(logger, "%s", outResult.diagnostic.String());
		return session.InitCheck();
	}

	off_t freeOffset = 0;
	off_t freeSize = 0;
	status_t status = FindLargestFreeSpace(&device, freeOffset, freeSize);
	if (status != B_OK) {
		outResult.diagnostic.SetToFormat(
			"GetPartitioningInfo failed: %s", strerror(status));
		LogErrorF(logger, "%s", outResult.diagnostic.String());
		return status;
	}

	outResult.freeContiguousBytes = freeSize;
	outResult.freeContiguousOffset = freeOffset;

	if (freeSize >= requestedESPBytes) {
		outResult.state = ESP_STATE_NONE_BUT_FREE_SPACE;
		outResult.diagnostic.SetToFormat(
			"No ESP found. %lld bytes contiguous free at offset %lld — "
			"can create a %lld-byte ESP here.",
			(long long)freeSize, (long long)freeOffset,
			(long long)requestedESPBytes);
	} else {
		outResult.state = ESP_STATE_NONE_NO_FREE_SPACE;
		outResult.diagnostic.SetToFormat(
			"No ESP found. Largest free space (%lld bytes) is smaller "
			"than the requested ESP size (%lld bytes). Use DriveSetup to "
			"free at least that much contiguous space first.",
			(long long)freeSize, (long long)requestedESPBytes);
	}

	return B_OK;
}


status_t
ESPManager::CreateESP(const char* devicePath, off_t sizeBytes,
	partition_id& outPartitionID, IProgressLogger* logger)
{
	outPartitionID = -1;

	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* matched = NULL;

	if (roster.GetPartitionForPath(devicePath, &device, &matched) != B_OK) {
		LogErrorF(logger, "No disk matched path '%s'.", devicePath);
		return B_ENTRY_NOT_FOUND;
	}

	if (device.IsReadOnly()) {
		LogErrorF(logger, "Disk is read-only.");
		return B_READ_ONLY_DEVICE;
	}

	// Snapshot the set of existing child partition IDs so we can spot the
	// new one after the create-commit. Matching by offset is unreliable —
	// CreateChildJob may align further than ValidateCreateChild.
	std::vector<partition_id> existingIDs;
	existingIDs.reserve(device.CountChildren());
	for (int32 i = 0; i < device.CountChildren(); i++)
		existingIDs.push_back(device.ChildAt(i)->ID());

	// ----- Phase A: create the partition -----
	off_t offset = 0;
	off_t size = sizeBytes;
	BString partitionName;
	BString createParams;
	partition_id newID = -1;

	{
		ModificationSession session(&device);
		if (session.InitCheck() != B_OK) {
			LogErrorF(logger, "PrepareModifications failed: %s",
				strerror(session.InitCheck()));
			return session.InitCheck();
		}

		off_t freeSize = 0;
		status_t status = FindLargestFreeSpace(&device, offset, freeSize);
		if (status != B_OK) {
			LogErrorF(logger, "GetPartitioningInfo failed: %s",
				strerror(status));
			return status;
		}

		if (freeSize < sizeBytes) {
			LogErrorF(logger,
				"Not enough free space: largest=%lld B, need=%lld B.",
				(long long)freeSize, (long long)sizeBytes);
			return B_DEVICE_FULL;
		}

		LogStepF(logger, "Allocating partition (%lld bytes at offset %lld)",
			(long long)size, (long long)offset);

		status = device.ValidateCreateChild(&offset, &size,
			kEFIPartitionType, &partitionName, createParams.String());
		if (status != B_OK) {
			LogErrorF(logger, "ValidateCreateChild failed: %s",
				strerror(status));
			return status;
		}

		BPartition* created = NULL;
		status = device.CreateChild(offset, size, kEFIPartitionType,
			partitionName.String(), createParams.String(), &created);
		if (status != B_OK) {
			LogErrorF(logger, "CreateChild failed: %s", strerror(status));
			return status;
		}

		LogDetailF(logger, "Committing partition table change");
		status = session.Commit();
		if (status != B_OK) {
			LogErrorF(logger, "Commit (create) failed: %s",
				strerror(status));
			return status;
		}
	}

	// ----- Locate the partition we just created -----
	BDiskDevice freshDevice;
	BPartition* freshPart = NULL;
	if (roster.GetPartitionForPath(devicePath, &freshDevice, &freshPart)
			!= B_OK) {
		LogErrorF(logger, "Could not re-acquire disk after create-commit.");
		return B_ERROR;
	}

	for (int32 i = 0; i < freshDevice.CountChildren(); i++) {
		BPartition* child = freshDevice.ChildAt(i);
		if (child->Type() == NULL
			|| strcmp(child->Type(), kEFIPartitionType) != 0) {
			continue;
		}

		bool seenBefore = false;
		for (size_t j = 0; j < existingIDs.size(); j++) {
			if (existingIDs[j] == child->ID()) {
				seenBefore = true;
				break;
			}
		}
		if (!seenBefore) {
			newID = child->ID();
			break;
		}
	}

	if (newID < 0) {
		LogErrorF(logger,
			"Could not locate newly-created ESP after create-commit.");
		return B_ERROR;
	}

	LogDetailF(logger, "New partition id=%" B_PRId32, newID);

	// ----- Phase B: uninitialize if the kernel auto-detected a stale FS -----
	{
		BDiskDevice probeDevice;
		BPartition* probePart = NULL;
		if (roster.GetPartitionWithID(newID, &probeDevice, &probePart)
				!= B_OK) {
			LogErrorF(logger, "GetPartitionWithID(new) failed.");
			return B_ERROR;
		}

		const char* ct = probePart->ContentType();
		if (ct != NULL && *ct != '\0') {
			LogStepF(logger,
				"Clearing residual filesystem signature (%s)", ct);

			ModificationSession session(&probeDevice);
			if (session.InitCheck() != B_OK) {
				LogErrorF(logger,
					"PrepareModifications (uninit) failed: %s",
					strerror(session.InitCheck()));
				return session.InitCheck();
			}

			status_t status = probePart->Uninitialize();
			if (status != B_OK) {
				LogErrorF(logger, "Uninitialize failed: %s",
					strerror(status));
				return status;
			}

			status = session.Commit();
			if (status != B_OK) {
				LogErrorF(logger, "Commit (uninit) failed: %s",
					strerror(status));
				return status;
			}
		}
	}

	// ----- Phase C: format as FAT -----
	{
		BDiskDevice initDevice;
		BPartition* toInit = NULL;
		if (roster.GetPartitionWithID(newID, &initDevice, &toInit) != B_OK) {
			LogErrorF(logger, "GetPartitionWithID(init) failed.");
			return B_ERROR;
		}

		ModificationSession session(&initDevice);
		if (session.InitCheck() != B_OK) {
			LogErrorF(logger, "PrepareModifications (init) failed: %s",
				strerror(session.InitCheck()));
			return session.InitCheck();
		}

		BString fsName(kPartitionTypeFAT32);
		BString volumeName = "ESP";
		// FAT init parameter grammar from
		// src/add-ons/disk_systems/fat/InitializeParameterEditor.cpp.
		// "fat 0" lets mkdosfs auto-pick FAT12/16/32 by volume size; this
		// avoids "fat 32" failures on partitions below FAT32's minimum
		// cluster count (~520 MB). UEFI accepts any FAT variant for the
		// ESP.
		BString fsParams;
		fsParams << "fat 0;\nname \"" << volumeName << "\";\n";

		status_t status = toInit->ValidateInitialize(fsName.String(),
			&volumeName, fsParams.String());
		if (status != B_OK) {
			LogErrorF(logger, "ValidateInitialize failed: %s",
				strerror(status));
			return status;
		}

		LogStepF(logger, "Formatting as FAT");

		status = toInit->Initialize(fsName.String(), volumeName.String(),
			fsParams.String());
		if (status != B_OK) {
			LogErrorF(logger, "Initialize failed: %s", strerror(status));
			return status;
		}

		LogDetailF(logger, "Committing format");
		status = session.Commit();
		if (status != B_OK) {
			LogErrorF(logger, "Commit (init) failed: %s", strerror(status));
			return status;
		}
	}

	outPartitionID = newID;
	return B_OK;
}


status_t
ESPManager::Mount(partition_id partitionID, BPath& outMountPath,
	IProgressLogger* logger)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition = NULL;

	status_t status = roster.GetPartitionWithID(partitionID, &device,
		&partition);
	if (status != B_OK) {
		LogErrorF(logger,
			"No partition with id %" B_PRId32 ".", partitionID);
		return status;
	}

	if (!partition->IsMounted()) {
		LogStepF(logger, "Mounting partition");
		status = partition->Mount();
		if (status < B_OK) {
			LogErrorF(logger, "Mount failed: %s", strerror(status));
			return status;
		}
	}

	status = partition->GetMountPoint(&outMountPath);
	if (status != B_OK) {
		LogErrorF(logger, "GetMountPoint failed: %s", strerror(status));
		return status;
	}

	LogDetailF(logger, "Mounted at %s", outMountPath.Path());
	return B_OK;
}


status_t
ESPManager::Unmount(partition_id partitionID, IProgressLogger* logger)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition = NULL;

	if (roster.GetPartitionWithID(partitionID, &device, &partition) != B_OK)
		return B_ENTRY_NOT_FOUND;

	if (!partition->IsMounted())
		return B_OK;

	status_t status = partition->Unmount();
	if (status != B_OK) {
		LogErrorF(logger, "Unmount failed: %s", strerror(status));
		return status;
	}
	return B_OK;
}


status_t
ESPManager::DeleteByID(partition_id partitionID, IProgressLogger* logger)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* target = NULL;

	if (roster.GetPartitionWithID(partitionID, &device, &target) != B_OK
		|| target == NULL) {
		LogErrorF(logger, "No partition with id %" B_PRId32 ".",
			partitionID);
		return B_ENTRY_NOT_FOUND;
	}

	if (target->IsMounted())
		target->Unmount();

	BPartition* parent = target->Parent();
	if (parent == NULL) {
		LogErrorF(logger,
			"Partition has no parent — refusing to delete a whole disk.");
		return B_BAD_VALUE;
	}

	// API trap from phase 2: DeleteChild takes Index(), not partition_id.
	// Don't gate on CanDeleteChild — that requires the disk system handle
	// to be in delegate state, which only happens inside a
	// PrepareModifications session. DeleteChild itself rejects the
	// operation if the underlying disk system can't support it.
	int32 index = target->Index();

	ModificationSession session(&device);
	if (session.InitCheck() != B_OK) {
		LogErrorF(logger, "PrepareModifications failed: %s",
			strerror(session.InitCheck()));
		return session.InitCheck();
	}

	status_t status = parent->DeleteChild(index);
	if (status != B_OK) {
		LogErrorF(logger, "DeleteChild failed: %s", strerror(status));
		return status;
	}

	status = session.Commit();
	if (status != B_OK) {
		LogErrorF(logger, "Commit (delete) failed: %s", strerror(status));
		return status;
	}

	LogStepF(logger, "Deleted partition id=%" B_PRId32, partitionID);
	return B_OK;
}
