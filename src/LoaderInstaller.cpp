/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 *
 * Loader-copy logic mirrors src/apps/installer/WorkerThread.cpp::
 * InstallEFILoader — including the "rename existing to *_old.EFI"
 * behaviour — but extends it with a parallel copy to /EFI/Haiku/ so
 * boot managers like rEFInd have a stable path to chain against.
 */


#include "LoaderInstaller.h"

#include <stdio.h>
#include <string.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <String.h>

#include "ArchUtils.h"
#include "IProgressLogger.h"


static void
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


static void
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


static void
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


LoaderInstaller::LoaderInstaller()
{
}


LoaderInstaller::~LoaderInstaller()
{
}


status_t
LoaderInstaller::Install(const BPath& mountedESP, const BPath& loaderSource,
	BPath& outDestination, IProgressLogger* logger)
{
	const char* archName = ArchUtils::DefaultBootFileName();
	if (archName == NULL) {
		LogErrorF(logger, "Unknown architecture for EFI loader filename.");
		return B_NOT_SUPPORTED;
	}

	BFile srcCheck(loaderSource.Path(), B_READ_ONLY);
	if (srcCheck.InitCheck() != B_OK) {
		LogErrorF(logger, "Cannot open loader source %s: %s",
			loaderSource.Path(), strerror(srcCheck.InitCheck()));
		return srcCheck.InitCheck();
	}
	srcCheck.Unset();

	// /EFI/BOOT/BOOT<arch>.EFI — the firmware fallback path used when no
	// Boot#### NVRAM entry directs the firmware elsewhere.
	BPath bootDir(mountedESP);
	bootDir.Append("EFI");
	bootDir.Append("BOOT");

	status_t status = _EnsureDirectory(bootDir, logger);
	if (status != B_OK)
		return status;

	BPath bootDest(bootDir);
	bootDest.Append(archName);

	LogStepF(logger, "Copying loader to %s", bootDest.Path());
	status = _CopyFileWithBackup(loaderSource, bootDest, logger);
	if (status != B_OK)
		return status;

	outDestination = bootDest;

	// /EFI/Haiku/haiku_loader.efi — stable path so boot managers (rEFInd
	// in particular) can chain against a known target. Failures here are
	// non-fatal: the firmware fallback at /EFI/BOOT/BOOT*.EFI is the
	// load-bearing copy.
	BPath haikuDir(mountedESP);
	haikuDir.Append("EFI");
	haikuDir.Append("Haiku");

	if (_EnsureDirectory(haikuDir, NULL) == B_OK) {
		BPath haikuDest(haikuDir);
		haikuDest.Append("haiku_loader.efi");

		LogStepF(logger, "Copying loader to %s", haikuDest.Path());
		status_t haikuStatus = _CopyFileWithBackup(loaderSource, haikuDest,
			logger);
		if (haikuStatus != B_OK) {
			LogDetailF(logger,
				"Secondary copy to %s failed (%s) — primary copy is enough.",
				haikuDest.Path(), strerror(haikuStatus));
		}
	}

	return B_OK;
}


status_t
LoaderInstaller::_EnsureDirectory(const BPath& dir, IProgressLogger* logger)
{
	if (create_directory(dir.Path(), 0755) != B_OK) {
		LogErrorF(logger, "create_directory %s failed.", dir.Path());
		return B_ERROR;
	}

	BDirectory check(dir.Path());
	if (check.InitCheck() != B_OK) {
		LogErrorF(logger, "Cannot open directory %s: %s",
			dir.Path(), strerror(check.InitCheck()));
		return check.InitCheck();
	}
	return B_OK;
}


status_t
LoaderInstaller::_CopyFileWithBackup(const BPath& source,
	const BPath& destination, IProgressLogger* logger)
{
	// If the destination already exists, rename it to <name>_old.<ext>
	// before overwriting. Mirrors the in-tree InstallEFILoader behaviour
	// so users who already have a working loader keep a recovery copy.
	BEntry existing(destination.Path());
	if (existing.Exists()) {
		BString backupName = destination.Leaf();
		int32 dot = backupName.FindLast('.');
		if (dot >= 0)
			backupName.Insert("_old", dot);
		else
			backupName << "_old";

		BPath backupPath;
		destination.GetParent(&backupPath);
		backupPath.Append(backupName.String());

		// Best-effort: if a previous *_old already exists, leave it alone
		// and let Rename clobber. (Rename(true) overrides.)
		if (existing.Rename(backupName.String(), true) != B_OK) {
			LogDetailF(logger,
				"Could not rename existing %s to %s — overwriting.",
				destination.Path(), backupName.String());
		} else {
			LogDetailF(logger, "Renamed existing %s -> %s",
				destination.Leaf(), backupName.String());
		}
	}

	BFile src(source.Path(), B_READ_ONLY);
	if (src.InitCheck() != B_OK) {
		LogErrorF(logger, "Cannot open loader source %s: %s",
			source.Path(), strerror(src.InitCheck()));
		return src.InitCheck();
	}

	BFile dest(destination.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (dest.InitCheck() != B_OK) {
		LogErrorF(logger, "Cannot create %s: %s",
			destination.Path(), strerror(dest.InitCheck()));
		return dest.InitCheck();
	}

	off_t totalSize = 0;
	src.GetSize(&totalSize);

	const size_t kBufferSize = 64 * 1024;
	char buffer[kBufferSize];
	off_t copied = 0;

	while (true) {
		ssize_t got = src.Read(buffer, kBufferSize);
		if (got < 0) {
			LogErrorF(logger, "Read from %s failed: %s",
				source.Path(), strerror(got));
			return got;
		}
		if (got == 0)
			break;

		ssize_t wrote = dest.Write(buffer, got);
		if (wrote != got) {
			LogErrorF(logger, "Short write to %s (%zd of %zd bytes).",
				destination.Path(), (ssize_t)wrote, (ssize_t)got);
			return B_IO_ERROR;
		}
		copied += got;
	}

	LogDetailF(logger, "Wrote %lld bytes (source size %lld).",
		(long long)copied, (long long)totalSize);
	return B_OK;
}
