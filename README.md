# OpenCardCanvas

Professional ID-card design, personalization, preview and printing software for
Windows 10 and 11.

OpenCardCanvas is a native C++/Qt 6 desktop application for designing ID cards
(front and back), personalizing them from a CSV data set, previewing them exactly
as they will print, and printing them on Entrust / Datacard XPS card printers or
any installed Windows printer.

---

## 1. What it does

| Area | Capability |
|------|-----------|
| Card setup | ISO/IEC 7810 ID-1 (85.60 × 53.98 mm) plus ID-2, ID-3, CR79, portrait and custom sizes; orientation, render DPI, bleed |
| Design | Text, images, ID photographs, shapes, QR codes, linear barcodes, backgrounds, layers |
| Editing | Move, resize, rotate, align, distribute, group, lock, hide, z-order, snapping, rulers, guides, grid |
| Units | Millimetres or inches; the document is stored in millimetres so the design is physically accurate at any screen DPI or output resolution |
| Undo/redo | Full command-based undo stack covering every editor operation |
| Project file | `.occard` — a documented, versioned ZIP container (JSON + assets + thumbnail) |
| Safety | Timed auto-save, crash recovery, atomic writes, unsaved-changes protection |
| Templates | Save / open / duplicate / rename / delete templates with `{{placeholder}}` fields |
| Personalization | CSV import (RFC 4180), field mapping, per-record preview, batch printing |
| Output | PNG, high-resolution PNG, PDF; print preview using the same rendering pipeline as printing |
| Printing | Windows printers, Entrust/Datacard XPS card printers, and a clearly separated simulator |
| Diagnostics | Structured logging, printer capability/SDK reporting, project validation |

## 2. What is verified, and what is not

Being precise about this is part of the product, not a footnote.

**Verified on this machine**

* Clean configure, build and link with MSVC v143 (14.44) + Qt 6.8.3 + CMake + Ninja.
  The engine library and the application build with **zero errors and zero
  warnings** at `/W4 /permissive-`, and the installer compiles.
* **13 of 13 automated tests pass** (`ctest --test-dir build --output-on-failure`):
  unit conversion and physical accuracy, card geometry, the object model and its
  transforms, the undo/redo command layer, project serialization round-trip and
  its failure modes, real pixel-level rendering, QR and barcode encoding, the ZIP
  container, CSV import, template expansion, canvas/snapping maths, the printer
  abstraction and its simulator, and error handling.
* The **QR encoder is compared module-for-module against golden matrices produced
  by an independent implementation** (`segno`), across 67 payload/ECC cases.
* The **`.occard` container is verified against a third-party ZIP reader**
  (PowerShell `Expand-Archive` and Python `zipfile.testzip()` both read it, and
  the extracted bytes match the originals).
* The application **launches and runs** with a clean log and no Qt warnings.
* The **portable distribution starts on a machine with no Qt on `PATH`**:
  `windeployqt` output was launched from a neutral working directory with Qt
  removed from the environment.
* `OpenCardCanvas_Setup.exe` (~22 MB) is produced by Inno Setup.

**Implemented, but hardware verification pending**

* Printing to an Entrust / Datacard XPS card printer. There is no physical card
  printer attached to the development machine. The integration uses the
  documented interfaces of the *XPS Card Printer Driver Software Developers Kit*
  (print spooler `BidiSpl` COM for printer options / supplies / job status, and
  the Win32 GDI print path plus the driver's print-ticket and escape options for
  printing). The code paths are exercised against the SDK's own data shapes —
  including XML parsing tests built from the SDK sample payloads — but a card has
  not physically been printed from this build.
  See [docs/PRINTER_SETUP.md](docs/PRINTER_SETUP.md) for the 18-case test checklist
  to run once a printer is available.

**Not yet verified**

* The end-to-end interactive workflow (draw → design → save → reopen → export →
  preview → print) has not been driven by an automated UI test. The steps behind
  it are covered by the library tests above and the application has been launched
  and inspected, but a full click-through has **not** been performed. Treat that
  as the next QA task rather than as verified behaviour.

Nothing in the application reports a card as printed unless the printer reported
the job as succeeded. The simulator backend is labelled as a simulator everywhere
it appears and is structurally incapable of claiming a real print.

## 3. Requirements

* Windows 10 (1809 or later) or Windows 11, x64
* Qt 6.5 or newer (6.8.3 LTS used for development)
* Visual Studio 2022 with the *Desktop development with C++* workload
* CMake 3.21 or newer, Ninja (recommended)
* For card printing: the Entrust / Datacard XPS Card Printer Driver installed for
  the printer (not redistributed with this application)
* For packaging: Inno Setup 6

## 4. Quick start

```bat
:: 1. Configure
cmake -S . -B build -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64

:: 2. Build
cmake --build build

:: 3. Test
ctest --test-dir build --output-on-failure

:: 4. Run
build\bin\OpenCardCanvas.exe
```

Or use the helper script, which also produces the log used for troubleshooting:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Tests
```

## 5. Documentation

| Document | Contents |
|----------|----------|
| [BUILD.md](docs/BUILD.md) | Toolchain setup, configuring, building, testing, troubleshooting build errors |
| [INSTALLATION.md](docs/INSTALLATION.md) | Installing the packaged application, silent install, uninstall, portable build |
| [PRINTER_SETUP.md](docs/PRINTER_SETUP.md) | Entrust/Datacard driver setup, printer configuration, capability checks, hardware test checklist |
| [DEVELOPMENT.md](docs/DEVELOPMENT.md) | Architecture, module responsibilities, coding conventions, how to extend the application |
| [PROJECT_FORMAT.md](docs/PROJECT_FORMAT.md) | The `.occard` file format, versioning and migration |

## 6. Privacy

OpenCardCanvas collects no telemetry and performs no network access. Card data,
photographs and personal records never leave the machine. Log files are
diagnostic only and deliberately redact e-mail addresses and long digit strings
(personal identifiers) before they are written.

## 7. Licence

See [LICENSE](LICENSE). Entrust and Datacard are trademarks of their respective
owners; the printer driver software is not redistributed with this application.
