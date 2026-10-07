# .occard project format

Version **1**. The version this build understands is defined once, in
`src/utils/Version.h.in`, as `OCC_PROJECT_FORMAT_VERSION`; the running application
reports it in **Help → About** and writes it into every project.

## 1. Container

A `.occard` file is a **standard ZIP archive** with entries *stored*
uncompressed. Any ZIP tool can open it, so a project can always be inspected or
recovered by hand.

```
project.json            the document (section 2)
assets/<assetId>.<ext>  original image bytes, one entry per imported image
thumbnail.png           optional ~200 px front preview, used by Open Recent
```

Entries are stored rather than deflated because card artwork is already compressed
(PNG/JPEG): re-compressing gains almost nothing and would make reading a project
file depend on a compression library. Timestamps are fixed at 1980-01-01, so
saving identical content twice produces byte-identical output — `.occard` files
are diffable and backup friendly.

The container is written atomically: `<name>.occard.tmp`, then renamed over the
target. A crash or a full disk can never leave a half-written project behind.

## 2. project.json

```jsonc
{
  "formatVersion": 1,
  "application": { "name": "OpenCardCanvas", "version": "1.0.0" },

  "metadata": {
    "title": "Staff card", "author": "", "notes": "",
    "created":  "2026-01-14T09:12:03Z",
    "modified": "2026-01-14T09:40:55Z",
    "isTemplate": false,
    "templateDescription": "",
    "templatePlaceholders": ["name", "employee_id", "department", "photo"]
  },

  // CardGeometry::toJson(). All lengths in millimetres.
  "geometry": {
    "preset": "iso-id1", "widthMm": 85.6, "heightMm": 53.98,
    "renderDpi": 300, "bleedMm": 0.0
  },

  // CardSide::toJson(). "front" and "back" have the same shape; a missing side
  // is treated as empty rather than as an error.
  "front": {
    "background": {
      "kind": "solid",          // none | solid | linear | radial | image
      "color": "#ffffffff", "color2": "#ffffffff",
      "angleDeg": 0.0, "assetId": "", "stretchImage": true
    },
    "objects": [
      {
        "type": "text",         // text|image|photo|shape|qr|barcode|group
        "id": "6f1c0a2e-...",   // stable identity, survives save and load
        "name": "Employee Name",// the layer name, user editable
        "rectMm": { "x": 32.0, "y": 12.5, "w": 45.0, "h": 7.0 },
        "rotation": 0.0,        // degrees clockwise about the centre
        "opacity": 1.0,         // 0..1
        "visible": true,
        "locked": false,
        "z": 3,                 // paint order hint; array order is authoritative

        // Everything type specific lives under "properties" so base and
        // per-type keys can never collide.
        "properties": {
          "text": "{{name}}",   // placeholders are expanded per data record
          "fontFamily": "Arial", "fontSizePt": 9.0,
          "bold": true, "italic": false, "underline": false,
          "hAlign": "left",     // left | center | right | justify
          "vAlign": "middle",   // top | middle | bottom
          "lineSpacingPercent": 100.0, "letterSpacingMm": 0.0,
          "wordWrap": true, "autoShrink": false,
          "color": "#ff000000",
          "outlineColor": "#00000000", "outlineWidthMm": 0.0
        }
      },
      {
        "type": "qr", "name": "QR Code",
        "rectMm": { "x": 62.0, "y": 26.0, "w": 22.0, "h": 22.0 },
        "properties": {
          "data": "{{employee_id}}",
          "ecc": "M",           // L | M | Q | H
          "contentKind": "employee",
          "foreground": "#ff000000", "background": "#ffffffff",
          "quietZoneModules": 4
        }
      },
      {
        "type": "photo", "name": "Employee Photo",
        "rectMm": { "x": 4.0, "y": 14.0, "w": 24.0, "h": 32.0 },
        "properties": {
          // Asset ids reference assets/<id>.<ext> inside the same archive.
          "assetId": "9b3f1d77-...",
          "fit": "cover",       // stretch | contain | cover | center | tile
          // Non-destructive crop: a normalised rectangle of the source image.
          "sourceRect": { "x": 0.08, "y": 0.0, "w": 0.84, "h": 1.0 },
          "background": "#00000000",
          "cropShape": "ellipse", // rectangle | rounded | ellipse
          "cornerRadiusMm": 3.0,
          "borderWidthMm": 0.4, "borderColor": "#ff1f3864",
          "lockedAspectRatio": 0.75
        }
      }
    ]
  },

  "back": { "background": { }, "objects": [ ] },

## 3. Why the format is shaped this way

* **Millimetres everywhere.** A card is a physical object; storing millimetres
  means the file keeps its true size no matter what resolution it is later
  rendered or printed at.
* **Enum values are strings, never integers.** Adding a shape kind or a symbology
  can never silently reinterpret an existing file.
* **`properties` is per-type**, so base and specific keys cannot collide and a
  reader knows exactly where to look.
* **Non-destructive image edits.** `sourceRect` is a window into the original bytes
  in `assets/`. The imported image is never modified or re-encoded, so changing a
  crop is lossless and reversible.
* **`z` plus array order.** The array is authoritative for paint order; `z` is
  written for readability and renumbered on load.

## 4. Versioning and migration

* `formatVersion` is mandatory for files written by OpenCardCanvas.
* **At or below** the running build's version → the file is loaded and migrated if
  a migration step exists, and the upgrade is reported as a warning.
* **Above** the running build's version → loading is refused with a clear message:
  *"This project was created by a newer version of OpenCardCanvas (format 2).
  Please update the application."* The file is left untouched.
* A bare `project.json` with no `formatVersion` is treated as version 0 and
  migrated to 1 (metadata defaults are filled in).
* Unrecognised keys are ignored and collected as warnings rather than causing a
  failure, so extra fields from a newer file do not needlessly block an older
  build — the `formatVersion` check is what actually protects the user.

## 5. Validation on load

Nothing from the file is trusted:

| Field | Guard |
|-------|-------|
| geometry | `CardGeometry::fromJson` rejects non-finite or out-of-range values; a bad size is a load error, not a silently resized card |
| object `rectMm` | must be four numbers with positive width and height, otherwise a load error |
| object `type` | must match the concrete class the reader created, otherwise a load error |
| enum strings | unknown values fall back to the documented default and raise a warning |
| `assetId` | a missing asset is reported as a *missing image* and drawn as a clearly marked placeholder, never silently blank |
| ZIP entry names | absolute paths, `..` components and drive letters are rejected, so a hostile project cannot write outside the intended directory |
| ZIP size fields | entry count and per-entry size are capped and body offsets are bounds-checked before allocating |

**Help → Diagnostics** runs `ProjectValidator` over the open document and lists
every problem with a severity: missing images, fonts that are not installed, QR or
barcode payloads that are invalid at their own settings, barcodes too narrow to
print reliably, text that cannot fit its box, and objects that fall completely
outside the card.

## 6. Writing a project by hand

Because the container is plain ZIP and the payload plain JSON, a minimal project
can be written in a text editor and zipped:

```jsonc
{ "formatVersion": 1,
  "geometry": { "preset": "iso-id1", "widthMm": 85.6, "heightMm": 53.98,
                "renderDpi": 300, "bleedMm": 0 },
  "front": { "objects": [] },
  "back":  { "objects": [] } }
```

OpenCardCanvas also accepts such a file **unzipped** (a bare `.json` renamed to
`.occard`): when the bytes are not a ZIP, the loader falls back to parsing them as
`project.json` and reports that as a warning. That keeps hand-written and
script-generated projects usable.


  // Print defaults, restored into the print dialogs.
  "printSettings": {
    "printer": "XPS Card Printer", "copies": 1, "duplex": false,
    "frontEnabled": true, "backEnabled": false,
    "frontTopcoat": "DriverDefault",  // DriverDefault|All|Except|Iso7816|Iso2Track|Iso3Track|Jis
    "backTopcoat": "DriverDefault",
    "hopper": "", "cardEjectSide": ""
  }
}
```
