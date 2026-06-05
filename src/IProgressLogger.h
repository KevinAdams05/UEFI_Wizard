/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_PROGRESS_LOGGER_H
#define UEFI_WIZARD_PROGRESS_LOGGER_H


#include <SupportDefs.h>


// Pluggable progress sink. ESPManager and LoaderInstaller report what
// they're doing through this interface so the CLI can print to stdout and
// the GUI can post BMessages back to the wizard window. Implementations
// must be safe to call from a worker thread.
class IProgressLogger {
public:
	virtual						~IProgressLogger() {}

	// A new high-level step has begun (e.g. "Creating partition").
	// Implementations typically advance a progress page or print a
	// "==> ..." line.
	virtual void				Step(const char* message) = 0;

	// Auxiliary detail about the step in flight. Free-form, user-visible.
	virtual void				Detail(const char* message) = 0;

	// A non-fatal warning — work continues but the user should see it.
	virtual void				Warn(const char* message) = 0;

	// A failure. Printf-style is intentionally not provided here; callers
	// format with a BString first so the message is fully cooked when the
	// logger sees it.
	virtual void				Error(const char* message) = 0;
};


// Drop-in no-op logger for code paths that don't need progress reporting.
class NullProgressLogger : public IProgressLogger {
public:
	virtual void				Step(const char* /*message*/) {}
	virtual void				Detail(const char* /*message*/) {}
	virtual void				Warn(const char* /*message*/) {}
	virtual void				Error(const char* /*message*/) {}
};


#endif	// UEFI_WIZARD_PROGRESS_LOGGER_H
