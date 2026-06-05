/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */


#include "MainWindow.h"

#include <stdio.h>

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>

#include "Constants.h"
#include "DiskPicker.h"
#include "IProgressLogger.h"
#include "WorkerThread.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static BString
human_size(off_t bytes)
{
	const off_t kKiB = 1024;
	const off_t kMiB = 1024 * kKiB;
	const off_t kGiB = 1024 * kMiB;
	const off_t kTiB = 1024 * kGiB;
	BString out;
	if (bytes >= kTiB)
		out.SetToFormat("%.2f TiB", (double)bytes / (double)kTiB);
	else if (bytes >= kGiB)
		out.SetToFormat("%.2f GiB", (double)bytes / (double)kGiB);
	else if (bytes >= kMiB)
		out.SetToFormat("%.0f MiB", (double)bytes / (double)kMiB);
	else
		out.SetToFormat("%lld B", (long long)bytes);
	return out;
}


static BTextView*
make_static_textview(const char* name)
{
	BTextView* view = new BTextView(name, B_WILL_DRAW | B_FRAME_EVENTS);
	view->MakeEditable(false);
	view->MakeSelectable(true);
	view->SetWordWrap(true);
	view->SetStylable(false);
	view->SetInsets(8, 8, 8, 8);
	// 480 px = comfortable reading width for one of the wizard's
	// confirmation paragraphs; the scroll wrapper (below) provides the
	// vertical elasticity.
	view->SetExplicitMinSize(BSize(480, 100));
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	return view;
}


/*! Wrap a static text view in a scroll view. BTextView reports
	height-for-width, so placed bare in a group it stays content-sized
	and the group pads the leftover space with ugly gaps (the
	too-tall-window / floating-text-box look). A BScrollView has no
	such constraint: it expands to fill whatever the page has, the text
	scrolls if it ever overflows, and the page reads as one solid
	panel. Same pattern the progress page has always used. */
static BScrollView*
make_scrolled(BTextView* view, const char* name)
{
	return new BScrollView(name, view, 0, false, true, B_FANCY_BORDER);
}


// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------
MainWindow::MainWindow()
	:
	BWindow(BRect(50, 50, 640, 460),
		B_TRANSLATE_SYSTEM_NAME("UEFI Wizard"),
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE),
	fCardLayout(NULL),
	fCurrentPage(PAGE_WELCOME),
	fBackButton(NULL),
	fNextButton(NULL),
	fCancelButton(NULL),
	fDiskPicker(NULL),
	fStatusText(NULL),
	fConfirmText(NULL),
	fProgressLog(NULL),
	fDoneText(NULL),
	fSelectedDiskPath(),
	fInspection(),
	fWorker(NULL),
	fFinalDestination()
{
	for (int i = 0; i < PAGE_COUNT; i++)
		fPages[i] = NULL;

	_BuildLayout();
	_ShowPage(PAGE_WELCOME);
	CenterOnScreen();
}


MainWindow::~MainWindow()
{
	delete fWorker;
}


bool
MainWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPageNext:
			switch (fCurrentPage) {
				case PAGE_WELCOME:
					_ShowPage(PAGE_PICK_DISK);
					break;
				case PAGE_PICK_DISK:
					if (fSelectedDiskPath.IsEmpty()) {
						BAlert* alert = new BAlert("",
							B_TRANSLATE("Please select a disk first."),
							B_TRANSLATE("OK"), NULL, NULL,
							B_WIDTH_AS_USUAL, B_INFO_ALERT);
						alert->SetFlags(alert->Flags()
							| B_CLOSE_ON_ESCAPE);
						alert->Go();
						break;
					}
					_RunInspection();
					_ShowPage(PAGE_ESP_STATUS);
					break;
				case PAGE_ESP_STATUS:
					if (fInspection.state != ESP_STATE_VALID
						&& fInspection.state
							!= ESP_STATE_NONE_BUT_FREE_SPACE) {
						BAlert* alert = new BAlert("",
							B_TRANSLATE("This disk is not in a state we "
								"can fix automatically. See the diagnostic "
								"text for details."),
							B_TRANSLATE("OK"), NULL, NULL,
							B_WIDTH_AS_USUAL, B_STOP_ALERT);
						alert->SetFlags(alert->Flags()
							| B_CLOSE_ON_ESCAPE);
						alert->Go();
						break;
					}
					_PrepareConfirmPage();
					_ShowPage(PAGE_CONFIRM);
					break;
				case PAGE_CONFIRM:
					_ShowPage(PAGE_PROGRESS);
					_StartWorker();
					break;
				case PAGE_PROGRESS:
					// "Next" is disabled while the worker runs.
					break;
				case PAGE_DONE:
					Quit();
					break;
				default:
					break;
			}
			break;

		case kMsgPageBack:
			if (fCurrentPage == PAGE_PICK_DISK)
				_ShowPage(PAGE_WELCOME);
			else if (fCurrentPage == PAGE_ESP_STATUS)
				_ShowPage(PAGE_PICK_DISK);
			else if (fCurrentPage == PAGE_CONFIRM)
				_ShowPage(PAGE_ESP_STATUS);
			break;

		case kMsgCancel:
			Quit();
			break;

		case kMsgDiskSelected:
		{
			const char* path = NULL;
			if (message->FindString("disk_path", &path) == B_OK
				&& path != NULL) {
				_OnDiskSelected(path);
			}
			break;
		}

		case kMsgWorkerLog:
		{
			const char* level = "step";
			const char* text = "";
			message->FindString("level", &level);
			message->FindString("message", &text);
			_AppendLog(level, text);
			break;
		}

		case kMsgWorkerDone:
			_OnWorkerDone(message);
			break;

		case kMsgWorkerFailed:
			_OnWorkerFailed(message);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void
MainWindow::_BuildLayout()
{
	fPages[PAGE_WELCOME]    = _BuildWelcomePage();
	fPages[PAGE_PICK_DISK]  = _BuildDiskPickerPage();
	fPages[PAGE_ESP_STATUS] = _BuildStatusPage();
	fPages[PAGE_CONFIRM]    = _BuildConfirmPage();
	fPages[PAGE_PROGRESS]   = _BuildProgressPage();
	fPages[PAGE_DONE]       = _BuildDonePage();

	BView* cardHost = new BView("card_host", B_WILL_DRAW);
	fCardLayout = new BCardLayout();
	cardHost->SetLayout(fCardLayout);
	for (int i = 0; i < PAGE_COUNT; i++)
		fCardLayout->AddView(fPages[i]);

	fBackButton = new BButton("back",
		B_TRANSLATE("Back"), new BMessage(kMsgPageBack));
	fNextButton = new BButton("next",
		B_TRANSLATE("Next"), new BMessage(kMsgPageNext));
	fCancelButton = new BButton("cancel",
		B_TRANSLATE("Cancel"), new BMessage(kMsgCancel));

	BMenuBar* menuBar = new BMenuBar("menubar");
	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	BMenuItem* aboutItem = new BMenuItem(
		B_TRANSLATE("About UEFI Wizard" B_UTF8_ELLIPSIS),
		new BMessage(B_ABOUT_REQUESTED));
	fileMenu->AddItem(aboutItem);
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(fileMenu);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(cardHost)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING)
			.Add(fCancelButton)
			.AddGlue()
			.Add(fBackButton)
			.Add(fNextButton)
		.End()
	.End();

	SetDefaultButton(fNextButton);

	// The About item must reach the application, not this window
	aboutItem->SetTarget(be_app);
}


BView*
MainWindow::_BuildWelcomePage()
{
	BTextView* intro = make_static_textview("welcome_text");
	intro->SetText(
		"This tool finishes the EFI half of a Haiku install.\n\n"
		"On modern UEFI hardware, Haiku's Installer leaves the EFI System "
		"Partition (ESP) untouched and never copies the boot loader. The "
		"resulting install completes without error but won't boot.\n\n"
		"Pick a target disk on the next page. If a valid ESP is already "
		"there, the loader is just copied into place. If no ESP exists "
		"and there is at least 256 MB of unallocated space, this tool "
		"will offer to create one.\n\n"
		"Read every confirmation carefully — partition operations are "
		"destructive."
	);

	BView* page = new BView("welcome", B_WILL_DRAW);
	BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(new BStringView("welcome_heading",
			B_TRANSLATE("Welcome to UEFI Wizard")))
		.Add(make_scrolled(intro, "welcome_scroll"))
	.End();
	return page;
}


BView*
MainWindow::_BuildDiskPickerPage()
{
	fDiskPicker = new DiskPicker();
	return fDiskPicker;
}


BView*
MainWindow::_BuildStatusPage()
{
	fStatusText = make_static_textview("status_text");
	fStatusText->SetText("");

	BView* page = new BView("status", B_WILL_DRAW);
	BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(new BStringView("status_heading",
			B_TRANSLATE("Disk inspection")))
		.Add(make_scrolled(fStatusText, "status_scroll"))
	.End();
	return page;
}


BView*
MainWindow::_BuildConfirmPage()
{
	fConfirmText = make_static_textview("confirm_text");
	fConfirmText->SetText("");

	BView* page = new BView("confirm", B_WILL_DRAW);
	BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(new BStringView("confirm_heading",
			B_TRANSLATE("Confirm — last chance to back out")))
		.Add(make_scrolled(fConfirmText, "confirm_scroll"))
	.End();
	return page;
}


BView*
MainWindow::_BuildProgressPage()
{
	fProgressLog = new BTextView("progress_log",
		B_WILL_DRAW | B_FRAME_EVENTS);
	fProgressLog->MakeEditable(false);
	fProgressLog->MakeSelectable(true);
	fProgressLog->SetWordWrap(true);

	BScrollView* scroll = new BScrollView("progress_scroll",
		fProgressLog, 0, false, true, B_FANCY_BORDER);

	BView* page = new BView("progress", B_WILL_DRAW);
	BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(new BStringView("progress_heading",
			B_TRANSLATE("Working — please wait")))
		.Add(scroll)
	.End();
	return page;
}


BView*
MainWindow::_BuildDonePage()
{
	fDoneText = make_static_textview("done_text");
	fDoneText->SetText("");

	BView* page = new BView("done", B_WILL_DRAW);
	BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(new BStringView("done_heading",
			B_TRANSLATE("Done")))
		.Add(make_scrolled(fDoneText, "done_scroll"))
	.End();
	return page;
}


// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
void
MainWindow::_ShowPage(page_index page)
{
	fCurrentPage = page;
	fCardLayout->SetVisibleItem((int32)page);
	_UpdateNavButtons();
}


void
MainWindow::_UpdateNavButtons()
{
	bool canBack = false;
	bool canNext = true;
	bool canCancel = true;

	BString backLabel = B_TRANSLATE("Back");
	BString nextLabel = B_TRANSLATE("Next");

	switch (fCurrentPage) {
		case PAGE_WELCOME:
			canBack = false;
			break;
		case PAGE_PICK_DISK:
			canBack = true;
			break;
		case PAGE_ESP_STATUS:
			canBack = true;
			break;
		case PAGE_CONFIRM:
			canBack = true;
			nextLabel = B_TRANSLATE("Apply");
			break;
		case PAGE_PROGRESS:
			canBack = false;
			canNext = false;
			canCancel = false;
			break;
		case PAGE_DONE:
			canBack = false;
			canCancel = false;
			nextLabel = B_TRANSLATE("Quit");
			break;
		default:
			break;
	}

	fBackButton->SetEnabled(canBack);
	fNextButton->SetEnabled(canNext);
	fCancelButton->SetEnabled(canCancel);
	fBackButton->SetLabel(backLabel);
	fNextButton->SetLabel(nextLabel);
}


// ---------------------------------------------------------------------------
// State updates
// ---------------------------------------------------------------------------
void
MainWindow::_OnDiskSelected(const char* path)
{
	fSelectedDiskPath = path;
}


void
MainWindow::_RunInspection()
{
	ESPManager manager;
	NullProgressLogger nullLogger;

	manager.Inspect(fSelectedDiskPath.String(), kDefaultESPSize,
		fInspection, &nullLogger);

	BString text;
	text << B_TRANSLATE("Disk:") << " " << fSelectedDiskPath << "\n";
	text << B_TRANSLATE("Partitioning system:") << " "
		<< (fInspection.partitioningSystem.IsEmpty()
			? "(none)" : fInspection.partitioningSystem.String())
		<< "\n\n";

	switch (fInspection.state) {
		case ESP_STATE_VALID:
			text << B_TRANSLATE(
				"An EFI System Partition is already present.") << "\n\n";
			text << B_TRANSLATE("ESP partition id:") << " "
				<< fInspection.espPartitionID << "\n";
			text << B_TRANSLATE("ESP size:") << " "
				<< human_size(fInspection.espSizeBytes) << "\n\n";
			text << B_TRANSLATE("The next step will mount it (if needed) "
				"and copy haiku_loader.efi into /EFI/BOOT/.");
			break;

		case ESP_STATE_NONE_BUT_FREE_SPACE:
			text << B_TRANSLATE(
				"No ESP found, but there is enough unallocated space "
				"to create one.") << "\n\n";
			text << B_TRANSLATE("Largest free region:") << " "
				<< human_size(fInspection.freeContiguousBytes) << "\n";
			text << B_TRANSLATE("ESP size to allocate:") << " "
				<< human_size(kDefaultESPSize) << "\n\n";
			text << B_TRANSLATE(
				"The next step will create a FAT partition tagged as an "
				"EFI System Partition, format it, and copy "
				"haiku_loader.efi into /EFI/BOOT/.");
			break;

		case ESP_STATE_NONE_NO_FREE_SPACE:
			text << B_TRANSLATE(
				"No ESP, and not enough unallocated space to create "
				"one.") << "\n\n";
			text << fInspection.diagnostic << "\n\n";
			text << B_TRANSLATE(
				"Use DriveSetup to free at least 256 MB of contiguous "
				"space, then come back to this tool.");
			break;

		case ESP_STATE_NO_PARTITIONING_SYSTEM:
		case ESP_STATE_UNSUPPORTED_PARTITIONING:
			text << fInspection.diagnostic << "\n\n";
			text << B_TRANSLATE(
				"Initialize the disk with DriveSetup first (Intel or GPT "
				"partition map), then come back to this tool.");
			break;

		case ESP_STATE_INSPECTION_FAILED:
		default:
			text << B_TRANSLATE("Inspection failed:") << " "
				<< fInspection.diagnostic;
			break;
	}

	fStatusText->SetText(text.String());
}


void
MainWindow::_PrepareConfirmPage()
{
	BString text;
	text << B_TRANSLATE("Target disk:") << " " << fSelectedDiskPath
		<< "\n\n";

	if (fInspection.state == ESP_STATE_VALID) {
		text << B_TRANSLATE("Action: copy boot loader only.") << "\n\n";
		text << B_TRANSLATE(
			"haiku_loader.efi will be written to /EFI/BOOT/BOOTX64.EFI "
			"on the existing ESP. If a loader is already present it "
			"will be renamed to *_old.EFI before being overwritten.");
	} else {
		text << B_TRANSLATE("Action: create ESP and copy boot loader.")
			<< "\n\n";
		text << B_TRANSLATE("Steps:") << "\n";
		text << B_TRANSLATE(
			"  1. Allocate a ") << human_size(kDefaultESPSize)
			<< B_TRANSLATE(" FAT partition in the largest unallocated "
				"region of the target disk.") << "\n";
		text << B_TRANSLATE(
			"  2. Tag the new partition as an EFI System Partition "
			"(GPT type GUID C12A7328-...-93EC93B / MBR type 0xEF).")
			<< "\n";
		text << B_TRANSLATE(
			"  3. Format it as FAT.") << "\n";
		text << B_TRANSLATE(
			"  4. Mount it and copy haiku_loader.efi to "
			"/EFI/BOOT/BOOTX64.EFI plus /EFI/Haiku/.") << "\n\n";
		text << B_TRANSLATE(
			"Existing partitions on the disk are NOT touched.");
	}

	text << "\n\n" << B_TRANSLATE(
		"Click Apply to begin, or Back to reconsider.");

	fConfirmText->SetText(text.String());
}


// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------
void
MainWindow::_StartWorker()
{
	delete fWorker;
	fWorker = NULL;

	fProgressLog->SetText("");

	WorkPlan plan;
	plan.devicePath = fSelectedDiskPath;
	plan.espSizeBytes = kDefaultESPSize;

	if (fInspection.state == ESP_STATE_VALID) {
		plan.createPartition = false;
		plan.existingESPID = fInspection.espPartitionID;
	} else {
		plan.createPartition = true;
		plan.existingESPID = -1;
	}

	fWorker = new WorkerThread(plan, BMessenger(this));
	status_t status = fWorker->Start();
	if (status != B_OK) {
		BMessage failure(kMsgWorkerFailed);
		BString msg;
		msg.SetToFormat("Failed to spawn worker: %s", strerror(status));
		failure.AddString("message", msg.String());
		PostMessage(&failure);
	}
}


void
MainWindow::_AppendLog(const char* level, const char* message)
{
	const char* prefix = "==> ";
	if (strcmp(level, "detail") == 0)
		prefix = "    ";
	else if (strcmp(level, "warn") == 0)
		prefix = "!   ";
	else if (strcmp(level, "error") == 0)
		prefix = "X   ";

	BString line;
	line << prefix << message << "\n";
	fProgressLog->Insert(fProgressLog->TextLength(), line.String(),
		line.Length());
	fProgressLog->ScrollToOffset(fProgressLog->TextLength());
}


void
MainWindow::_OnWorkerDone(BMessage* message)
{
	const char* destination = "";
	int32 espID = -1;
	message->FindString("destination_path", &destination);
	message->FindInt32("esp_id", &espID);

	fFinalDestination = destination;

	BString text;
	text << B_TRANSLATE("UEFI loader installed successfully.") << "\n\n";
	text << B_TRANSLATE("Destination:") << " " << destination << "\n";
	text << B_TRANSLATE("ESP partition id:") << " " << espID << "\n\n";
	text << B_TRANSLATE(
		"Reboot and select the disk from your firmware boot menu. If "
		"the firmware doesn't list it, check the boot order in setup.");
	fDoneText->SetText(text.String());

	_ShowPage(PAGE_DONE);
}


void
MainWindow::_OnWorkerFailed(BMessage* message)
{
	const char* errorText = "(no detail)";
	message->FindString("message", &errorText);

	BString summary;
	summary << B_TRANSLATE("The operation failed.") << "\n\n";
	summary << errorText << "\n\n";
	summary << B_TRANSLATE(
		"Nothing further has been written. You can close this window "
		"and try again.");
	fDoneText->SetText(summary.String());

	_ShowPage(PAGE_DONE);
}
