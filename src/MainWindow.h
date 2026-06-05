/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_MAIN_WINDOW_H
#define UEFI_WIZARD_MAIN_WINDOW_H


#include <CardLayout.h>
#include <String.h>
#include <Window.h>

#include "ESPManager.h"


class BButton;
class BStringView;
class BTextView;
class BView;
class DiskPicker;
class WorkerThread;


// Six-page wizard: Welcome, Pick disk, ESP status, Confirm, Progress, Done.
// Pages are sibling BViews swapped via a BCardLayout on the content area.
// A footer bar with Back / Next / Cancel is owned by the window so pages
// don't reimplement navigation.
class MainWindow : public BWindow {
public:
								MainWindow();
	virtual						~MainWindow();

	virtual bool				QuitRequested();
	virtual void				MessageReceived(BMessage* message);

private:
			enum page_index {
				PAGE_WELCOME = 0,
				PAGE_PICK_DISK,
				PAGE_ESP_STATUS,
				PAGE_CONFIRM,
				PAGE_PROGRESS,
				PAGE_DONE,
				PAGE_COUNT
			};

			void				_BuildLayout();
			BView*				_BuildWelcomePage();
			BView*				_BuildDiskPickerPage();
			BView*				_BuildStatusPage();
			BView*				_BuildConfirmPage();
			BView*				_BuildProgressPage();
			BView*				_BuildDonePage();

			void				_ShowPage(page_index page);
			void				_UpdateNavButtons();

			void				_OnDiskSelected(const char* path);
			void				_RunInspection();
			void				_PrepareConfirmPage();
			void				_StartWorker();
			void				_AppendLog(const char* level,
									const char* message);
			void				_OnWorkerDone(BMessage* message);
			void				_OnWorkerFailed(BMessage* message);

			BCardLayout*		fCardLayout;
			BView*				fPages[PAGE_COUNT];
			page_index			fCurrentPage;

			BButton*			fBackButton;
			BButton*			fNextButton;
			BButton*			fCancelButton;

			DiskPicker*			fDiskPicker;

			BTextView*			fStatusText;
			BTextView*			fConfirmText;
			BTextView*			fProgressLog;
			BTextView*			fDoneText;

			BString				fSelectedDiskPath;
			ESPInspection		fInspection;
			WorkerThread*		fWorker;
			BString				fFinalDestination;
};


#endif	// UEFI_WIZARD_MAIN_WINDOW_H
