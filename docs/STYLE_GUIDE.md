> [!NOTE]
> An LLM was used to aid in development of this code.

# UEFI Wizard Coding Style Guide

UEFI Wizard is a native Haiku application (a wizard-style GUI plus a small
scriptable CLI backend) written in C++. Our style is the **Haiku Project
Coding Guidelines** verbatim — UEFI Wizard is a pure Haiku-API app with no
vendored third-party code, so there is nothing to bend the rules around.

**Authoritative base:** <https://www.haiku-os.org/development/coding-guidelines>

When this document and the Haiku guidelines disagree, **this document wins**
for UEFI Wizard code. When this document is silent, defer to Haiku.

When in doubt, look at how the surrounding code does it. Consistency with the
immediate context outranks consistency with the project as a whole — never
make a file "stick out" from its neighbours just to match a rule in this guide.

---

## 1. Project-specific notes

UEFI Wizard tracks upstream Haiku style with **no intentional deviations** on
formatting. The points below are the few project-specific conventions worth
calling out explicitly; everything else in this document is a restatement of
the Haiku rules for convenience.

### 1.1 Line length — 100-column limit

- **Hard cap: 80 columns**, matching upstream Haiku. The existing sources
  hold to this; keep new code there.
- A handful of lines (long `printf` format chains, `B_PRId32` macros in
  diagnostics) run slightly over when a wrap would hurt readability. Treat
  80 as the target and only exceed it when a break genuinely obscures the
  line — never as a habit.

### 1.2 Destructive-operation UX rule

UEFI Wizard modifies partition tables. **Every partition-modifying action
must sit behind an explicit confirmation step with plain-language wording**,
and the confirmation must state in plain English what will and will not be
touched.

- In the GUI this is the dedicated **Confirm** page (`PAGE_CONFIRM`), whose
  heading is literally "Confirm — last chance to back out" and whose body
  spells out each step plus the reassurance "Existing partitions on the disk
  are NOT touched."
- In the CLI, the destructive commands (`create-esp`, `delete`) are labelled
  `DESTRUCTIVE.` in the usage text, and `auto` refuses with a clear message
  rather than guessing when there is no safe option.

This is a product requirement, not just a style preference: a new code path
that can destroy data without a clear, dismissible confirmation is a bug.

---

## 2. Indentation and whitespace

- **Tabs** for indenting blocks. Editor tab width is **4** for purposes of
  computing line length and alignment.
- Wrapped lines get **at least one extra tab**, plus one more tab per
  expression nesting level.
- Namespace contents are **not indented** — they sit flush at column 0
  (see the `namespace ArchUtils { ... }` block in `ArchUtils.cpp`).
- **Spaces** on both sides of binary operators (`a + b`, `x == y`).
- **No space** between a C-style cast operator and its operand: `(off_t)mb`,
  `(double)bytes`, `(long long)freeSize`.
- **Always a space** after a comma.
- Every file ends with a newline.
- No trailing whitespace on any line.

## 3. Naming

| Kind | Convention | Example |
|---|---|---|
| Classes, structs, types, namespaces, functions | `UpperCamelCase` | `ESPManager`, `FindLoaderSourcePath` |
| Local variables | `lowerCamelCase` | `partitionName`, `freeOffset` |
| Member variables | `f` prefix + `UpperCamelCase` | `fMainWindow`, `fCardLayout`, `fSelectedDiskPath` |
| Constants | `k` prefix + `UpperCamelCase` | `kAppName`, `kDefaultESPSize`, `kMsgPageNext` |
| Globals | `g` prefix | `gApp` (none currently in the tree) |
| Private methods | `_` prefix | `_BuildLayout`, `_RunInspection`, `_CopyFileWithBackup` |

Rules:

- No underscores in type or function names (other than the `_` prefix on
  private methods).
- **Descriptive names always beat short ones.** No abbreviations, no
  letter-soup names, even for "obvious" things. Spell it out.
  - Variables: `message` not `msg`, `partition` not `p`, `destination` not
    `dst`, `index` not `idx`.
  - File and class names: `LoaderInstaller` not `LdrInst`; `DiskPicker` not
    `DPicker`; `WorkerThread` not `WThread`.
  - Method names: `FindLoaderSourcePath()` not `FindLdrPath()`.
- Exception: a few well-known abbreviations are fine when their full form
  would be noise — `id`, `url`, `efi`, `esp`, `gpt`, `mbr`, `fat`,
  `min`/`max`, `i`/`j`/`k` for tight loop indices. `ESP` and `EFI` appear as
  domain terms throughout and are kept uppercase (`ESPManager`,
  `kEFISystemPartitionGUID`).
- No articles in names — prefer `message`, `view`, `Draw`, not `aMessage`,
  `theView`, `MyDraw`.
- All identifiers, comments, and strings in **US English**.

### 3.1 The `k`-prefix constant rule and the legacy enum exception

New named constants take the `k` prefix: `kAppSignature`, `kDefaultESPSize`,
`kEFIPartitionType`, and the `kMsg*` message-code enum in `Constants.h`.

The disk-inspection enumerators in `ESPManager.h` — `ESP_STATE_VALID`,
`ESP_STATE_NONE_BUT_FREE_SPACE`, and the rest — use SCREAMING_SNAKE_CASE
instead, and their enclosing type is `enum esp_state` (lower-snake, like a
POSIX type). This mirrors Haiku's own disk-device enums
(`B_DISK_SYSTEM_*`, `partition_id`) and is the established convention for
this state machine. Keep the existing members consistent; **new** general
constants still take `k`.

## 4. Braces and blocks

- **Class / struct** opening brace: same line as the declaration.
- **Function** opening brace: on its own line, flush left.
- **`if` / `else` / `for` / `while` / `switch`** opening brace: same line as
  the keyword and condition.
- `else` and `else if` go on a new line, after the closing brace of the
  previous block.
- **Single-statement** `if`/`else`/`for`/`while`: omit the braces, put the
  statement on a new indented line.
- **Multi-statement** blocks: always braces.
- Empty inline methods defined inside a class definition may sit on a single
  line (e.g. `status_t InitCheck() const { return fStatus; }` in
  `ESPManager.cpp`'s `ModificationSession`). Empty functions defined outside
  the class follow the standard function format.
- After an early `return` (or `break`/`continue`) inside an `if`, do **not**
  write an `else`.

```cpp
status_t
ESPManager::Mount(partition_id partitionID, BPath& outMountPath,
	IProgressLogger* logger)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition = NULL;

	status_t status = roster.GetPartitionWithID(partitionID, &device,
		&partition);
	if (status != B_OK) {
		LogErrorF(logger,
			"No partition with id %" B_PRId32 ".", partitionID);
		return status;
	}

	if (!partition->IsMounted()) {
		LogStepF(logger, "Mounting partition");
		status = partition->Mount();
		if (status < B_OK) {
			LogErrorF(logger, "Mount failed: %s", strerror(status));
			return status;
		}
	}

	return B_OK;
}
```

## 5. Functions

- Return type on its own line, **above** the function name.
- Opening brace on its own line, flush left.
- **Two blank lines** between function definitions.
- Long argument lists: wrap and indent the continuation by **one tab**.

```cpp
status_t
LoaderInstaller::Install(const BPath& mountedESP, const BPath& loaderSource,
	BPath& outDestination, IProgressLogger* logger);
```

## 6. Constructor initializer lists

- Colon on its **own line**, indented one tab.
- Each initializer on its own line, indented one tab, in declaration order.
- Prefer initializer lists over assigning in the body.

```cpp
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
	fNextButton(NULL)
{
}
```

## 7. Blank lines

- **Two blank lines** between functions.
- **Two blank lines** between the `#include` block and the first
  declaration, and between a translation-context `#define` block and the
  first declaration (see `MainWindow.cpp`).
- **One blank line** between cases in a `switch` when the cases have bodies
  of more than one line.
- **One blank line** after the opening `#define` of a header guard.
- **Two blank lines** before the closing `#endif` of a header guard.
- No blank line between the license/copyright block and the header guard.

## 8. Control flow specifics

### 8.1 If / else

- Always use explicit boolean tests, never implicit truthiness.
  - Pointers: `if (matched == NULL)`, not `if (!matched)`.
  - Integers / ids: `if (existing >= 0)`, `if (count != 0)`.
- Bitmasks go in parentheses with an explicit comparison:
  `if ((flags & kMask) != 0)`.
- No assignment inside an `if` condition. Split it:
  ```cpp
  status_t status = entry.GetRef(&ref);
  if (status != B_OK)
      return status;
  ```
  The one tolerated exception in the current tree is the row-drain loop in
  `DiskPicker::Reload` (`while (BRow* row = fDiskList->RowAt(0, NULL))`),
  which is a well-known Haiku list-teardown idiom. Do not introduce new
  assignment-in-condition outside that pattern.
- Variable goes on the **left** of comparisons: `if (status == B_OK)`,
  never `if (B_OK == status)`. No Yoda conditions.
- Do not wrap an entire `if` condition in redundant outer parentheses, and
  do not parenthesise each clause.

### 8.2 Long conditions

When wrapping a long boolean expression, put the **logical operator at the
start** of the next line:

```cpp
if (fInspection.state != ESP_STATE_VALID
	&& fInspection.state != ESP_STATE_NONE_BUT_FREE_SPACE) {
	// ...
}
```

### 8.3 Switch

- `case` labels are indented one tab inside the `switch`.
- The body of each case is indented one further tab.
- One blank line between multi-line cases; consecutive fall-through labels
  may stack with no blank line (see the grouped `ESP_STATE_*` failure cases
  in `UEFIWizardCLI.cpp::cmd_auto`).
- Wrap a case body in `{ }` whenever it declares its own variables (see the
  `kMsgDiskSelected` case in `MainWindow::MessageReceived`).
- Always have a `default:` (even if it just `break;`s). UEFI Wizard's
  state-machine switches list every `ESP_STATE_*` value explicitly **and**
  carry a `default:` so a future enum value can't silently fall through.

```cpp
switch (result.state) {
	case ESP_STATE_VALID:
		espID = result.espPartitionID;
		break;

	case ESP_STATE_NONE_BUT_FREE_SPACE:
		status = manager.CreateESP(devicePath, bytes, espID, &logger);
		if (status != B_OK)
			return 2;
		break;

	case ESP_STATE_NONE_NO_FREE_SPACE:
	case ESP_STATE_NO_PARTITIONING_SYSTEM:
	case ESP_STATE_UNSUPPORTED_PARTITIONING:
	case ESP_STATE_INSPECTION_FAILED:
	default:
		fprintf(stderr, "Cannot proceed: %s\n", result.diagnostic.String());
		return 2;
}
```

### 8.4 Loops

- Prefer `for` over `while`-with-assignment (with the documented list-drain
  exception in §8.1).
- Plain indexed `for` over `BDiskDevice::CountChildren()` /
  `CountPartitionableSpaces()` is the norm for walking disk-device children.

### 8.5 No `goto`. No exceptions for cleanup either — use RAII.

The `ModificationSession` class in `ESPManager.cpp` is the project's model
RAII type: its destructor calls `CancelModifications()` unless `Commit()`
already ran, so every early `return` out of a partition operation unwinds the
disk-device session cleanly without a `goto cleanup:` ladder.

## 9. Types

### 9.1 Prefer Haiku types over raw C types

- `int32` / `uint32` instead of `int` / `unsigned`.
- `int64` / `uint64` for explicit 64-bit.
- `off_t` for partition sizes and offsets (`kDefaultESPSize`,
  `espSizeBytes`, `freeContiguousOffset`).
- `partition_id` for partition identifiers (it is a signed id; `-1` means
  "none", which is why the inspection result initialises ids to `-1`).
- `size_t` / `ssize_t` for sizes.
- `status_t` for error returns. **Every UEFI Wizard function that can fail
  returns `status_t`**, with `B_OK` on success. `main()` in both the app and
  the CLI returns `int` (process exit code) — that is the one place a raw
  `int` return is correct.

These come from `<SupportDefs.h>` (and `<DiskDeviceDefs.h>` for
`partition_id`).

### 9.2 Strings

- `BString` over `char*`, `malloc`/`strdup`/`free`, or fixed `char[N]`
  buffers.
- Use `BString::operator<<`, `BString::SetToFormat`, and
  `BString::SetToFormatVarArgs` instead of `sprintf`. The `LogStepF` /
  `LogDetailF` / `LogErrorF` helpers in `ESPManager.cpp` are the canonical
  pattern: take a printf-style format, cook it into a `BString` via
  `SetToFormatVarArgs`, then hand the finished string to the logger.
- `const char*` is fine for read-only interface parameters
  (`IProgressLogger::Step(const char* message)`), string literals, and
  `argv`.

### 9.3 Collections

- `BObjectList<T>` over `BList` when a Haiku container fits.
- `std::vector<T>` is acceptable at the disk-device API boundary where it is
  the simplest fit — e.g. the `std::vector<partition_id> existingIDs`
  snapshot in `ESPManager::CreateESP` used to spot the newly-created child.
  Keep such use local; do not let STL containers become the project's
  general-purpose collection.

### 9.4 Casts

- Use C++ casts: `static_cast`, `dynamic_cast`, `const_cast`,
  `reinterpret_cast`.
- `dynamic_cast` for down-casts whose runtime type is not statically
  guaranteed — see `dynamic_cast<DiskRow*>(row)` in
  `DiskPicker::MessageReceived`, which then null-checks the result.
- C-style casts are only acceptable for primitive numeric conversions and
  must have **no whitespace** after the operator: `(double)bytes`,
  `(long long)freeSize`, `(off_t)mb`, `(int32)page`.

## 10. Pointers and null

- `NULL`, not `0` or `nullptr`. (Haiku tradition.)
- Initialize pointers with traditional assignment, not constructor syntax:
  `BPartition* matched = NULL;`, not `BPartition* matched(NULL);`.
- **Pointer asterisk binds to the type**: `MainWindow* fMainWindow;`, not
  `MainWindow *fMainWindow;`. Matches `clang-format`'s `PointerAlignment:
  Left`.
- Do **not** check for `NULL` before `delete` — `delete` accepts `NULL` and
  the check is noise:
  ```cpp
  delete fWorker;   // not: if (fWorker != NULL) delete fWorker;
  ```

## 11. Boolean conventions

- Use `true` / `false`, never `TRUE` / `FALSE` macros.
- Functions that report success/failure return `status_t` (`B_OK` on
  success), not `bool`. A `bool` means a genuine yes/no flag — e.g.
  `WorkPlan::createPartition`, or the `canBack` / `canNext` /
  `canCancel` navigation flags in `MainWindow::_UpdateNavButtons`.

## 12. Returns and parentheses

- Do not parenthesise the return expression: `return status;`, not
  `return (status);`.
- Prefer early returns. Keep the happy path at one indent level — the
  `ESPManager` methods are written as a sequence of "do a step; if it failed,
  log and return the status" blocks rather than nested `if` pyramids.

## 13. Comments

- Prefer `//` over `/* */` for in-body comments. The block form is reserved
  for the file copyright header and for the larger explanatory headers above
  a class or a tricky API sequence.
- Explain **why**, not what. The valuable comments in this tree document
  hard-won disk-device API facts — for example the `ESPManager.cpp` file
  header listing the five PrepareModifications / CreateChild / DeleteChild
  traps from `docs/phase2-findings.md`, or the note in `App.cpp` explaining
  why the About-box version is read from the `app_version` resource instead
  of being hardcoded. Preserve that kind of comment.
- No author initials in comments. Git already knows.
- No `// TODO: kevin` style markers. Plain `// TODO:` is fine.
- No `#if 0`'d dead code. Delete it; git has the history.
- **Doxygen** (`/*! ... */`) for documenting public/header API surface — see
  the `make_scrolled` helper's `/*! ... */` block in `MainWindow.cpp`. Used
  for code comprehension; end-user documentation lives in `docs/`.

## 14. Includes

### 14.1 Ordering

Within a source file (`.cpp`), in this order, with **one blank line**
between groups:

1. The corresponding header (`#include "ESPManager.h"` from
   `ESPManager.cpp`).
2. POSIX / standard C headers (`<stdio.h>`, `<stdlib.h>`, `<string.h>`).
3. C++ standard headers (`<vector>`) — only when genuinely needed at an API
   boundary.
4. Haiku API headers (`<DiskDeviceRoster.h>`, `<LayoutBuilder.h>`, ...).
5. Haiku private headers (`<private/...>`) — only when unavoidable.
6. Local project headers (`"Constants.h"`, `"IProgressLogger.h"`).

Within each group, **alphabetize** include lines.

### 14.2 Style

- `<angle>` for system / framework headers.
- `"quoted"` for local project headers.
- Use **C-style header names**: `<string.h>`, `<stdlib.h>` — not
  `<cstring>`, `<cstdlib>`. (Haiku tradition.)
- No path components — `<Application.h>`, not `<be/app/Application.h>`.

## 15. Header files

### 15.1 Layout

```cpp
/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef UEFI_WIZARD_ARCH_UTILS_H
#define UEFI_WIZARD_ARCH_UTILS_H


#include <String.h>
#include <SupportDefs.h>


namespace ArchUtils {

const char*			DefaultBootFileName();
status_t			FindLoaderSourcePath(BString& outPath);

}	// namespace ArchUtils


#endif	// UEFI_WIZARD_ARCH_UTILS_H
```

### 15.2 Header-guard rules

- Header guards use the form `UEFI_WIZARD_<FILE>_H` (e.g.
  `UEFI_WIZARD_ESP_MANAGER_H`), matching the current project name. The whole
  tree was migrated to this form in one deliberate sweep; **match it** in any
  new header. The old `HAIKU_UEFI_SETUP_*` prefix (a holdover from the former
  name HaikuUEFISetup) is gone — do not reintroduce it.
- The guard immediately follows the copyright block — **no blank line
  between them**.
- **One blank line** after the `#define`.
- **Two blank lines** before the closing `#endif`.
- The closing `#endif` carries a `// GUARD_NAME` comment.

### 15.3 Member declaration alignment

- Members and methods inside a class are aligned in columns: the
  access-specifier-relative indent, the return-type column, then the name
  column (see `App.h`, `MainWindow.h`, `ESPManager.h`). This is the Haiku
  public-header style and the whole tree follows it — keep new declarations
  aligned the same way rather than left-packing them.
- Classes that must not be copied declare a private, undefined copy
  constructor and `operator=` (`ESPManager`, `LoaderInstaller`). Keep that
  idiom for any new resource-owning class.

## 16. Copyright headers and license

UEFI Wizard is **MIT-licensed** (see `LICENSE` at the repo root). Every
source and header file carries the standard Haiku two-line MIT header:

```cpp
/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>
 * Distributed under the terms of the MIT License.
 */
```

- The `.rdef` resource file uses the same header with an extra leading
  description line (`* UEFI Wizard resource definitions`); a file-purpose
  line above the copyright is fine for non-`.cpp`/`.h` files.
- A file with a substantial explanatory preamble (the `ESPManager.cpp` and
  `cli/UEFIWizardCLI.cpp` headers) keeps that prose **inside the same comment
  block**, after the two license lines.
- Update the year when you make a substantive change: `2026` for a new file,
  `2026-2027` if you meaningfully edit it next year. Trivial typo fixes do
  not bump the year.
- The package metadata must agree: `package/PackageInfo` declares
  `licenses { "MIT" }` and `copyrights { "2026 Kevin Adams" }`. Keep those in
  sync with the source headers.

## 17. Localization (B_TRANSLATE)

UEFI Wizard is a translatable Haiku app. All user-facing strings go through
the locale kit:

- Each translation unit that shows text `#undef`s then `#define`s a
  `B_TRANSLATION_CONTEXT` near the top (`"MainWindow"`, `"DiskPicker"`),
  immediately after the includes.
- Wrap user-visible literals in `B_TRANSLATE(...)`. Column headers, button
  labels, alert text, and the wizard-page prose are all translated.
- Use `B_TRANSLATE_SYSTEM_NAME("UEFI Wizard")` for the app name in the window
  title, and `B_UTF8_ELLIPSIS` (not `"..."`) inside menu labels such as
  `B_TRANSLATE("About UEFI Wizard" B_UTF8_ELLIPSIS)`.
- The CLI (`UEFIWizardCLI.cpp`) is intentionally **not** localized — its
  output is machine-parseable / log-style and stays in plain English.
- The build links `liblocalestub.a` so the binary runs without a catalog
  present; new GUI strings still belong in `B_TRANSLATE` so they can be
  extracted later.

## 18. The progress-logger interface pattern

Long-running disk work is decoupled from its presentation through the
`IProgressLogger` abstract interface (`IProgressLogger.h`):

```cpp
class IProgressLogger {
public:
	virtual						~IProgressLogger() {}
	virtual void				Step(const char* message) = 0;
	virtual void				Detail(const char* message) = 0;
	virtual void				Warn(const char* message) = 0;
	virtual void				Error(const char* message) = 0;
};
```

- `ESPManager` and `LoaderInstaller` know nothing about the UI — they report
  progress only through an `IProgressLogger*`.
- Three concrete sinks exist: `NullProgressLogger` (no-op, for paths that
  don't need reporting, defined alongside the interface), `ConsoleLogger`
  (prints to stdout/stderr, in the CLI), and `MessengerLogger` (posts
  `kMsgWorkerLog` BMessages to the wizard window, in `WorkerThread.cpp`).
- **Implementations must be safe to call from a worker thread.** The GUI sink
  satisfies this by posting BMessages rather than touching views directly.
- Messages are **fully cooked before they reach the logger** — callers format
  with a `BString` (or the `Log*F` helpers) first, so the interface itself
  takes a plain `const char*` with no printf surface.

New backend code that does user-visible work should report through this
interface rather than calling `printf` or touching the window directly.

## 19. Threading and the worker model

- All partition mutation runs on a background thread (`WorkerThread`),
  spawned with `spawn_thread` / `resume_thread`, never on the window thread.
- The worker talks back to the window exclusively through a `BMessenger` and
  the `kMsgWorker*` codes in `Constants.h` — never by touching the window's
  views from the worker thread.
- `WorkerThread`'s destructor calls `WaitForCompletion()` so the thread is
  always joined before the object dies.
- While the worker runs, the wizard disables Back / Next / Cancel
  (`PAGE_PROGRESS` in `_UpdateNavButtons`) so there is no way to navigate out
  of an in-flight operation.

## 20. Dead code, debug code, and printfs

- No `#if 0` blocks. Delete the code; git keeps history.
- The GUI binary does **not** log to stderr — it reports through
  `MessengerLogger`. Do not add ad-hoc `printf`/`fprintf` debugging to the
  GUI code.
- The CLI is the one place `printf`/`fprintf(stderr, ...)` is correct: it
  *is* the user interface. `ConsoleLogger` and the `cmd_*` functions print
  directly, and `fflush(stdout)` after each step keeps progress visible when
  output is piped.
- Prefer the RAII / early-return error style over defensive scaffolding.

## 21. Resource management

- Stack objects over heap objects whenever possible.
- Use RAII for any acquired resource. The `ModificationSession` wrapper
  around `PrepareModifications` / `Cancel` / `Commit` is the project's model
  here — never call `PrepareModifications` and the matching cancel/commit by
  hand across a function with early returns.
- For locks, use the Haiku `AutoLock` template — never hand `Lock()` /
  `Unlock()` pairs, and not `BAutolock`.
- Resource-owning classes that should not be copied declare private,
  undefined copy constructor and `operator=` (see §15.3).

## 22. Build and packaging

UEFI Wizard cross-builds from the Linux Haiku build server. The recipe lives
in `package/build-hpkg.sh`; understand these project-specific facts before
touching the build:

- **App name:** "UEFI Wizard". **GUI binary:** `UEFIWizard`. **CLI binary:**
  `uefi_wizard`. **Signature:** `application/x-vnd.UEFIWizard`. **Package:**
  `uefi_wizard`.
- Shared sources (`ArchUtils`, `Constants`, `ESPManager`, `LoaderInstaller`)
  are compiled once and linked into **both** binaries; the GUI adds `App`,
  `DiskPicker`, `MainWindow`, `WorkerThread`, the CLI adds
  `cli/UEFIWizardCLI.cpp`.
- **Resources require two steps, both mandatory.** First the host `rc`
  compiles `src/UEFIWizard.rdef` and `xres` attaches the resource section to
  the GUI binary. Then `resattr` *mirrors* those resources as file
  **attributes** on the staged binary. The app icon resolves through the
  **attribute** copy (`BAppFileInfo`), not the resource section — on a Linux
  host the attributes won't exist unless `resattr` writes them, so skipping
  the second step yields an icon-less app. Keep both steps.
- The `package/PackageInfo` version (`version` field) is the single source of
  the `.hpkg` filename; `build-hpkg.sh` parses it.

## 23. Icon workflow

The application icon is HVIF (Haiku's binary vector format). Full details are
in `docs/icon-design.md`; the short version:

- Design in SVG at a 64×64 viewBox (`src/UEFIWizard-icon.svg`) — HVIF's
  native coordinate space.
- Convert with hvif-tools `icon2icon`:
  `icon2icon src/UEFIWizard-icon.svg src/UEFIWizard.hvif`.
- Embed the resulting `.hvif` as an inline hex `resource vector_icon` block
  in `src/UEFIWizard.rdef` (the in-tree Installer convention).
- **Always verify** a regenerated icon by rendering it back to PNG at 16/32/64
  px before re-embedding — at 16×16 the drive + amber ESP slice + dark chip
  must stay distinguishable.

## 24. PR checklist

Before opening a PR, verify:

- [ ] Lines stay within 80 columns (target) — overruns only where a wrap
      genuinely hurts readability.
- [ ] Every partition-modifying path is behind a plain-language confirmation
      (GUI Confirm page / CLI `DESTRUCTIVE.` label).
- [ ] All new user-facing GUI strings go through `B_TRANSLATE`.
- [ ] Backend progress is reported via `IProgressLogger`, not stray
      `printf`/`fprintf` in GUI code.
- [ ] No `#if 0` blocks, no debug `printf` leftovers in the GUI.
- [ ] Every function that can fail returns `status_t`; `B_OK` on success.
- [ ] Disk-device sessions use `ModificationSession` (RAII), not hand-rolled
      cancel/commit.
- [ ] MIT copyright header present and correct; `PackageInfo` license /
      copyright in sync.
- [ ] File ends with a newline.

---

## Appendix A — Quick reference card

```
Indent: TAB (width 4)
Line:   80 columns (target/cap)
Brace:  class same line; function own line; if/for/while same line
Naming: UpperCamel types/funcs, lowerCamel vars, f/k/g prefixes, _ private
        ESP_STATE_* enum members keep SCREAMING_CASE (legacy state machine)
Pointer: BPartition* x = NULL;
Cast:   static_cast<T>(x); dynamic_cast for runtime downcasts; (T)x primitives
Null:   NULL, no nullptr; don't null-check before delete
Bool:   true/false, never TRUE/FALSE
Bitmask: if ((x & MASK) != 0)
Switch: case indented; { } if vars; list all enum values + default:
Strings: BString + SetToFormat/SetToFormatVarArgs, not sprintf
Errors: status_t, B_OK on success (main() returns int)
Logging: IProgressLogger -> Step/Detail/Warn/Error (cook BString first)
Threads: WorkerThread off the window thread; talk back via BMessenger
RAII:   ModificationSession around PrepareModifications/Commit
License: MIT, two-line Haiku header
i18n:   B_TRANSLATE in GUI; CLI stays English
Destructive ops: always behind an explicit confirmation
```

## Appendix B — Naming caveat

The project was renamed from **HaikuUEFISetup** to **UEFI Wizard** on
2026-06-05. The header guards have since been migrated in one deliberate
sweep to the `UEFI_WIZARD_*` prefix (see §15.2), so the old
`HAIKU_UEFI_SETUP_*` form no longer appears anywhere in the tree.

One artefact of the project's history is **intentionally retained**:

- The `enum esp_state` members use SCREAMING_SNAKE_CASE rather than the
  `k`-prefixed camel form (this mirrors Haiku's own disk-device enums — see
  §3.1).

This is not a license or correctness issue. Match the existing convention in
any file you touch; if it is ever modernised, do it as a single deliberate
sweep, not piecemeal.
