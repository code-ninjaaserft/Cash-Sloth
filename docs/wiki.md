# Cash-Sloth Wiki

This document can be used as a seed for a GitHub Wiki or shared internally to onboard
contributors and operators.

## Overview
- **Goal:** Touch-first Windows POS prototype with a JSON-driven catalogue and theming.
- **Tech stack:** Win32 + GDI for rendering, custom JSON parser, no external runtime
  dependencies beyond the Windows SDK and a C++17 compiler.

## Getting started
1. Clone or download the repository on Windows.
2. Install MinGW-w64 or Visual Studio (Desktop development with C++ workload).
3. Build via `mingw32-make` (MinGW) or the CMake generator of your choice. See
   [README.md](../README.md) for details.
4. Keep the `assets/` directory next to the produced executable so styles, catalogue, and
   imagery are located at runtime.

## Asset conventions
- **Catalogue:** `assets/cash_sloth_catalog.json` defines categories, articles, prices,
  and optional barcodes. The parser also accepts a `categories` array or a top-level
  object keyed by category name.
- **Style sheet:** `assets/style.json` drives palette colors, typography, spacing, quick
  payment amounts, and hero text.
- **Images:** `assets/cash_sloth_images.json` provides metadata for bundling artwork; it
  is optional but helps keep binary releases organised.

## Troubleshooting
- The app shows the default catalogue and style if assets are missing or invalid. Check
  the console output for parsing warnings (e.g., missing fields) when assets fail to load.
- Non-Windows systems will exit immediately because the UI is entirely Win32 based.
- If touch targets look too small, adjust `metrics` values in `assets/style.json` (e.g.,
  `quick_button_height`, `margin`, `tile_gap`).

## Roadmap ideas
- Add CI-friendly tests around JSON parsing to prevent regressions.
- Surface asset-loading errors directly in the UI banner, not just `stderr`.
- Provide a headless preview mode to render the layout to an image for documentation.
