/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_LOADER_INSTALLER_H
#define UEFI_WIZARD_LOADER_INSTALLER_H


#include <Path.h>
#include <SupportDefs.h>


class IProgressLogger;


// Copies haiku_loader.efi onto a mounted ESP at:
//   /EFI/BOOT/BOOT<arch>.EFI       — firmware fallback path
//   /EFI/Haiku/haiku_loader.efi    — stable path for boot managers (rEFInd)
//
// If a loader is already present at either destination it's renamed to
// *_old.EFI before being overwritten — the same convention the in-tree
// WorkerThread::InstallEFILoader uses (src/apps/installer/WorkerThread.cpp).
class LoaderInstaller {
public:
								LoaderInstaller();
								~LoaderInstaller();

	// Drive the install. mountedESP is the path returned by
	// ESPManager::Mount. loaderSource is the path to haiku_loader.efi
	// (typically resolved by ArchUtils::FindLoaderSourcePath). On success
	// outDestination is the /EFI/BOOT/BOOT<arch>.EFI path actually written.
			status_t			Install(const BPath& mountedESP,
									const BPath& loaderSource,
									BPath& outDestination,
									IProgressLogger* logger);

private:
			status_t			_EnsureDirectory(const BPath& dir,
									IProgressLogger* logger);
			status_t			_CopyFileWithBackup(const BPath& source,
									const BPath& destination,
									IProgressLogger* logger);

								LoaderInstaller(const LoaderInstaller&);
			LoaderInstaller&	operator=(const LoaderInstaller&);
};


#endif	// UEFI_WIZARD_LOADER_INSTALLER_H
