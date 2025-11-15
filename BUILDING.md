# Building Cash-Sloth on Windows with MinGW

This project uses a few Win32 helper libraries that must be linked in explicitly when
compiling with MinGW.  Use the provided `Makefile` to compile everything into a single
executable:

```sh
mingw32-make
```

The build assumes that `x86_64-w64-mingw32-g++` is available in your PATH.  If your
compiler uses a different executable name, override `CXX` when invoking make:

```sh
mingw32-make CXX=g++
```

The resulting binary (`cash-sloth.exe`) will be created in the repository root.  Run it
from the same directory so the JSON catalogue and style files can be located.
