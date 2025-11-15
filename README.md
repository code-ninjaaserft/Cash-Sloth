# Cash-Sloth

Cash-Sloth is a Windows point-of-sale (POS) prototype with a touch-first layout. The
project bundles the full Win32 GUI implementation together with the JSON-driven style
and product catalogue definitions that power the interface.

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

## Runtime assets

When distributing the application, place `cash-sloth.exe`, `lauch.exe`, and the `assets`
folder side by side. The executable first looks for the new `assets` directory, but it
also falls back to legacy filenames for backwards compatibility.
