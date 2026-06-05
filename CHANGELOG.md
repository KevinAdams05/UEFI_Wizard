>[!NOTE]
>An LLM was used to aid in development of this code.

# Changelog

All notable changes to UEFI Wizard will be recorded in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.1.0] – 2026-06-05 — initial import

State of the project at the initial git import, as the first beta.
Development builds 0.0.1–0.0.7 predate version control; the
highlights that got here:

- **Phases 1–3** (April–May): disk-device API spike (`spike/`,
  findings in `docs/phase2-findings.md`), `ESPManager` /
  `LoaderInstaller` core with CLI, then the six-page GUI wizard
  (BCardLayout + worker thread + `IProgressLogger`).
- **0.0.4–0.0.5**: application icon (SVG → HVIF via hvif-tools,
  embedded as inline rdef hex); fixed the cross-build resource
  pipeline (`xres` was missing — all earlier builds shipped without
  resources); File menu with About; scroll-wrapped wizard pages and a
  compact window.
- **0.0.6**: About version read dynamically from the `app_version`
  resource; resources mirrored as file attributes (`resattr`) so the
  icon actually shows in Tracker/Deskbar/About.
- **0.0.7**: renamed **HaikuUEFISetup → UEFI Wizard** (binary
  `UEFIWizard`, signature `application/x-vnd.UEFIWizard`, package
  `uefi_wizard`); project style guide added and a full conformance
  pass applied; README made release-ready; legacy Windows build
  scripts removed.
- **0.1.0**: version bumped to beta for the initial import and first
  release.
