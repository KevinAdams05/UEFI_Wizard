/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 *
 * uefi_wizard — scriptable backend for UEFI Wizard. Same
 * ESPManager + LoaderInstaller pipeline the GUI uses, surfaced as a
 * shell-friendly command. Useful for headless repair from a live USB
 * session and for regression testing.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <Path.h>
#include <String.h>

#include "ArchUtils.h"
#include "Constants.h"
#include "ESPManager.h"
#include "IProgressLogger.h"
#include "LoaderInstaller.h"


// Console logger: prints each progress event as a single line on stdout
// (steps and details) or stderr (warnings and errors).
class ConsoleLogger : public IProgressLogger {
public:
	virtual void				Step(const char* message)
								{
									printf("==> %s\n", message);
									fflush(stdout);
								}
	virtual void				Detail(const char* message)
								{
									printf("    %s\n", message);
									fflush(stdout);
								}
	virtual void				Warn(const char* message)
								{
									fprintf(stderr, "warning: %s\n",
										message);
								}
	virtual void				Error(const char* message)
								{
									fprintf(stderr, "error: %s\n", message);
								}
};


static const char* kStateNames[] = {
	"valid",
	"none-but-free-space",
	"none-no-free-space",
	"no-partitioning-system",
	"unsupported-partitioning",
	"inspection-failed",
};


static void
print_size(off_t bytes)
{
	const off_t kKiB = 1024;
	const off_t kMiB = 1024 * kKiB;
	const off_t kGiB = 1024 * kMiB;
	if (bytes >= kGiB)
		printf("%.2f GiB", (double)bytes / (double)kGiB);
	else if (bytes >= kMiB)
		printf("%.1f MiB", (double)bytes / (double)kMiB);
	else if (bytes >= kKiB)
		printf("%.0f KiB", (double)bytes / (double)kKiB);
	else
		printf("%lld B", (long long)bytes);
}


static void
usage(const char* progName)
{
	fprintf(stderr,
		"Usage: %s <command> [options]\n"
		"\n"
		"Commands:\n"
		"  list\n"
		"      Enumerate every disk and report its ESP state.\n"
		"\n"
		"  inspect <disk-path>\n"
		"      Detailed report for one disk.\n"
		"\n"
		"  auto <disk-path> [--size MB]\n"
		"      The recommended path: detect existing ESP or create one,\n"
		"      then copy haiku_loader.efi into place. Refuses with a clear\n"
		"      message when there's no safe option.\n"
		"\n"
		"  create-esp <disk-path> [--size MB]\n"
		"      DESTRUCTIVE. Allocate a FAT ESP out of the largest free\n"
		"      space on the target. Default size is 256 MB.\n"
		"\n"
		"  copy-loader <esp-partition-id>\n"
		"      Copy haiku_loader.efi to /EFI/BOOT/ on the given partition.\n"
		"\n"
		"  delete <partition-id>\n"
		"      DESTRUCTIVE. Remove a child partition.\n",
		progName);
}


static int
cmd_list(ESPManager& manager)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	int32 count = 0;
	NullProgressLogger nullLogger;

	while (roster.GetNextDevice(&device) == B_OK) {
		BPath path;
		device.GetPath(&path);

		printf("Disk: %s\n", path.Path());
		printf("  size=");
		print_size(device.Size());
		printf(", removable=%d, readOnly=%d, contentType=\"%s\"\n",
			device.IsRemovableMedia() ? 1 : 0,
			device.IsReadOnly() ? 1 : 0,
			device.ContentType() ? device.ContentType() : "(none)");

		ESPInspection result;
		manager.Inspect(path.Path(), kDefaultESPSize, result, &nullLogger);

		const char* stateName = "unknown";
		if (result.state >= 0
			&& (size_t)result.state
				< sizeof(kStateNames) / sizeof(kStateNames[0])) {
			stateName = kStateNames[result.state];
		}
		printf("  state=%s", stateName);
		if (result.state == ESP_STATE_VALID) {
			printf(" (id=%" B_PRId32 ", size=", result.espPartitionID);
			print_size(result.espSizeBytes);
			printf(")");
		} else if (result.state == ESP_STATE_NONE_BUT_FREE_SPACE
			|| result.state == ESP_STATE_NONE_NO_FREE_SPACE) {
			printf(" (largest free=");
			print_size(result.freeContiguousBytes);
			printf(")");
		}
		printf("\n");
		count++;
	}

	if (count == 0) {
		fprintf(stderr, "No disks reported by BDiskDeviceRoster.\n");
		return 1;
	}
	return 0;
}


static int
cmd_inspect(ESPManager& manager, const char* devicePath)
{
	ConsoleLogger logger;
	ESPInspection result;
	status_t status = manager.Inspect(devicePath, kDefaultESPSize, result,
		&logger);
	if (status != B_OK)
		return 2;

	printf("disk:                %s\n", devicePath);
	printf("partitioning:        %s\n",
		result.partitioningSystem.IsEmpty() ? "(none)"
			: result.partitioningSystem.String());

	const char* stateName = "unknown";
	if (result.state >= 0
		&& (size_t)result.state
			< sizeof(kStateNames) / sizeof(kStateNames[0])) {
		stateName = kStateNames[result.state];
	}
	printf("state:               %s\n", stateName);

	if (result.espPartitionID >= 0) {
		printf("esp partition id:    %" B_PRId32 "\n",
			result.espPartitionID);
		printf("esp size:            ");
		print_size(result.espSizeBytes);
		printf("\n");
	}
	if (result.freeContiguousBytes > 0) {
		printf("largest free space:  ");
		print_size(result.freeContiguousBytes);
		printf(" at offset ");
		print_size(result.freeContiguousOffset);
		printf("\n");
	}
	if (!result.diagnostic.IsEmpty())
		printf("diagnostic:          %s\n", result.diagnostic.String());

	return 0;
}


static int
cmd_create_esp(ESPManager& manager, const char* devicePath, off_t bytes)
{
	ConsoleLogger logger;
	partition_id newID = -1;
	status_t status = manager.CreateESP(devicePath, bytes, newID, &logger);
	if (status != B_OK)
		return 2;
	printf("Created ESP partition id=%" B_PRId32 ".\n", newID);
	return 0;
}


static int
cmd_copy_loader(ESPManager& manager, partition_id espID)
{
	ConsoleLogger logger;

	BString sourcePath;
	if (ArchUtils::FindLoaderSourcePath(sourcePath) != B_OK) {
		fprintf(stderr,
			"Could not resolve haiku_loader.efi source path.\n");
		return 2;
	}

	BPath mountPath;
	status_t status = manager.Mount(espID, mountPath, &logger);
	if (status != B_OK)
		return 2;

	LoaderInstaller installer;
	BPath dest;
	BPath source(sourcePath.String());
	status = installer.Install(mountPath, source, dest, &logger);
	if (status != B_OK)
		return 2;

	printf("Loader installed at %s.\n", dest.Path());
	return 0;
}


static int
cmd_auto(ESPManager& manager, const char* devicePath, off_t bytes)
{
	ConsoleLogger logger;
	ESPInspection result;
	status_t status = manager.Inspect(devicePath, bytes, result, &logger);
	if (status != B_OK)
		return 2;

	partition_id espID = -1;

	switch (result.state) {
		case ESP_STATE_VALID:
			espID = result.espPartitionID;
			printf("Found existing ESP id=%" B_PRId32 " (",
				espID);
			print_size(result.espSizeBytes);
			printf(").\n");
			break;

		case ESP_STATE_NONE_BUT_FREE_SPACE:
			printf("No ESP found. Creating one (");
			print_size(bytes);
			printf(") in available free space.\n");
			status = manager.CreateESP(devicePath, bytes, espID, &logger);
			if (status != B_OK)
				return 2;
			break;

		case ESP_STATE_NONE_NO_FREE_SPACE:
		case ESP_STATE_NO_PARTITIONING_SYSTEM:
		case ESP_STATE_UNSUPPORTED_PARTITIONING:
		case ESP_STATE_INSPECTION_FAILED:
		default:
			fprintf(stderr, "Cannot proceed: %s\n",
				result.diagnostic.IsEmpty() ? "(no diagnostic)"
					: result.diagnostic.String());
			return 2;
	}

	BString sourcePath;
	if (ArchUtils::FindLoaderSourcePath(sourcePath) != B_OK) {
		fprintf(stderr,
			"Could not resolve haiku_loader.efi source path.\n");
		return 2;
	}

	BPath mountPath;
	status = manager.Mount(espID, mountPath, &logger);
	if (status != B_OK)
		return 2;

	LoaderInstaller installer;
	BPath dest;
	BPath source(sourcePath.String());
	status = installer.Install(mountPath, source, dest, &logger);
	if (status != B_OK)
		return 2;

	printf("\nDone. UEFI loader installed at %s.\n", dest.Path());
	printf("Reboot and select the disk from your firmware boot menu.\n");
	return 0;
}


static int
cmd_delete(ESPManager& manager, partition_id partitionID)
{
	ConsoleLogger logger;
	status_t status = manager.DeleteByID(partitionID, &logger);
	return status == B_OK ? 0 : 2;
}


int
main(int argc, char* argv[])
{
	BApplication app(kAppSignature);

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	const char* cmd = argv[1];
	ESPManager manager;

	if (strcmp(cmd, "list") == 0)
		return cmd_list(manager);

	if (strcmp(cmd, "inspect") == 0) {
		if (argc < 3) {
			usage(argv[0]);
			return 1;
		}
		return cmd_inspect(manager, argv[2]);
	}

	if (strcmp(cmd, "create-esp") == 0) {
		if (argc < 3) {
			usage(argv[0]);
			return 1;
		}
		off_t bytes = kDefaultESPSize;
		for (int32 i = 3; i < argc - 1; i++) {
			if (strcmp(argv[i], "--size") == 0) {
				int mb = atoi(argv[i + 1]);
				if (mb > 0)
					bytes = (off_t)mb * 1024 * 1024;
			}
		}
		return cmd_create_esp(manager, argv[2], bytes);
	}

	if (strcmp(cmd, "auto") == 0) {
		if (argc < 3) {
			usage(argv[0]);
			return 1;
		}
		off_t bytes = kDefaultESPSize;
		for (int32 i = 3; i < argc - 1; i++) {
			if (strcmp(argv[i], "--size") == 0) {
				int mb = atoi(argv[i + 1]);
				if (mb > 0)
					bytes = (off_t)mb * 1024 * 1024;
			}
		}
		return cmd_auto(manager, argv[2], bytes);
	}

	if (strcmp(cmd, "copy-loader") == 0) {
		if (argc < 3) {
			usage(argv[0]);
			return 1;
		}
		partition_id id = atoi(argv[2]);
		return cmd_copy_loader(manager, id);
	}

	if (strcmp(cmd, "delete") == 0) {
		if (argc < 3) {
			usage(argv[0]);
			return 1;
		}
		partition_id id = atoi(argv[2]);
		return cmd_delete(manager, id);
	}

	usage(argv[0]);
	return 1;
}
