/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_ESP_MANAGER_H
#define UEFI_WIZARD_ESP_MANAGER_H


#include <DiskDevice.h>
#include <Partition.h>
#include <Path.h>
#include <String.h>
#include <SupportDefs.h>


class IProgressLogger;


// Result of inspecting one disk.
enum esp_state {
	ESP_STATE_VALID,
		// A FAT partition tagged with the EFI System type GUID exists on
		// the disk and is at least 1 MiB. Loader copy is the next step.
	ESP_STATE_NONE_BUT_FREE_SPACE,
		// No ESP found, but there is at least the requested ESP size of
		// unallocated space. Offer to create one.
	ESP_STATE_NONE_NO_FREE_SPACE,
		// No ESP and not enough unallocated space — refuse, defer to
		// DriveSetup.
	ESP_STATE_NO_PARTITIONING_SYSTEM,
		// Disk has no recognized partitioning system (raw / unformatted).
	ESP_STATE_UNSUPPORTED_PARTITIONING,
		// Disk uses a partitioning system other than GPT or MBR.
	ESP_STATE_INSPECTION_FAILED,
		// Roster lookup or PrepareModifications failed.
};


struct ESPInspection {
	esp_state			state;
	// -1 unless state == ESP_STATE_VALID
	partition_id		espPartitionID;
	off_t				espSizeBytes;
	off_t				freeContiguousBytes;
	off_t				freeContiguousOffset;
	// "GUID Partition Map" or "Intel Partition Map"
	BString				partitioningSystem;
	BString				diagnostic;			// human-readable explanation
};


class ESPManager {
public:
								ESPManager();
								~ESPManager();

	// Read-only inspection. The disk is identified by its raw device path
	// (e.g. "/dev/disk/scsi/0/0/0/raw"). Wraps a PrepareModifications
	// session internally — calling code does not need to.
	//
	// Returns B_OK with outResult.state set to one of ESP_STATE_*. A
	// status_t other than B_OK indicates the inspection itself could not
	// run (no logger callbacks are issued in that case).
			status_t			Inspect(const char* devicePath,
									off_t requestedESPBytes,
									ESPInspection& outResult,
									IProgressLogger* logger);

	// Create a fresh ESP on the given disk: allocates a child partition
	// of the requested size out of the largest unallocated space, runs
	// the create-commit / uninitialize-if-needed / format-commit dance
	// from docs/phase2-findings.md, and returns the new partition's id.
	//
	// Pre-condition: Inspect on this disk must have returned
	// ESP_STATE_NONE_BUT_FREE_SPACE.
			status_t			CreateESP(const char* devicePath,
									off_t sizeBytes,
									partition_id& outPartitionID,
									IProgressLogger* logger);

	// Mount the partition with the given id read/write. On success outPath
	// is filled with the mount point chosen by the kernel.
			status_t			Mount(partition_id partitionID,
									BPath& outMountPath,
									IProgressLogger* logger);

	// Unmount a previously-mounted partition. Best-effort.
			status_t			Unmount(partition_id partitionID,
									IProgressLogger* logger);

	// Diagnostic helper used by the CLI's "delete" command (and by tests).
	// Wraps the Index()/DeleteChild API trap that bit the spike.
			status_t			DeleteByID(partition_id partitionID,
									IProgressLogger* logger);

private:
								ESPManager(const ESPManager&);
			ESPManager&			operator=(const ESPManager&);
};


#endif	// UEFI_WIZARD_ESP_MANAGER_H
