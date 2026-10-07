# Development guide

## 1. The three rules that shape this codebase

Read these first; they explain most of the design decisions.

**1. Millimetres are the only internal length unit.**
A card is a physical object. Every coordinate, size, stroke width and spacing in
the document model is in millimetres. Pixels appear only at the last moment, in
`RenderContext::pxPerMm`, and there is exactly one conversion function, so there
can never be two answers to "how many pixels is a millimetre".

**2. There is exactly one render path.**
`CardRenderer::paintSide()` is the *only* place that iterates a `CardSide` and
paints it. The editing canvas, the print preview, the PNG/PDF export and the
printer output all call it. That makes "what you see is what prints" a structural
property rather than a hope. If you find yourself writing a second paint loop,
stop.

**3. Text is laid out as vector outlines at a fixed reference size.**
Qt maps font point sizes through a paint device's logical DPI, which makes the same
card re-flow between a widget, an image and a printer. `TextLayout` avoids that by
generating glyph outlines from a font pinned to 256 px and scaling them by an exact
factor into millimetres. Layout is therefore identical on every device at every
resolution — and the output is true vector text, which is what makes printed text
sharp.

## 2. Architecture

```
src/
  main.cpp              identity, high DPI, logging, translations, run

  core/                 document model - no Qt GUI, no file IO, no printing
    Units               the single definition of every length conversion
    CardGeometry        physical card, presets, DPI, bleed, orientation
    CardObject          abstract base: geometry, rotation, opacity, JSON, paint hook
    Text/Image/Photo/   concrete objects; each paints itself and serialises itself,
    Shape/Qr/Barcode/   and nothing else
    Group
    CardSide            one side: background, owned objects, z-order authority
    CardDocument        QObject facade over geometry + two sides + assets + metadata
    ObjectFactory       JSON "type" -> concrete object

  codes/                pure algorithms, no Qt GUI
    QrEncoder           ISO/IEC 18004, versions 1-40, L/M/Q/H, RS error correction
    BarcodeEncoder      Code128, Code39, EAN-13, EAN-8, ITF-14 + validation

  rendering/            everything that turns the model into pixels
    RenderContext       pxPerMm, device DPI, assets, placeholders, warnings
    TextLayout          deterministic text layout and glyph outlining
    CardRenderer        THE render path (paintSide / renderSide)
    ExportRenderer      PNG, high-res PNG, PDF, batch export

  canvas/               the editor surface
    CanvasGeometry      pure mm <-> view maths, handles, hit testing (unit tested)
    GuideModel          per-side guides
    SnapEngine          pure snapping computation (unit tested)
    RulerWidget         rulers and guide dragging
    CardCanvas          selection, move, resize, rotate, inline text editing
    CanvasHost          canvas + rulers + scroll area

  commands/             QUndoCommand layer; snapshots object state as JSON
  project/              AssetStore, ProjectSerializer (.occard), ProjectValidator,
                        AutoSave, TemplateStore
  personalization/      CsvImporter, TemplateEngine, DataMapper, BatchRenderer
  printing/             PrinterTypes, ICardPrinter, EntrustXpsPrinter,
                        WindowsPrinterBackend, SimulatorPrinter, PrinterManager
  utils/                Logger, AppPaths, Settings, SimpleZip, BidiClient,
                        GdiPrintSurface, ImageCache, TextUtils
  ui/                   MainWindow and the panels/dialogs
```

Dependencies point one way: `core` knows nothing about rendering, project files or
printing; `rendering` knows the model and the `RenderContext`; `ui` sits on top.

### Why the model paints at all

`CardObject` declares `paintObject(QPainter&, const RenderContext&)`. That is the
model's only painting touchpoint and it is deliberate: painting *is* the object's
responsibility, but the object never decides scale, device or colour handling — the
`RenderContext` does. The model stays testable (a test can paint into a small
`QImage` and assert pixels) without a second drawing implementation existing.

## 3. Adding a new object type

1. Add a value to `ObjectType` and a row to the `names::kObjectTypes` table in
   `core/CardTypes.h`.
2. Subclass `CardObject` and implement `paintObject`, `propertiesToJson`,
   `propertiesFromJson`, `defaultName`, `cloneImpl`.
3. Register it in `ObjectFactory::create` / `createAndLoad`.
4. Paint in **local millimetres**: `(0,0)` is the object's top-left, the size is
   `widthMm() x heightMm()`, and rotation and opacity are already applied.
5. Report problems through `RenderContext::addWarning()` rather than drawing
   something misleading. That is how a missing image, an uninstalled font or an
   unencodable payload reaches the user.
6. Add a test that renders into a `QImage` and asserts real pixels.

## 4. Adding an undoable operation

Snapshot-based commands keep this cheap. For an operation that changes existing
objects, capture before/after JSON and push a single `ModifyObjectsCommand`:

```cpp
const auto before = ModifyObjectsCommand::captureState(side, ids);
// ...mutate the objects...
const auto after = ModifyObjectsCommand::captureState(side, ids);
stack->push(new ModifyObjectsCommand(&doc, sideId, before, after,
                                     tr("Resize %n object(s)", "", ids.size())));
```

Payloads are small because images live in the `AssetStore` by id, so a snapshot is
a few hundred bytes. `mergeWith()` composes consecutive edits to the same objects,
so dragging a slider does not create two hundred undo steps.


## 5. Threading and responsiveness

The GUI thread must never block on image decoding, CSV parsing, PDF generation,
printing or a large project load.

* **One card, one action** (a preview render, a single export) is synchronous — it
  is genuinely fast, and a progress dialog would flicker more than it helps.
* **Anything proportional to the number of records** (batch render, batch print, a
  large CSV) runs on a worker with a `QProgressDialog` and real cancel support.
* Image decoding goes through `ImageCache` and `QImageReader` with explicit size
  limits, so a 200 MP photograph cannot exhaust memory.
* Printer operations run off the GUI thread and report through `PrintJob::progress`,
  so a card that takes 30 seconds does not freeze the window.

## 6. Error handling and logging

* The **user** sees what failed, the printer/file involved, the reason, and what to
  do next — never a stack trace, never a raw HRESULT.
* The **log** gets the technical detail: the object id, the SDK/COM error, the Win32
  error. Use the `occ.print.sdk`, `occ.project`, `occ.render`, `occ.data` categories.
* Cardholder data must never reach the log. `Logger::redact()` masks e-mail addresses
  and long digit runs; do not log object text, image bytes or CSV rows.
* `RenderContext::addWarning()` is the mechanism for "this rendered, but not the way
  you asked": a missing font, a missing image, text clipped by its box, a barcode too
  narrow to scan. Warnings surface in the status bar and in **Help → Diagnostics**.
  Silently drawing something else is never acceptable.

## 7. User-visible strings

Every user-visible string goes through `tr()` / `QCoreApplication::translate()`.
English is the source language; translations are picked up automatically from a
`translations/` folder next to the executable (`qtbase_<locale>.qm`,
`OpenCardCanvas_<locale>.qm`). Log messages and internal identifiers are not
translated.

## 8. Coding conventions

* C++20, MSVC `/W4 /permissive-`; warnings are treated as defects.
* No `using namespace` in headers. `namespace occ` throughout.
* Ownership is explicit: `std::unique_ptr` for owned objects, raw pointers for
  non-owning observers, `std::shared_ptr` only for `ICardPrinter`.
* Nothing throws across the Qt event loop. Backends return `false` and set
  `lastError()`.
* Prefer functions with no hidden state. `CanvasGeometry` and `SnapEngine` are
  deliberately static and pure so they can be unit tested without a widget.

## 9. Tests

```bat
ctest --test-dir build --output-on-failure
```

| Test | Covers |
|------|--------|
| `test_units` | conversions, geometry, orientation, JSON, physical pixel sizes |
| `test_qr` | QR structure **and a module-for-module comparison against golden matrices from the independent `segno` library** |
| `test_barcode` | Code128/39, EAN-13/8, ITF-14, check digits, rejection of invalid data |
| `test_zip` | CRC-32 reference vectors, round trip, determinism, atomic write, path-traversal rejection, truncated archives |
| `test_objects` | object model, transforms, hit testing, z-order, grouping |
| `test_undo` | undo/redo of move, resize, text, style, add, remove, reorder |
| `test_serialization` | save/load round-trip equality, newer-version refusal, corrupt files |
| `test_rendering` | objects actually produce pixels; bleed; missing-asset handling |
| `test_template` | `{{placeholder}}` expansion, fallbacks, missing keys |
| `test_csv` | quoting, embedded newlines, BOM, semicolons, ragged rows, errors |
| `test_canvas` | snapping, guides, zoom/geometry round trips |
| `test_printer` | the printer abstraction and the simulator backend |
| `test_errors` | invalid projects, missing files, unusable input |

After touching `QrEncoder`, regenerate the golden data:

```bat
python tools\qr\generate_golden.py
```

Tests run with `QT_QPA_PLATFORM=offscreen`, so no desktop session is needed.

## 10. Release checklist

1. `tools\build.ps1 -Clean -Tests` — clean configure, build and test must all pass.
2. `cmake --build build --target portable` — windeployqt output.
3. Launch `build\bin\OpenCardCanvas.exe` on a machine **without** Qt installed and
   run the workflow in [README.md](../README.md) once.
4. `ISCC.exe installer\OpenCardCanvas.iss`, then install and uninstall the result.
5. Walk the hardware checklist in [PRINTER_SETUP.md](PRINTER_SETUP.md) on a machine
   with a card printer attached and record the outcome. Anything not exercised stays
   marked *verification pending* — do not claim hardware compatibility that has not
   been tested.
