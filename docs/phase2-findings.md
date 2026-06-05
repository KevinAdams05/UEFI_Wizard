# Phase 2 — API spike findings

> [!NOTE]
> An LLM was used to aid in development of this code.

**Author:** Kevin Adams &lt;kevinadams05@gmail.com&gt;
**Date:** 2026-05-09
**Spike binary:** [`spike/uefi_spike.cpp`](../spike/uefi_spike.cpp), cross-built via [`spike/build-spike.sh`](../spike/build-spike.sh).
**Test box:** `user@192.168.74.53` (Haiku, internal SATA SSD authorized for destructive tests).

---

## TL;DR

All four risks identified during initial design are resolved:

1. `BPartition::CreateChild` persisting the GPT/MBR type at create time.
2. Live-USB write access to a freshly-created ESP.
3. `BPartition::Initialize("FAT32 File System")` from app code.
4. Whether the type goes via the parent's parameter dict or the
   `typeString` argument. The complete `inspect → create → uninitialize-if-needed →
initialize → mount → write → copy-loader` sequence ran end-to-end on a real
disk, producing a bootable-shaped `BOOTX64.EFI` at `/esp/EFI/BOOT/`. We can
move to Phase 3 (porting the proven sequence into `ESPManager` and
`LoaderInstaller`) with confidence.

The spike also surfaced four **new** issues that the production code must
account for; they're documented inline below and called out in §7.

---

## 1. Validated risks

| Risk | Status | How proven |
| --- | --- | --- |
| #1 — `BPartition::CreateChild` persists GPT/MBR type at create time | ✓ Resolved | Created a 256 MB child with `typeString="EFI system data"`; after commit, `inspect` reports `type="EFI system data"` on the new partition. No parameter dict needed — type goes in the `typeString` argument directly. |
| #2 — Live-USB write access to a freshly mounted ESP | ✓ Resolved | After format, the partition auto-mounted at `/esp` and a `BFile(B_WRITE_ONLY \| B_CREATE_FILE \| B_ERASE_FILE)` write succeeded with no privilege escalation. Subsequent `EFI/BOOT/BOOTX64.EFI` (504 360 B) and `EFI/Haiku/haiku_loader.efi` writes also succeeded. |
| #3 — `BPartition::Initialize("FAT32 File System")` formats FAT from app code | ✓ Resolved | After the two-step commit fix (see §3), the InitializeJob completed and the partition mounted as FAT. Parameter format must follow the FAT add-on's grammar — see §2. |
| #4 — Type passed via parent `CreateChild` typeString, not parameter dict | ✓ Resolved | Confirmed by reading `src/add-ons/disk_systems/gpt/GPTPartitionHandle.cpp::CreateChild` and the matching MBR add-on, then validating by behaviour. The GPT add-on translates the type string via `kTypeMap` (in `src/add-ons/kernel/partitioning_systems/gpt/gpt_known_guids.h`); the MBR add-on translates it via `PartitionType::SetType` (which knows MBR byte 0xEF as "EFI system data"). Either way, callers pass a plain string. |

## 2. The exact API sequence that works

```cpp
// ---------- Phase A: create the partition ----------
ModificationPreparer prep(&device);
// (PrepareModifications must be active for GetPartitioningInfo to work.)

BPartitioningInfo info;
device.GetPartitioningInfo(&info);
// Find the largest partitionable space at info.GetPartitionableSpaceAt(i).

off_t offset = pickedOffset, size = 256 * 1024 * 1024;
BString name, params;
device.ValidateCreateChild(&offset, &size, "EFI system data", &name, params);

BPartition* created = NULL;
device.CreateChild(offset, size, "EFI system data", name, params, &created);
prep.Commit();
// CreateChildJob runs here. The kernel re-scans the new area and may
// auto-detect a stale filesystem signature in the freed space. That's
// what blocks Phase B if we skip the second commit.

// ---------- Phase B: uninitialize if needed, then format ----------
roster.GetPartitionWithID(newID, &freshDevice, &toInit);
if (toInit->ContentType() != NULL) {
    ModificationPreparer prepUninit(&freshDevice);
    toInit->Uninitialize();
    prepUninit.Commit();
    // Re-acquire toInit after this commit.
}

ModificationPreparer prepInit(&freshDevice);
BString fsParams;
fsParams << "fat 0;\nname \"ESP\";\n";    // grammar from FAT InitializeParameterEditor
BString volName = "ESP";
toInit->ValidateInitialize("FAT32 File System", &volName, fsParams);
toInit->Initialize("FAT32 File System", volName, fsParams);
prepInit.Commit();

// ---------- Phase C: mount + copy ----------
roster.GetPartitionWithID(newID, &mountDev, &esp);
if (!esp->IsMounted())
    esp->Mount();
esp->GetMountPoint(&mountPath);
// Now /<mountPath>/EFI/BOOT/BOOTX64.EFI is writable.
```

Notable bits that are not obvious from the public docs:

- **`GetPartitioningInfo` requires an active `PrepareModifications` session.**
  Outside one it returns `B_NO_INIT` ("Initialization failed"). DriveSetup's
  `_Create` is laid out the same way; following the same order keeps us
  safe.
- **CreateChild and Initialize must commit separately.** Putting both in
  one `prep.Commit()` causes `_user_initialize_partition` (in
  `src/system/kernel/disk_device_manager/ddm_userland_interface.cpp:1144`)
  to return `B_BAD_VALUE` — the kernel re-scans the new partition area
  during the CreateChildJob, finds residual filesystem metadata
  (BFS leftovers from a previously-deleted partition in the same offset),
  and assigns it a `DiskSystem`. The InitializeJob then refuses because
  `partition->DiskSystem() != NULL`.
- **FAT init grammar:** `"fat <bits>;\nname \"<name>\";\n"`. `fat 0`
  auto-selects FAT12/16/32 by size; `fat 32` explicitly forces FAT32 but
  fails on partitions below FAT32's minimum cluster count (~520 MB).
  UEFI firmware accepts any FAT variant for the ESP, so `fat 0` is the
  right default.
- **`DeleteChild` takes `Index()`, not `ID()`.** This is unfortunate API
  shape — `BPartition::DeleteChild(int32 index)` accepts the
  *position-in-children-list*, not the persistent `partition_id`. Easy to
  get wrong; my first spike attempt failed with `Invalid Argument` because
  of this.

## 3. Surprising findings about real-world disks

### 3.1 "Intel Partition Map" parents host valid ESPs

Both disks on the test box (internal SSD + USB live medium) had
`parent->ContentType() == "Intel Partition Map"`, even though the internal
SSD is currently UEFI-booting Haiku from its own ESP at `/efi`. Their
EFI System partitions have MBR type byte `0xEF`, which the in-tree MBR
partitioning system maps to the same `"EFI system data"` string the GPT
add-on uses (see `PartitionMap.cpp:141`).

**Implication:** the in-tree `EFIVisitor::Visit` filter in
`src/apps/installer/WorkerThread.cpp:1090` rejects partitions whose parent
isn't `"GUID Partition Map"`. **On these very disks the in-tree menu would
silently show nothing** — exactly the symptom users report on the
[forum](https://discuss.haiku-os.org/t/installer-new-install-efi-loader/18518).
Our `ESPManager` must classify by the partition itself, not the parent
disk-system name. The spike's `inspect_disk` already does this.

### 3.2 The kernel auto-detects stale filesystem signatures

After deleting a 447 GiB BFS partition (OS SSD) and creating a 256 MB child
in the freed area, the new partition came back with non-null
`ContentType()` because the BFS superblock from the previous tenant was
still sitting in the new partition's offset range. This forced the
two-commit pattern documented in §2.

**Implication:** v1's `ESPManager::CreateESP` must always be prepared to
call `Uninitialize()` between the create-commit and the format-commit when
`ContentType()` comes back non-null. The spike now does this conditionally;
the production code should do the same.

### 3.3 Disk inventory at test time

```
/dev/disk/scsi/0/0/0/raw — internal SSD, 447.14 GiB, MBR
  [2] /dev/disk/scsi/0/0/0/0  128 MiB  type="EFI system data"  FAT32  /efi
  [3] /dev/disk/scsi/0/0/0/1  447.01 GiB  Be File System "OS SSD"  (deleted during spike)

/dev/disk/usb/0/0/raw — live USB anyboot, 28.85 GiB, MBR
  [4] /dev/disk/usb/0/0/0  3.91 GiB  Be File System "HaikuOS"  /boot
  [5] /dev/disk/usb/0/0/1  2.8 MiB  type="EFI system data"  FAT32 "haiku esp"
```

End-state of `/dev/disk/scsi/0/0/0/raw` after the spike:

```
[2] /dev/disk/scsi/0/0/0/0  128 MiB  type="EFI system data"  FAT32  /efi  (unchanged)
[8] /dev/disk/scsi/0/0/0/1  256 MiB  type="EFI system data"  FAT  "ESP"  /esp
    /esp/EFI/BOOT/BOOTX64.EFI       (504360 bytes)
    /esp/EFI/Haiku/haiku_loader.efi (504360 bytes)
```

## 4. Loader source path

`find_directory(B_SYSTEM_DATA_DIRECTORY)` plus
`platform_loaders/haiku_loader.efi` resolves to
`/boot/system/data/platform_loaders/haiku_loader.efi` in the live USB
session — exactly what the in-tree `InstallEFILoader` uses. 504 360 bytes
on the May 2026 nightly build the test box is running. No surprises.

## 5. Architecture-arch fallback name

`__x86_64__` is correctly detected by the cross-compiler; the spike picks
`BOOTX64.EFI`. Other architectures will need to be confirmed individually
when we have hardware to test on — the table follows the in-tree
`arch_efi_default_prefix()` exactly, so it should be a non-issue.

## 6. Test artifacts left on the box

Cleanup happens via `uefi_spike delete <id>` for each spike-created
partition. After Phase 2 the only spike-allocated partition (id=8, the
fresh ESP) was kept around for visual inspection — the user authorized
this, but it should be deleted before Phase 3 testing.

## 7. Implications for `ESPManager` and `LoaderInstaller`

1. **`ESPManager::Inspect` classifies by partition, not parent.** GPT and
   MBR (with type 0xEF) both produce `Type() == "EFI system data"`. Don't
   require `parent->ContentType() == "GUID Partition Map"`.
2. **`ESPManager::CreateESP` is a three-or-four-commit dance,** not a
   single atomic call:
   - Commit 1 (always): `PrepareModifications` → `CreateChild` → `Commit`.
   - Commit 2 (conditional): if `ContentType()` is non-null on the new
     partition, `Uninitialize` → `Commit`.
   - Commit 3 (always): `PrepareModifications` → `Initialize("FAT32 File
     System", "ESP", "fat 0;\nname \"ESP\";\n")` → `Commit`.
   The wizard's progress page should show all of these as named steps.
3. **`GetPartitioningInfo` callers must wrap with a `ModificationPreparer`.**
   Even read-only inspection ("how much free space is on this disk?")
   needs the session.
4. **`DeleteChild` API trap:** wrap with a helper that translates
   `partition_id` to `Index()` so the rest of the code can stay in
   id-space.
5. **No NVRAM, no problem.** With `BOOTX64.EFI` placed at `/EFI/BOOT/`,
   removable-media firmware fallback handles boot. v1 stays out of
   `efivars` territory.
6. **Mount path is `/esp`, `/efi`, etc. by name.** The kernel auto-mounts
   the new ESP using the volume name as the mount point. We don't need
   to invent or pass one.

## 8. What's left for Phase 3

- Port `cmd_create_esp` and `cmd_copy_loader` into `ESPManager` and
  `LoaderInstaller` (the production headers are already stubbed in
  `src/`). Keep the spike around as a regression check.
- Decide: does `ESPManager::CreateESP` accept arbitrary disk paths, or
  only those classified as `ESP_STATE_NONE_BUT_FREE_SPACE`? Lean toward
  the latter — any other state is a refusal at the wizard layer.
- Build out the GUI wizard pages on top.
- Add an explicit "Refresh disks" button — `BDiskDeviceRoster` does not
  notify on USB hotplug.
