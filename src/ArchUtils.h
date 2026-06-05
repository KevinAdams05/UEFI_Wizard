/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_ARCH_UTILS_H
#define UEFI_WIZARD_ARCH_UTILS_H


#include <String.h>
#include <SupportDefs.h>


// EFI fallback boot loader filenames per architecture, as written by firmware
// when no Boot#### entry directs it elsewhere. Mirrors the table in
// src/apps/installer/WorkerThread.cpp::arch_efi_default_prefix() in the
// in-tree Installer.
namespace ArchUtils {

// Return the architecture-specific filename for /EFI/BOOT/, e.g.
// "BOOTX64.EFI" on x86_64. Returns NULL on an unrecognized arch.
const char*			DefaultBootFileName();

// Path to the source haiku_loader.efi shipped with the running system —
// typically /boot/system/data/platform_loaders/haiku_loader.efi. Returns
// B_OK if the file was found and the path was filled in.
status_t			FindLoaderSourcePath(BString& outPath);

}	// namespace ArchUtils


#endif	// UEFI_WIZARD_ARCH_UTILS_H
