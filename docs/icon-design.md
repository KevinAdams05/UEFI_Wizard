>[!NOTE]
>An LLM was used to aid in development of this code.

# UEFI Wizard — Icon Design

## Concept

The icon tells the app's story in three shapes:

1. **UEFI firmware chip** (top right) — a dark IC package with pins and
   a green power symbol on the die: the machine's firmware, the thing
   that needs somewhere to boot from.
2. **Boot arrow** (orange) — sweeping from the chip down to…
3. **The ESP** — the small amber slice at the front of an otherwise
   ordinary partitioned drive. Amber marks it as *the* partition this
   tool creates; the larger gray slice is the existing Haiku volume.
   A small green activity LED nods at the drive being live.

No text (HVIF renders no glyphs, and nothing would survive 16×16
anyway) — the chip + arrow + highlighted-slice grammar carries it.

## Palette (8 colors)

| Use | Color |
|---|---|
| Chip body / outline | `#46546a` / `#2c3644` |
| Chip die | `#5a6c88` |
| Pins, main partition | `#8e9cb4` |
| Drive body | `#cfd8e6` (outline `#5a6880`) |
| ESP slice | `#f5b821` (outline `#a06900`) |
| Boot arrow | `#f08000` |
| Power symbol, LED | `#3fc73f` |

## Source of truth and regeneration

- Design source: [`../src/UEFIWizard-icon.svg`](../src/UEFIWizard-icon.svg)
  (64×64 viewBox — HVIF's native coordinate space)
- Converted with [hvif-tools](https://github.com/threedeyes/hvif-tools)
  (`icon2icon`, cloned + built at `~/Code/Haiku/hvif-tools/build/`):

  ```sh
  icon2icon src/UEFIWizard-icon.svg src/UEFIWizard.hvif
  ```

- Result: 1943-byte HVIF (10 styles / 26 paths), embedded in
  `src/UEFIWizard.rdef` as an inline `resource vector_icon` hex
  block (the in-tree Installer convention).
- **Always verify a regenerated icon** by rendering the .hvif back to
  PNG at Tracker's sizes before re-embedding:

  ```sh
  icon2icon src/UEFIWizard.hvif check.png --width 16 --height 16
  ```

  At 16×16 the legibility bar is: drive + amber slice + dark chip blob
  still distinguishable.

Icon-O-Matic on Haiku can open the .hvif directly for LOD-hint tuning
if small-size rendering ever needs hand adjustment.
