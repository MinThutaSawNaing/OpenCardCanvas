# Building OpenCardCanvas

## 1. Prerequisites

| Component | Version used | Notes |
|-----------|--------------|-------|
| Windows | 10 (1809+) / 11 x64 | Windows SDK 10.0.26100 or newer |
| Visual Studio 2022 | 17.x, MSVC v143 | Workload: *Desktop development with C++*, plus the latest Windows SDK |
| Qt | 6.8.3 LTS, `msvc2022_64` | `qtbase`, `qtsvg`, `qttools`, `qtimageformats` (TIFF), `qtsqldrivers` |
| CMake | 3.21+ (4.4.4 used) | On `PATH` |
| Ninja | 1.11+ | Optional but much faster than MSBuild |
| Inno Setup | 6.2+ | Only for building the installer |

### Installing Qt without the GUI installer

`aqtinstall` is the reproducible way to get exactly the modules this project needs:

```powershell
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 `
        -m qtimageformats -O C:\Qt
```

That produces `C:\Qt\6.8.3\msvc2022_64`. `qtimageformats` is what adds TIFF/TGA/WebP
image support; without it TIFF images cannot be imported.

## 2. Configure

### Recommended: build and run from PowerShell

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Tests -Run
```

The helper finds Visual Studio / Build Tools through `vswhere` and uses CMake
and Ninja from `PATH` or the Visual Studio CMake tools component. It discovers
Qt MSVC x64 kits under `C:\Qt` and `D:\Qt`; use `-QtPrefix "C:\path\to\kit"`
or `QTDIR` for a custom location. No developer prompt is required. Output is
streamed to the console and saved in `build\build.log`.

`-Portable` produces `build\dist` without launching it. `-Run` also deploys
the Qt runtime, then starts `build\dist\OpenCardCanvas.exe` after a successful
build (and successful tests when `-Tests` is supplied).

The project finds Qt through, in order of precedence:

1. `-DOCC_QT_PREFIX=<path>`
2. the `QTDIR` environment variable
3. `CMAKE_PREFIX_PATH`

```bat
cmake -S . -B build -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 ^
      -DOCC_QT_PREFIX=C:/Qt/6.8.3/msvc2022_64
```

Useful options:

| Option | Default | Meaning |
|--------|---------|---------|
| `OCC_BUILD_TESTS` | `ON` | Build the unit tests |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `RelWithDebInfo`, `Release` |

A Visual Studio solution can be produced instead of Ninja:

```bat
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
```

## 3. Build

```bat
cmake --build build                  :: everything
cmake --build build --target occcore :: the engine library only (fast iteration)
cmake --build build --target portable:: add the Qt runtime next to the .exe
```

Outputs:

```
build/bin/OpenCardCanvas.exe         the application
build/lib/occcore.lib                the engine library
build/bin/tests/                     the test executables
```

### Building from a Visual Studio developer prompt

If you build from a normal shell, CMake uses the MSVC toolchain it detects. If
you prefer an explicit environment, run
`VC\Auxiliary\Build\vcvars64.bat` first — that is what `tools/build.ps1` does.

## 4. Test

```bat
ctest --test-dir build --output-on-failure
```

Tests needing golden data read `OCC_QR_GOLDEN` (set automatically by
`tests/CMakeLists.txt`). Regenerate the QR golden file with:

```bat
python tools\qr\generate_golden.py
```

The tests run headless through `QT_QPA_PLATFORM=offscreen`, so they work on a
build agent with no desktop session.

## 5. Package

```bat
cmake --build build --target portable
```

This runs `windeployqt` so `build/dist` becomes a self-contained **portable release
directory**: copy it to any Windows machine with the Visual C++ 2015–2022 x64
runtime and it will start.

Then build the installer:

```bat
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\OpenCardCanvas.iss
```

See [INSTALLATION.md](INSTALLATION.md).

## 6. Troubleshooting

**`Could not find a package configuration file provided by "Qt6"`**
`CMAKE_PREFIX_PATH` does not point at the kit *directory* (the one containing
`lib/cmake/Qt6`), not at `C:/Qt` or `C:/Qt/6.8.3`.

**`Cannot open include file: 'QtWidgets/QApplication'`**
You configured with a Qt built for a different compiler (for example MinGW).
Use the `msvc2022_64` kit with MSVC.

**Link errors mentioning `winspool.lib` or `IBidiSpl`**
The printer integration needs the Windows SDK. Install the latest *Windows SDK* in
the Visual Studio Installer. Note there is no `bidispl.lib`; the spooler COM
classes are obtained through `__uuidof(BidiSpl)` / `__uuidof(BidiRequest)`, which
is why the build only links `ole32`, `oleaut32` and `uuid`.

**TIFF images cannot be imported**
Install the Qt module `qtimageformats` for your kit (see above).

**The application starts but the canvas is blank**
Check `%LOCALAPPDATA%\OpenCardCanvas\logs\OpenCardCanvas.log`. Rendering problems,
missing fonts and missing assets are all logged with the object id that caused
them.

**Continuous integration / headless build**
Set `OCC_BUILD_TESTS=OFF` if you do not want the tests, and always pass
`QT_QPA_PLATFORM=offscreen` when running a GUI test.
