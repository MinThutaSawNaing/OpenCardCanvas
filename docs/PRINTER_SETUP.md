# Printer setup and hardware verification

Covers the Entrust / Datacard card printer integration: which interfaces are used,
how to prepare a printer, what the application can and cannot report, and the
checklist to run once a physical printer is available.

> The development machine for this build has **no card printer attached**. Items
> marked *(pending)* are implemented but have not been exercised on real hardware.
> This document is not a claim that printing has been physically verified.

---

## 1. What the integration uses

OpenCardCanvas invents no printer API. It uses the two documented, supported
interfaces of the *XPS Card Printer Driver*.

### 1.1 Printing — Win32 GDI on the printer's device context

This is the path the SDK's own C++ samples use:

1. `OpenPrinter` for a handle.
2. `DocumentProperties` to obtain and modify the driver `DEVMODE`:
   `dmOrientation = DMORIENT_LANDSCAPE` (card printers are landscape),
   `dmDuplex = DMDUP_VERTICAL` for two-sided, `DMDUP_SIMPLEX` otherwise.
3. `CreateDC(L"WINSPOOL", printerName, NULL, devmode)`.
4. `StartDoc` → `StartPage` → draw → `EndPage`
   → [`ResetDC` → `StartPage` → draw → `EndPage` for the back] → `EndDoc` →
   `DeleteDC`.
5. The card is rendered **once** by the same renderer that feeds the editor and the
   preview, at the printer's own resolution (300×300 or 600×600 dpi, read from the
   device), and blitted with `StretchDIBits`. What you preview is what prints.
6. The spooler is polled until it has accepted all job data before the job is
   declared complete.

Driver options that exist only as escapes (top coat blocking, magnetic stripe,
tactile impresser) are sent with `TextOut` using the documented escape strings
(`~TA%…` top coat add, `~PB%…` print blocking, `~1`/`~2`/`~3` magnetic tracks,
`~IT%…` impresser). They are only sent when the user explicitly enables them.

### 1.2 Status, capabilities, supplies — print spooler `BidiSpl` COM

Printer information is read through the spooler's bidirectional interface
(`IBidiSpl` / `IBidiRequest`). The schema strings come verbatim from the SDK
header `DXP01SDK.H`:

| Schema | Gives |
|--------|-------|
| `\Printer.PrinterOptions2:Read` | model, serial, firmware, status text, print head, duplex, laminator, magstripe, smart card, embosser, resolutions |
| `\Printer.PrinterOptions3:Read` | colour mode |
| `\Printer.SuppliesStatus3:Read` | print ribbon, top coat, retransfer film, laminator L1/L2, impresser foil |
| `\Printer.PrintMessages:Read` | device messages with error code and severity |
| `\Printer.JobStatus:Read` | `JobActive` / `JobSucceeded` / `JobFailed` / `JobCancelled` / `CardReadyToRetrieve` / `CardNotRetrieved` / `NotAvailable` |
| `\Printer.SDK:Version` | driver SDK version |
| `\Printer.Hopper:Status:Get` | input hopper status (may be unsupported) |
| `\Printer.Print:StartJob:Set` | claims a printer job id before spooling |
| `\Printer.Print:EndJob:Set` | signals that all data for the card was sent |
| `\Printer.Action:Set` | cancel / resume / restart |

There is no `bidispl.lib` in the SDK: the COM classes are obtained with
`__uuidof(BidiSpl)` / `__uuidof(BidiRequest)`.

### 1.3 Backend selection

`PrinterManager` enumerates queues with
`EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, level 2)` and then
**asks the driver**: if a `BidiSpl` round trip to `\Printer.SDK:Version` or
`\Printer.PrinterOptions2:Read` succeeds, the queue is an XPS Card Printer queue
and the Entrust backend is used; otherwise the queue is handled as a plain Windows
printer. Detection is a real query, never a guess from the printer's name.

## 2. Preparing a printer

### 2.1 Check the installation from the command line (fastest)

Before opening the application, run it from a terminal. These two modes create no
window, report what the spooler and the driver actually say, and write a copy to
`%LOCALAPPDATA%\OpenCardCanvas\logs\printer-report.txt`:

```bat
:: Which printers exist, and which backend each one will use?
OpenCardCanvas.exe --list-printers

:: Full device + SDK report for one printer (omit the name to use the first)
OpenCardCanvas.exe --printer-report "XPS Card Printer"
```

`--list-printers` prints one block per queue. A correctly installed card printer
shows:

```
Name         : XPS Card Printer
  Backend    : Entrust XPS card printer      <-- the XPS Card Printer driver answered
  Port       : USB001
  Driver     : XPS Card Printer Driver
  Model      : CE840
  Serial     : 12345678
  Duplex     : yes
  Card job   : yes (XPS Card Printer driver detected)
```

If your printer shows `Backend : Windows printer` and
`Card job : treated as a plain Windows print queue`, the queue is **not** driven by
the XPS Card Printer driver — reinstall the driver, or select the driver's own
queue rather than a generic one. Printing will still work through the Windows
backend, but without card-specific features, ribbon status or card job tracking.

### 2.2 Then check it in the application

1. Install the **XPS Card Printer Driver** shipped with the printer. The driver is
   **not** distributed with OpenCardCanvas — install it separately per the
   vendor's instructions.
2. Confirm the queue exists in *Settings → Bluetooth & devices → Printers &
   scanners* and note its exact name (for example `XPS Card Printer`).
3. In OpenCardCanvas: **Print → Printer Settings → Refresh**. The printer should
   appear with `Backend = Entrust XPS card printer`.
4. Use **Driver information** and **SDK information**. A working installation
   reports model, serial, firmware and SDK version. If the SDK version shows
   *not available*, the queue is not an XPS Card Printer driver queue and printing
   falls back to the generic Windows path.
5. Check the ribbon level. If **print ribbon remaining** shows *not available*,
   this driver version does not answer the supplies schema; printing may still work
   but ribbon consumption cannot be predicted.
6. **Print → Printer Diagnostics** shows the same report inside the application.
7. **Print → Print Test Card** prints a single generated test card. It is labelled
   as a test card, and the result you are shown is the printer's own job state.

## 3. What the application refuses to report

* A value the driver does not expose is shown as **Not available**.
* A status that could not be queried is **Unknown**, not "Ready".
* A job is successful only when `\Printer.JobStatus:Read` reached `JobSucceeded`.
  `JobFailed`, `JobCancelled`, `CardNotRetrieved` and timeouts are failures, shown
  with the printer's own error code and message plus a plain-language explanation.
* The **Simulator** never claims a card was printed: its name, tooltip, status text
  and completion message all state that it rendered a file and did not print.


## 4. Duplex, copies and card sides

* **Front only** — one page, `DMDUP_SIMPLEX`.
* **Front + back** — one document, two pages, `DMDUP_VERTICAL`, with `ResetDC`
  between pages so each side can have its own orientation.
* **Copies** — the driver's own copy count is used instead of sending the image
  `n` times, so the printer's card accounting stays correct.
* **Input hopper** — passed to the `StartJob` request; empty means driver default.
* **Card eject side** — `Front` / `Back` / driver default.

## 5. Hardware test checklist *(pending — run with a printer attached)*

| # | Case | Expected |
|---|------|----------|
| 1 | Detect printer (Refresh) | Queue listed, backend `Entrust XPS`, SDK version shown |
| 2 | Read capabilities | Model / serial / firmware / duplex / laminator / magstripe flags match the unit |
| 3 | Read supplies | Ribbon percentage matches the printer's own display (±5 %) |
| 4 | Print test card (single sided) | One card printed; job state reaches `JobSucceeded` |
| 5 | Print front + back | Both sides printed, correct orientation, back not upside down |
| 6 | Print 3 copies | Exactly three cards, one job, correct count on the printer |
| 7 | Text card | Fonts correct, no substitution, edges sharp |
| 8 | Image / photo card | Photograph without colour cast, correct crop |
| 9 | QR card | QR scans with a phone at the printed size |
| 10 | Barcode card | Barcode scans; narrow bar ≥ 0.19 mm as reported by the editor |
| 11 | Batch, 20 records with photos | All 20 distinct, correct photo per record, no duplicates |
| 12 | Printer offline (powered off) | Clear error naming the printer and the reason; no false success |
| 13 | Printer disconnected mid-batch | Job reported as failed; remaining records not silently skipped |
| 14 | Invalid printer name | Clear "printer not found" message |
| 15 | Cancel a job | Job reported as cancelled; printer returns to Ready |
| 16 | Ribbon exhausted | Printer's own error surfaced with code and message |
| 17 | Card jam | Printer error surfaced; no retry storm |
| 18 | 600 × 600 dpi model | Resolution detected and used; image not resampled twice |

## 6. Non-card printers and the simulator

Two further backends keep the application usable:

* **WindowsPrinterBackend** — prints through any installed Windows queue using Qt's
  print support. Intended for proof sheets and drafts. It is a real printer backend
  (`isRealHardware()` is true) but it is not card hardware.
* **SimulatorPrinter** — clearly separated. It renders the card and writes the PNG
  plus a small job record to disk so a workflow can be rehearsed end to end, and it
  is structurally incapable of reporting a print. Every surface that mentions it
  says so.

## 7. Troubleshooting

**The printer appears with `Backend = Windows`**
The queue is not driven by the XPS Card Printer driver. Reinstall the driver, or
make sure you selected the driver's own queue.

**`BindDevice` / BidiSpl fails**
Usually the print spooler is not running, the queue name is wrong, or the user lacks
print access. Restart the *Print Spooler* service and check the queue name
character for character. Technical detail is in
`%LOCALAPPDATA%\OpenCardCanvas\logs\OpenCardCanvas.log`.

**Status is Not available for everything**
The driver answered the connection but not the schemas — typically an older driver.
Update the XPS Card Printer driver. Generic Windows printing still works through the
Windows backend.

**The card prints rotated**
Check the driver's own settings: card printers expect a landscape page.
OpenCardCanvas sets `dmOrientation` explicitly, but a per-printer driver default
override can defeat it.

**Nothing prints and no error appears**
Inspect the Windows print queue first — if the job is stuck in the spooler, the
problem is between the spooler and the printer. The Diagnostics dialog lists the
Windows job id so the spooler job can be identified.
