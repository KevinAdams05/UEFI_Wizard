/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */


#include "ArchUtils.h"

#include <FindDirectory.h>
#include <Path.h>


namespace ArchUtils {


// Mirrors the table in
// src/apps/installer/WorkerThread.cpp::arch_efi_default_prefix() so the
// architectures stay in lockstep with the in-tree Installer.
const char*
DefaultBootFileName()
{
#if defined(__x86_64__)
	return "BOOTX64.EFI";
#elif defined(__i386__)
	return "BOOTIA32.EFI";
#elif defined(__aarch64__) || defined(__arm64__)
	return "BOOTAA64.EFI";
#elif defined(__arm__) || defined(__ARM__)
	return "BOOTARM.EFI";
#elif defined(__riscv) && __riscv_xlen == 64
	return "BOOTRISCV64.EFI";
#elif defined(__riscv) && __riscv_xlen == 32
	return "BOOTRISCV32.EFI";
#else
	return NULL;
#endif
}


status_t
FindLoaderSourcePath(BString& outPath)
{
	BPath path;
	status_t status = find_directory(B_SYSTEM_DATA_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	status = path.Append("platform_loaders/haiku_loader.efi");
	if (status != B_OK)
		return status;

	outPath = path.Path();
	return B_OK;
}


}	// namespace ArchUtils
