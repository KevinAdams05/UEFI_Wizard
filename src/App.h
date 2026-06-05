/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_APP_H
#define UEFI_WIZARD_APP_H


#include <Application.h>


class MainWindow;


class App : public BApplication {
public:
								App();
	virtual						~App();

	virtual void				ReadyToRun();
	virtual void				AboutRequested();

private:
			MainWindow*			fMainWindow;
};


#endif	// UEFI_WIZARD_APP_H
