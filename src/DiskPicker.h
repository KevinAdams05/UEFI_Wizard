/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_DISK_PICKER_H
#define UEFI_WIZARD_DISK_PICKER_H


#include <ColumnListView.h>
#include <String.h>
#include <View.h>


class BButton;
class BMessage;


// Wizard page (BView) listing every disk reported by BDiskDeviceRoster.
// Posts kMsgDiskSelected to its target with "disk_path" set to the
// device's raw path (e.g. /dev/disk/scsi/0/0/0/raw) on every row change.
// The MainWindow uses that path to drive ESPManager.
class DiskPicker : public BView {
public:
								DiskPicker();
	virtual						~DiskPicker();

	virtual void				AttachedToWindow();
	virtual void				MessageReceived(BMessage* message);

			void				Reload();
			BString				SelectedDiskPath() const;

private:
			void				_PopulateRows();

			BColumnListView*	fDiskList;
			BButton*			fRefreshButton;
			BString				fSelectedPath;
};


#endif	// UEFI_WIZARD_DISK_PICKER_H
