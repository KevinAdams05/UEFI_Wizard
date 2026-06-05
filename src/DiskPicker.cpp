/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */


#include "DiskPicker.h"

#include <stdio.h>

#include <Button.h>
#include <Catalog.h>
#include <ColumnTypes.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Path.h>
#include <StringView.h>

#include "Constants.h"
#include "ESPManager.h"
#include "IProgressLogger.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DiskPicker"


// Column ids
enum {
	kColumnPath = 0,
	kColumnSize,
	kColumnRemovable,
	kColumnESPState,
};


// BRow subclass that remembers the disk's raw device path. The DiskPicker
// reads this back on selection to hand off to ESPManager.
class DiskRow : public BRow {
public:
								DiskRow(const char* path)
									:
									BRow(),
									fPath(path)
								{
								}

			const BString&		Path() const { return fPath; }

private:
			BString				fPath;
};


static BString
human_size_to_string(off_t bytes)
{
	const off_t kKiB = 1024;
	const off_t kMiB = 1024 * kKiB;
	const off_t kGiB = 1024 * kMiB;
	const off_t kTiB = 1024 * kGiB;
	BString text;
	if (bytes >= kTiB)
		text.SetToFormat("%.2f TiB", (double)bytes / (double)kTiB);
	else if (bytes >= kGiB)
		text.SetToFormat("%.2f GiB", (double)bytes / (double)kGiB);
	else if (bytes >= kMiB)
		text.SetToFormat("%.0f MiB", (double)bytes / (double)kMiB);
	else
		text.SetToFormat("%lld B", (long long)bytes);
	return text;
}


DiskPicker::DiskPicker()
	:
	BView("disk_picker", B_WILL_DRAW),
	fDiskList(NULL),
	fRefreshButton(NULL),
	fSelectedPath()
{
	fDiskList = new BColumnListView("disk_list",
		B_NAVIGABLE | B_WILL_DRAW, B_FANCY_BORDER, true);

	fDiskList->AddColumn(new BStringColumn(B_TRANSLATE("Disk"),
		240, 120, 480, B_TRUNCATE_MIDDLE), kColumnPath);
	fDiskList->AddColumn(new BStringColumn(B_TRANSLATE("Size"),
		90, 60, 120, B_TRUNCATE_END), kColumnSize);
	fDiskList->AddColumn(new BStringColumn(B_TRANSLATE("Removable"),
		90, 60, 120, B_TRUNCATE_END), kColumnRemovable);
	fDiskList->AddColumn(new BStringColumn(B_TRANSLATE("ESP state"),
		260, 100, 420, B_TRUNCATE_END), kColumnESPState);

	fDiskList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fDiskList->SetSelectionMessage(new BMessage(kMsgDiskSelected));

	// Make sure the window opens wide enough for all four columns; with
	// B_AUTO_UPDATE_SIZE_LIMITS the smallest contributing min wins.
	fDiskList->SetExplicitMinSize(BSize(700, 240));

	fRefreshButton = new BButton("refresh",
		B_TRANSLATE("Refresh"), new BMessage(kMsgRefreshDisks));

	BStringView* heading = new BStringView("heading",
		B_TRANSLATE("Select the disk you want to make UEFI-bootable:"));
	heading->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(heading)
		.Add(fDiskList)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fRefreshButton)
		.End()
	.End();
}


DiskPicker::~DiskPicker()
{
}


void
DiskPicker::AttachedToWindow()
{
	BView::AttachedToWindow();

	fDiskList->SetTarget(this);
	fRefreshButton->SetTarget(this);

	if (fDiskList->CountRows() == 0)
		_PopulateRows();
}


void
DiskPicker::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefreshDisks:
			Reload();
			break;

		case kMsgDiskSelected:
		{
			BRow* row = fDiskList->CurrentSelection();
			DiskRow* diskRow = dynamic_cast<DiskRow*>(row);
			if (diskRow != NULL) {
				fSelectedPath = diskRow->Path();
				BMessage notice(kMsgDiskSelected);
				notice.AddString("disk_path", fSelectedPath.String());
				if (Window() != NULL)
					Window()->PostMessage(&notice);
			}
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


void
DiskPicker::Reload()
{
	while (BRow* row = fDiskList->RowAt(0, NULL)) {
		fDiskList->RemoveRow(row);
		delete row;
	}
	fSelectedPath = "";
	_PopulateRows();
}


BString
DiskPicker::SelectedDiskPath() const
{
	return fSelectedPath;
}


void
DiskPicker::_PopulateRows()
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	ESPManager manager;
	NullProgressLogger nullLogger;

	while (roster.GetNextDevice(&device) == B_OK) {
		BPath path;
		device.GetPath(&path);

		DiskRow* row = new DiskRow(path.Path());
		row->SetField(new BStringField(path.Path()), kColumnPath);

		BString sizeText = human_size_to_string(device.Size());
		row->SetField(new BStringField(sizeText.String()), kColumnSize);

		row->SetField(new BStringField(device.IsRemovableMedia()
			? B_TRANSLATE("Yes") : B_TRANSLATE("No")), kColumnRemovable);

		ESPInspection result;
		manager.Inspect(path.Path(), kDefaultESPSize, result, &nullLogger);

		BString stateText;
		switch (result.state) {
			case ESP_STATE_VALID:
				stateText.SetToFormat(B_TRANSLATE("Existing ESP (%s)"),
					human_size_to_string(result.espSizeBytes).String());
				break;
			case ESP_STATE_NONE_BUT_FREE_SPACE:
				stateText.SetToFormat(
					B_TRANSLATE("No ESP — %s free for new one"),
					human_size_to_string(result.freeContiguousBytes).String());
				break;
			case ESP_STATE_NONE_NO_FREE_SPACE:
				stateText = B_TRANSLATE(
					"No ESP, no free space (use DriveSetup)");
				break;
			case ESP_STATE_NO_PARTITIONING_SYSTEM:
				stateText = B_TRANSLATE("No partition table");
				break;
			case ESP_STATE_UNSUPPORTED_PARTITIONING:
				stateText = B_TRANSLATE("Unsupported partitioning");
				break;
			case ESP_STATE_INSPECTION_FAILED:
			default:
				stateText = B_TRANSLATE("Could not inspect");
				break;
		}
		row->SetField(new BStringField(stateText.String()), kColumnESPState);

		fDiskList->AddRow(row);
	}
}

