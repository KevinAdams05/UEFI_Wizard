/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 *
 * uefi_spike — Phase 2 verification CLI for the HaikuUEFISetup project.
 *
 * Validates four risks before any production code is written
 * (findings later folded into docs/phase2-findings.md):
 *
 *   1. Does BPartition::CreateChild persist the GPT type GUID at create
 *      time when passed type "EFI system data"?
 *   2. Is a freshly-created ESP writable via BPartition::Mount() from a
 *      live-USB session?
 *   3. Does BPartition::Initialize("FAT32 File System") format FAT32
 *      from app code?
 *   4. Does the disk-system add-on accept the type via the parent's
 *      CreateChild call (vs. needing it in the parameters dict)?
 *
 * Commands (all are best-effort and verbose):
 *
 *   uefi_spike list
 *       Enumerate every disk, printing partitioning, ESP state, and
 *       partitionable spaces.
 *
 *   uefi_spike inspect <disk-path>
 *       Inspect one disk in detail: every partition, type, content
 *       type, mount state.
 *
 *   uefi_spike create-esp <disk-path> [--size MB]
 *       DESTRUCTIVE. Allocate a FAT32 ESP out of the largest free space
 *       on the target disk, format it, mount it, write a test file.
 *       Default size is 256 MB.
 *
 *   uefi_spike copy-loader <esp-partition-id>
 *       Mount the partition with the given id, copy the running system's
 *       haiku_loader.efi to /EFI/BOOT/BOOT<arch>.EFI, and verify.
 *
 *   uefi_spike delete <partition-id>
 *       DESTRUCTIVE. Remove a child partition. For tearing down spike runs.
 *
 * The spike intentionally lives outside src/ so failed experiments don't
 * leak into the production build. Once an end-to-end run succeeds, the
 * proven code paths get folded into ESPManager and LoaderInstaller.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <Directory.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceTypes.h>
#include <DiskSystem.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Partition.h>
#include <PartitioningInfo.h>
#include <Path.h>
#include <String.h>


static const char* kAppSignature = "application/x-vnd.HaikuUEFISetup-Spike";

static const off_t kMiB = 1024 * 1024;
static const off_t kDefaultESPBytes = 256 * kMiB;

static const char* kEFIPartitionType = "EFI system data";
	// Maps to GUID C12A7328-F81F-11D2-BA4B-00A0C93EC93B via the GPT add-on's
	// kTypeMap (src/add-ons/kernel/partitioning_systems/gpt/gpt_known_guids.h).


// ---------------------------------------------------------------------------
// RAII modification preparer — mirrors the helper class in DriveSetup's
// MainWindow.cpp so we get the same begin / cancel-or-commit semantics.
// ---------------------------------------------------------------------------
class ModificationPreparer {
public:
								ModificationPreparer(BDiskDevice* disk)
									:
									fDisk(disk),
									fStatus(disk->PrepareModifications())
								{
								}

								~ModificationPreparer()
								{
									if (fStatus == B_OK)
										fDisk->CancelModifications();
								}

			status_t			InitCheck() const { return fStatus; }

			status_t			Commit()
								{
									status_t status = fDisk->CommitModifications();
									if (status == B_OK)
										fStatus = B_ERROR;
												// prevent dtor from cancelling
									return status;
								}

private:
			BDiskDevice*		fDisk;
			status_t			fStatus;
};


// ---------------------------------------------------------------------------
// Pretty-printers
// ---------------------------------------------------------------------------
static void
print_size(off_t bytes)
{
	if (bytes >= 1024LL * 1024 * 1024)
		printf("%.2f GiB", (double)bytes / (1024.0 * 1024 * 1024));
	else if (bytes >= kMiB)
		printf("%.1f MiB", (double)bytes / (double)kMiB);
	else
		printf("%lld B", (long long)bytes);
}


static const char*
arch_loader_filename()
{
#if defined(__x86_64__)
	return "BOOTX64.EFI";
#elif defined(__i386__)
	return "BOOTIA32.EFI";
#elif defined(__aarch64__) || defined(__arm64__)
	return "BOOTAA64.EFI";
#elif defined(__riscv) && __riscv_xlen == 64
	return "BOOTRISCV64.EFI";
#else
	return NULL;
#endif
}


// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------
struct InspectResult {
	bool			isGPT;
	bool			hasValidESP;
	partition_id	espID;
	off_t			largestFreeSpace;
	off_t			largestFreeOffset;
};


static void
classify_partition(BPartition* partition, int32 level)
{
	for (int i = 0; i < level; i++)
		printf("  ");

	BPath path;
	partition->GetPath(&path);
	printf("[%" B_PRId32 "] %s  ", partition->ID(), path.Path() ? path.Path() : "(no path)");
	print_size(partition->Size());

	const char* type = partition->Type();
	const char* contentType = partition->ContentType();
	const char* contentName = partition->ContentName();

	printf("\n");
	for (int i = 0; i < level + 1; i++) printf("  ");
	printf("type=\"%s\"  contentType=\"%s\"  name=\"%s\"\n",
		type ? type : "(null)",
		contentType ? contentType : "(null)",
		contentName ? contentName : "(null)");

	for (int i = 0; i < level + 1; i++) printf("  ");
	printf("readOnly=%d  mounted=%d  children=%" B_PRId32 "\n",
		partition->IsReadOnly() ? 1 : 0,
		partition->IsMounted() ? 1 : 0,
		partition->CountChildren());

	if (partition->IsMounted()) {
		BPath mountPath;
		if (partition->GetMountPoint(&mountPath) == B_OK) {
			for (int i = 0; i < level + 1; i++) printf("  ");
			printf("mountPoint=%s\n", mountPath.Path());
		}
	}

	for (int32 i = 0; i < partition->CountChildren(); i++)
		classify_partition(partition->ChildAt(i), level + 1);
}


static InspectResult
inspect_disk(BDiskDevice* disk)
{
	InspectResult result;
	result.isGPT = false;
	result.hasValidESP = false;
	result.espID = -1;
	result.largestFreeSpace = 0;
	result.largestFreeOffset = 0;

	const char* contentType = disk->ContentType();
	result.isGPT = (contentType != NULL
		&& strcmp(contentType, kPartitionTypeEFI) == 0);

	for (int32 i = 0; i < disk->CountChildren(); i++) {
		BPartition* child = disk->ChildAt(i);
		const char* type = child->Type();
		const char* ct = child->ContentType();

		if (type != NULL && strcmp(type, kEFIPartitionType) == 0
			&& ct != NULL && strcmp(ct, kPartitionTypeFAT32) == 0
			&& !child->IsReadOnly()
			&& child->ContentSize() >= kMiB) {
			result.hasValidESP = true;
			result.espID = child->ID();
		}
	}

	BPartitioningInfo info;
	if (disk->GetPartitioningInfo(&info) == B_OK) {
		for (int32 i = 0; i < info.CountPartitionableSpaces(); i++) {
			off_t offset = 0;
			off_t size = 0;
			if (info.GetPartitionableSpaceAt(i, &offset, &size) != B_OK)
				continue;
			if (size > result.largestFreeSpace) {
				result.largestFreeSpace = size;
				result.largestFreeOffset = offset;
			}
		}
	}

	return result;
}


// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
static int
cmd_list()
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	int32 count = 0;

	while (roster.GetNextDevice(&device) == B_OK) {
		BPath path;
		device.GetPath(&path);

		printf("==== Disk #%" B_PRId32 ": %s ====\n", count, path.Path());
		printf("  ID=%" B_PRId32 "  size=", device.ID());
		print_size(device.Size());
		printf("  removable=%d  readOnly=%d\n",
			device.IsRemovableMedia() ? 1 : 0,
			device.IsReadOnly() ? 1 : 0);

		InspectResult r = inspect_disk(&device);
		printf("  GPT=%d  validESP=%d", r.isGPT, r.hasValidESP);
		if (r.hasValidESP)
			printf(" (id=%" B_PRId32 ")", r.espID);
		printf("  largestFreeSpace=");
		print_size(r.largestFreeSpace);
		printf("\n");
		count++;
	}

	if (count == 0)
		printf("(no disks reported by BDiskDeviceRoster)\n");

	return 0;
}


static int
cmd_inspect(const char* diskPath)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* matched = NULL;

	if (roster.GetPartitionForPath(diskPath, &device, &matched) != B_OK
		|| matched == NULL) {
		fprintf(stderr, "no disk matching path '%s'\n", diskPath);
		return 2;
	}

	BPath path;
	device.GetPath(&path);

	printf("Disk: %s\n", path.Path());
	printf("  id=%" B_PRId32 "  size=", device.ID());
	print_size(device.Size());
	printf("  contentType=\"%s\"\n",
		device.ContentType() ? device.ContentType() : "(null)");

	printf("\nPartition tree:\n");
	classify_partition(&device, 0);

	// GetPartitioningInfo on a real disk requires that the disk system
	// handle exists, which only happens during a PrepareModifications
	// session. Mirror DriveSetup's pattern: prepare → query → cancel
	// (we don't commit anything here).
	ModificationPreparer prep(&device);
	if (prep.InitCheck() != B_OK) {
		printf("\nPrepareModifications failed: %s\n",
			strerror(prep.InitCheck()));
		return 0;
	}

	BPartitioningInfo info;
	status_t infoStatus = device.GetPartitioningInfo(&info);
	printf("\nPartitionable spaces — GetPartitioningInfo() = %s\n",
		strerror(infoStatus));
	if (infoStatus == B_OK) {
		printf("  count = %" B_PRId32 "\n",
			info.CountPartitionableSpaces());
		for (int32 i = 0; i < info.CountPartitionableSpaces(); i++) {
			off_t offset = 0;
			off_t size = 0;
			status_t getErr = info.GetPartitionableSpaceAt(i, &offset, &size);
			printf("  [%" B_PRId32 "] err=%s offset=",
				i, strerror(getErr));
			print_size(offset);
			printf("  size=");
			print_size(size);
			printf("\n");
		}
	}

	InspectResult r = inspect_disk(&device);
	printf("\nClassification: GPT=%d, validESP=%d", r.isGPT, r.hasValidESP);
	if (r.hasValidESP)
		printf(" (id=%" B_PRId32 ")", r.espID);
	printf(", largestFree=");
	print_size(r.largestFreeSpace);
	printf("\n");

	return 0;
}


static int
cmd_create_esp(const char* diskPath, off_t requestedBytes)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* matched = NULL;

	if (roster.GetPartitionForPath(diskPath, &device, &matched) != B_OK) {
		fprintf(stderr, "no disk matching path '%s'\n", diskPath);
		return 2;
	}

	if (device.IsReadOnly()) {
		fprintf(stderr, "disk is read-only\n");
		return 2;
	}

	const char* ct = device.ContentType();
	if (ct == NULL) {
		fprintf(stderr, "disk has no partitioning system\n");
		return 2;
	}

	// Accept both GPT and MBR (Intel Partition Map) — many real UEFI-bootable
	// disks in the wild are MBR with an EFI System Partition (type 0xEF).
	bool isGPT = strcmp(ct, kPartitionTypeEFI) == 0;
	bool isMBR = strcmp(ct, kPartitionTypeIntel) == 0;
	if (!isGPT && !isMBR) {
		fprintf(stderr, "unsupported partitioning system: \"%s\"\n", ct);
		return 2;
	}
	printf("==> Disk partitioning: %s\n", isGPT ? "GPT" : "MBR");

	// GetPartitioningInfo requires an active PrepareModifications session,
	// so we begin the session first and query inside it.
	ModificationPreparer prep(&device);
	if (prep.InitCheck() != B_OK) {
		fprintf(stderr, "PrepareModifications failed: %s\n",
			strerror(prep.InitCheck()));
		return 2;
	}

	BPartitioningInfo info;
	if (device.GetPartitioningInfo(&info) != B_OK) {
		fprintf(stderr, "GetPartitioningInfo failed\n");
		return 2;
	}

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

	if (bestSize < requestedBytes) {
		fprintf(stderr, "not enough free space: largest=%lld B, need=%lld B\n",
			(long long)bestSize, (long long)requestedBytes);
		return 2;
	}

	printf("==> Free slot: offset=");
	print_size(bestOffset);
	printf("  size=");
	print_size(bestSize);
	printf("\n");

	off_t offset = bestOffset;
	off_t size = requestedBytes;
	BString name = "Haiku ESP";
	BString params;

	printf("==> Validating CreateChild offset=%lld size=%lld type=\"%s\"\n",
		(long long)offset, (long long)size, kEFIPartitionType);

	status_t status = device.ValidateCreateChild(&offset, &size,
		kEFIPartitionType, &name, params.String());
	if (status != B_OK) {
		fprintf(stderr, "ValidateCreateChild failed: %s\n", strerror(status));
		return 2;
	}

	printf("    validated offset=%lld size=%lld name=\"%s\"\n",
		(long long)offset, (long long)size, name.String());

	BPartition* created = NULL;
	status = device.CreateChild(offset, size, kEFIPartitionType,
		name.String(), params.String(), &created);
	if (status != B_OK) {
		fprintf(stderr, "CreateChild failed: %s\n", strerror(status));
		return 2;
	}

	printf("==> CreateChild OK (in-memory id=%" B_PRId32 ")\n",
		created->ID());

	printf("==> Committing partition creation (separate from initialize)...\n");
	status = prep.Commit();
	if (status != B_OK) {
		fprintf(stderr, "Commit failed: %s\n", strerror(status));
		return 2;
	}

	// Re-acquire the disk after the create-commit. The new partition gets
	// scanned by the kernel before we try to initialize it; doing the init
	// in a separate commit avoids the "partition already has a DiskSystem"
	// rejection from _user_initialize_partition when leftover filesystem
	// signatures are detected in the freed space.
	BDiskDevice freshDevice;
	BPartition* freshESP = NULL;
	if (roster.GetPartitionForPath(diskPath, &freshDevice, &freshESP) != B_OK) {
		fprintf(stderr, "could not re-acquire disk after commit\n");
		return 2;
	}

	partition_id newID = -1;
	for (int32 i = 0; i < freshDevice.CountChildren(); i++) {
		BPartition* child = freshDevice.ChildAt(i);
		if (child->Type() != NULL
			&& strcmp(child->Type(), kEFIPartitionType) == 0
			&& child->Offset() == offset) {
			newID = child->ID();
			break;
		}
	}

	if (newID < 0) {
		fprintf(stderr, "could not locate newly-created ESP after commit\n");
		return 2;
	}

	printf("==> Created partition id=%" B_PRId32 "\n", newID);

	// Second commit: format the partition as FAT.
	BDiskDevice initDevice;
	BPartition* toInit = NULL;
	if (roster.GetPartitionWithID(newID, &initDevice, &toInit) != B_OK) {
		fprintf(stderr, "GetPartitionWithID(new) failed\n");
		return 2;
	}

	if (toInit->ContentType() != NULL) {
		printf("==> New partition was auto-detected as \"%s\"; uninitializing first\n",
			toInit->ContentType());

		ModificationPreparer prepUninit(&initDevice);
		if (prepUninit.InitCheck() != B_OK) {
			fprintf(stderr, "PrepareModifications (uninit) failed: %s\n",
				strerror(prepUninit.InitCheck()));
			return 2;
		}
		status = toInit->Uninitialize();
		if (status != B_OK) {
			fprintf(stderr, "Uninitialize failed: %s\n", strerror(status));
			return 2;
		}
		status = prepUninit.Commit();
		if (status != B_OK) {
			fprintf(stderr, "Commit (uninit) failed: %s\n", strerror(status));
			return 2;
		}

		// Re-acquire after uninit-commit.
		if (roster.GetPartitionWithID(newID, &initDevice, &toInit) != B_OK) {
			fprintf(stderr, "re-acquire after uninit failed\n");
			return 2;
		}
	}

	ModificationPreparer prepInit(&initDevice);
	if (prepInit.InitCheck() != B_OK) {
		fprintf(stderr, "PrepareModifications (init) failed: %s\n",
			strerror(prepInit.InitCheck()));
		return 2;
	}

	BString fsName(kPartitionTypeFAT32);
	BString volumeName = "ESP";
	BString fsParams;
	// FAT init parameters taken from
	// src/add-ons/disk_systems/fat/InitializeParameterEditor.cpp.
	// "fat 0" = auto-select FAT12/16/32 based on volume size. UEFI firmware
	// accepts any FAT variant for the ESP.
	fsParams << "fat 0;\nname \"" << volumeName << "\";\n";

	status = toInit->ValidateInitialize(fsName.String(),
		&volumeName, fsParams.String());
	if (status != B_OK) {
		fprintf(stderr, "ValidateInitialize failed: %s\n", strerror(status));
		return 2;
	}

	printf("==> Initializing as %s, volumeName=\"%s\"\n",
		fsName.String(), volumeName.String());

	status = toInit->Initialize(fsName.String(), volumeName.String(),
		fsParams.String());
	if (status != B_OK) {
		fprintf(stderr, "Initialize failed: %s\n", strerror(status));
		return 2;
	}

	printf("==> Initialize OK; committing format...\n");
	status = prepInit.Commit();
	if (status != B_OK) {
		fprintf(stderr, "Commit (init) failed: %s\n", strerror(status));
		return 2;
	}

	printf("==> Format committed. ESP id=%" B_PRId32 "\n", newID);

	BDiskDevice mountDev;
	BPartition* esp = NULL;
	if (roster.GetPartitionWithID(newID, &mountDev, &esp) != B_OK
		|| esp == NULL) {
		fprintf(stderr, "GetPartitionWithID(new ESP) failed\n");
		return 2;
	}

	if (!esp->IsMounted()) {
		printf("==> Mounting...\n");
		status = esp->Mount();
		if (status < B_OK) {
			fprintf(stderr, "Mount failed: %s\n", strerror(status));
			return 2;
		}
	}

	BPath mountPath;
	if (esp->GetMountPoint(&mountPath) != B_OK) {
		fprintf(stderr, "GetMountPoint failed\n");
		return 2;
	}

	printf("==> Mounted at: %s\n", mountPath.Path());

	BPath testFile(mountPath);
	testFile.Append("haiku_uefi_setup_spike.txt");
	BFile out(testFile.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (out.InitCheck() != B_OK) {
		fprintf(stderr, "create test file failed: %s\n",
			strerror(out.InitCheck()));
		return 2;
	}

	const char* payload = "spike OK\n";
	if (out.Write(payload, strlen(payload)) != (ssize_t)strlen(payload)) {
		fprintf(stderr, "write to ESP failed\n");
		return 2;
	}

	printf("==> Wrote %s (%zu bytes)\n", testFile.Path(), strlen(payload));
	printf("==> SUCCESS — ESP id=%" B_PRId32 " ready at %s\n",
		newID, mountPath.Path());

	return 0;
}


static int
cmd_copy_loader(partition_id espID)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* esp = NULL;

	if (roster.GetPartitionWithID(espID, &device, &esp) != B_OK
		|| esp == NULL) {
		fprintf(stderr, "no partition with id %" B_PRId32 "\n", espID);
		return 2;
	}

	BPath loaderSrc;
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &loaderSrc) != B_OK
		|| loaderSrc.Append("platform_loaders/haiku_loader.efi") != B_OK) {
		fprintf(stderr, "could not resolve loader source path\n");
		return 2;
	}

	BFile src(loaderSrc.Path(), B_READ_ONLY);
	if (src.InitCheck() != B_OK) {
		fprintf(stderr, "open loader source %s failed: %s\n",
			loaderSrc.Path(), strerror(src.InitCheck()));
		return 2;
	}

	off_t loaderSize = 0;
	src.GetSize(&loaderSize);
	printf("==> Loader source: %s (", loaderSrc.Path());
	print_size(loaderSize);
	printf(")\n");

	if (!esp->IsMounted()) {
		printf("==> Mounting ESP...\n");
		status_t status = esp->Mount();
		if (status < B_OK) {
			fprintf(stderr, "Mount failed: %s\n", strerror(status));
			return 2;
		}
	}

	BPath mountPath;
	if (esp->GetMountPoint(&mountPath) != B_OK) {
		fprintf(stderr, "GetMountPoint failed\n");
		return 2;
	}

	printf("==> ESP mounted at %s\n", mountPath.Path());

	BPath bootDir(mountPath);
	bootDir.Append("EFI/BOOT");
	if (create_directory(bootDir.Path(), 0755) != B_OK) {
		fprintf(stderr, "create_directory %s failed\n", bootDir.Path());
		return 2;
	}

	const char* fname = arch_loader_filename();
	if (fname == NULL) {
		fprintf(stderr, "unknown architecture for EFI loader filename\n");
		return 2;
	}

	BPath dest(bootDir);
	dest.Append(fname);

	BFile out(dest.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (out.InitCheck() != B_OK) {
		fprintf(stderr, "create dest %s failed: %s\n",
			dest.Path(), strerror(out.InitCheck()));
		return 2;
	}

	char buffer[64 * 1024];
	off_t copied = 0;
	while (true) {
		ssize_t got = src.Read(buffer, sizeof(buffer));
		if (got < 0) {
			fprintf(stderr, "read source failed: %s\n", strerror(got));
			return 2;
		}
		if (got == 0)
			break;
		if (out.Write(buffer, got) != got) {
			fprintf(stderr, "write dest failed\n");
			return 2;
		}
		copied += got;
	}

	printf("==> Copied %lld bytes to %s\n", (long long)copied, dest.Path());

	BPath haikuDir(mountPath);
	haikuDir.Append("EFI/Haiku");
	create_directory(haikuDir.Path(), 0755);

	BPath haikuDest(haikuDir);
	haikuDest.Append("haiku_loader.efi");

	src.Seek(0, SEEK_SET);
	BFile out2(haikuDest.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (out2.InitCheck() == B_OK) {
		while (true) {
			ssize_t got = src.Read(buffer, sizeof(buffer));
			if (got <= 0)
				break;
			out2.Write(buffer, got);
		}
		printf("==> Also copied to %s\n", haikuDest.Path());
	}

	printf("==> SUCCESS\n");
	return 0;
}


static int
cmd_delete(partition_id id)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* target = NULL;

	if (roster.GetPartitionWithID(id, &device, &target) != B_OK
		|| target == NULL) {
		fprintf(stderr, "no partition with id %" B_PRId32 "\n", id);
		return 2;
	}

	if (target->IsMounted())
		target->Unmount();

	BPartition* parent = target->Parent();
	if (parent == NULL) {
		fprintf(stderr, "partition has no parent — refusing to delete a disk\n");
		return 2;
	}

	ModificationPreparer prep(&device);
	if (prep.InitCheck() != B_OK) {
		fprintf(stderr, "PrepareModifications failed: %s\n",
			strerror(prep.InitCheck()));
		return 2;
	}

	int32 index = target->Index();
	if (!parent->CanDeleteChild(index)) {
		fprintf(stderr, "CanDeleteChild reports false (index=%" B_PRId32 ")\n",
			index);
		return 2;
	}

	status_t status = parent->DeleteChild(index);
	if (status != B_OK) {
		fprintf(stderr, "DeleteChild failed: %s\n", strerror(status));
		return 2;
	}

	status = prep.Commit();
	if (status != B_OK) {
		fprintf(stderr, "Commit failed: %s\n", strerror(status));
		return 2;
	}

	printf("==> Deleted partition id=%" B_PRId32 "\n", id);
	return 0;
}


// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
static void
usage()
{
	fprintf(stderr,
		"Usage:\n"
		"  uefi_spike list\n"
		"  uefi_spike inspect <disk-path>\n"
		"  uefi_spike create-esp <disk-path> [--size MB]\n"
		"  uefi_spike copy-loader <esp-partition-id>\n"
		"  uefi_spike delete <partition-id>\n");
}


int
main(int argc, char* argv[])
{
	BApplication app(kAppSignature);

	if (argc < 2) {
		usage();
		return 1;
	}

	const char* cmd = argv[1];

	if (strcmp(cmd, "list") == 0)
		return cmd_list();

	if (strcmp(cmd, "inspect") == 0) {
		if (argc < 3) { usage(); return 1; }
		return cmd_inspect(argv[2]);
	}

	if (strcmp(cmd, "create-esp") == 0) {
		if (argc < 3) { usage(); return 1; }
		off_t bytes = kDefaultESPBytes;
		for (int i = 3; i < argc - 1; i++) {
			if (strcmp(argv[i], "--size") == 0) {
				int mb = atoi(argv[i + 1]);
				if (mb > 0)
					bytes = (off_t)mb * kMiB;
			}
		}
		return cmd_create_esp(argv[2], bytes);
	}

	if (strcmp(cmd, "copy-loader") == 0) {
		if (argc < 3) { usage(); return 1; }
		partition_id id = atoi(argv[2]);
		return cmd_copy_loader(id);
	}

	if (strcmp(cmd, "delete") == 0) {
		if (argc < 3) { usage(); return 1; }
		partition_id id = atoi(argv[2]);
		return cmd_delete(id);
	}

	usage();
	return 1;
}
