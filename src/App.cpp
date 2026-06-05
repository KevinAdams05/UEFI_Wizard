/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */


#include "App.h"

#include <AboutWindow.h>
#include <AppFileInfo.h>
#include <File.h>
#include <Roster.h>
#include <String.h>

#include "Constants.h"
#include "MainWindow.h"


App::App()
	:
	BApplication(kAppSignature),
	fMainWindow(NULL)
{
}


App::~App()
{
}


void
App::ReadyToRun()
{
	fMainWindow = new MainWindow();
	fMainWindow->Show();
}


void
App::AboutRequested()
{
	BAboutWindow* about = new BAboutWindow(kAppName, kAppSignature);
	// Pull the version from our own app_version resource (set in
	// UEFIWizard.rdef) so the About box can never drift from the
	// actual release version — the hardcoded string here had already
	// fallen two releases behind when this was fixed.
	BString versionString;
	app_info appInfo;
	if (be_app->GetAppInfo(&appInfo) == B_OK) {
		BFile file(&appInfo.ref, B_READ_ONLY);
		BAppFileInfo fileInfo(&file);
		version_info version;
		if (fileInfo.GetVersionInfo(&version, B_APP_VERSION_KIND)
				== B_OK) {
			versionString.SetToFormat("%" B_PRIu32 ".%" B_PRIu32
				".%" B_PRIu32, version.major, version.middle,
				version.minor);
		}
	}
	if (!versionString.IsEmpty())
		about->SetVersion(versionString.String());

	const char* authors[] = {
		"Kevin Adams",
		NULL
	};
	about->AddAuthors(authors);
	about->AddCopyright(2026, "Kevin Adams");
	about->AddDescription(
		"Finishes the EFI half of a Haiku install. Detects or creates an "
		"EFI System Partition on the target disk and copies haiku_loader.efi "
		"into place so the firmware can boot Haiku."
		"\n\n"
		"An LLM was used to aid in development of this code.");

	about->Show();
}


int
main(int /*argc*/, char** /*argv*/)
{
	App app;
	app.Run();
	return 0;
}
