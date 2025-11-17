# Cash-Sloth

Cash-Sloth is a Windows point-of-sale (POS) prototype with a touch-first layout. The
project bundles the full Win32 GUI implementation together with the JSON-driven style
and product catalogue definitions that power the interface.

## Features

- Full-screen, touch-friendly Win32 UI with animated hero/header area, cart pane, and
  quick-action buttons.
- JSON-configurable catalogue and styling so deployments can reskin or reprice items
  without recompiling.
- Built-in default catalogue and style definitions to keep the app usable even when
  external assets are missing.
- Simple Win32 message-pump application with double-buffered painting to keep redraws
  smooth on lower-powered kiosks.

## Repository layout

```
CashSloth/
├── src/                   # Win32 implementation files
├── include/               # Public headers shared across translation units
├── assets/                # JSON configuration for catalogue, styles, and imagery
├── Makefile               # MinGW build script targeting a Windows executable
├── README.md              # Project overview and usage notes
├── LICENSE                # Project licence information
└── lauch.exe              # Placeholder for the packaged Windows binary
```

The `assets` folder currently ships three JSON documents:

- `style.json` – visual styling, typography, and spacing tokens loaded at runtime.
- `cash_sloth_catalog.json` – the default product catalogue with categories and items.
- `cash_sloth_images.json` – metadata describing artwork bundled with release builds.

### Prerequisites

- Windows 10/11 with a touch display (or mouse/keyboard for development).
- [MinGW-w64](https://www.mingw-w64.org/) or Visual Studio with the Desktop
  development with C++ workload.
- Git if you plan to clone/update the repository.

## Building

The project targets Windows and can be built with the MinGW-w64 toolchain. The provided
`Makefile` links against the Win32, GDI, and common controls libraries required by the
GUI.

```
mingw32-make
```

The build assumes `x86_64-w64-mingw32-g++` is available on the `PATH`. Override the
compiler if necessary:

```
mingw32-make CXX=g++
```

The resulting binary (`cash-sloth.exe`) is written to the repository root. Run it from
there so the executable can resolve the JSON assets located in the `assets/` directory.

If you are iterating on the JSON catalogue or styles, keep the `assets/` folder next to
the executable. On launch the app searches for multiple filenames (see
`CashSlothGUI::loadCatalogue` and `StyleSheet::load`) and falls back to baked-in
defaults when nothing is found.

### Visual Studio / CMake build

If you prefer CMake or Visual Studio, generate a build directory and let CMake copy the
runtime assets next to the produced executable automatically:

```
cmake -S . -B build
cmake --build build --config Release
```

When the build finishes, the freshly built `cash-sloth.exe` lives under
`build/Release/` (or the configuration-specific output directory selected by your
generator) together with an `assets/` folder so you can launch the program immediately.

## Development tips

- The Win32 message loop lives in `CashSlothGUI::run`, and UI state is refreshed via
  helper methods such as `refreshCart` and `calculateLayout` in `src/main.cpp`.
- Style tokens (colors, typography, spacing, and quick-amount buttons) are parsed in
  `src/cash_sloth_style.cpp`. Modify `assets/style.json` to experiment without
  recompiling.
- Catalogue parsing and barcode lookup live in `Catalogue` within `src/main.cpp`. The
  JSON layout accepts either an array of categories or a keyed object; see the default
  `assets/cash_sloth_catalog.json` for examples.
- The utility helpers for currency formatting, UTF-8/UTF-16 conversion, and amount
  parsing reside in `include/cash_sloth_utils.h`.

## Runtime assets

When distributing the application, place `cash-sloth.exe`, `lauch.exe`, and the `assets`
folder side by side. The executable first looks for the new `assets` directory, but it
also falls back to legacy filenames for backwards compatibility.
