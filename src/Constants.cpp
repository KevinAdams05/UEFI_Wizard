/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */


#include "Constants.h"


const char* kAppSignature = "application/x-vnd.UEFIWizard";
const char* kAppName = "UEFI Wizard";

// EFI System Partition GPT type GUID. The GPT disk-system add-on translates
// the human-readable string "EFI system data" to this GUID at create time
// (src/add-ons/kernel/partitioning_systems/gpt/gpt_known_guids.h kTypeMap).
// Stored here for documentation and any future direct-GUID consumers.
const char* kEFISystemPartitionGUID = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B";

// Default ESP size — 256 MiB. Microsoft's modern recommendation is 100 MiB
// or 260 MiB; we round up for headroom in dual-boot scenarios while staying
// modest on small disks. UEFI firmware accepts any FAT variant for the ESP,
// and at this size mkdosfs auto-selects FAT16, which all firmware reads.
const off_t kDefaultESPSize = 256LL * 1024 * 1024;
