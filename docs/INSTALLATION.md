# Installing OpenCardCanvas

## 1. Which package do I need?

| Package | Use it when |
|---------|-------------|
| `OpenCardCanvas_Setup.exe` | Normal installation on a workstation. Adds Start Menu entries, optionally a desktop shortcut, registers `.occard` files, and provides uninstall. |
| Portable folder | Running from a USB stick or a locked-down machine, or testing. |
| Building from source | You are developing. See [BUILD.md](BUILD.md). |

## 2. Installation

1. Run `OpenCardCanvas_Setup.exe`.
2. Choose **Install for me only** (no administrator rights needed; writes to
   `%LOCALAPPDATA%\Programs\OpenCardCanvas`) or **Install for all users**
   (needs elevation, writes to `Program Files`).
3. Optionally tick *Create a desktop shortcut* and *.occard file association*.
4. Finish. OpenCardCanvas is created in the Start Menu, along with a shortcut to
   the printer setup guide.

### Upgrading an existing installation

Run the new installer without uninstalling the old version. Setup reuses the
existing installation folder, installation scope (current user or all users),
and shortcut choices. Save your work and close OpenCardCanvas before upgrading;
if it is running, Setup will offer to close it before replacing program files.
Projects, templates, auto-saves and settings stay in the user profile and are
not deleted by the upgrade. This is an installer-based upgrade, not an automatic
Internet update service.

### Silent install

```bat
OpenCardCanvas_Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART ^
    /TASKS="desktopicon,occardassoc"
```

### Where things are written

| What | Where |
|------|-------|
| Program files | `%LOCALAPPDATA%\Programs\OpenCardCanvas` (per user) or `C:\Program Files\OpenCardCanvas` |
| Settings | `%APPDATA%\OpenCardCanvas\OpenCardCanvas.ini` |
| Templates | `%APPDATA%\OpenCardCanvas\templates\` |
| Logs | `%LOCALAPPDATA%\OpenCardCanvas\logs\OpenCardCanvas.log` |
| Auto-save | `%LOCALAPPDATA%\OpenCardCanvas\autosave\` |
| Default exports | `%USERPROFILE%\Documents\OpenCardCanvas\` |

Nothing is written next to the executable, so Program Files can stay read-only.
Uninstalling deliberately **keeps** your projects, templates and settings — they
are your data, not the application's.

## 3. Portable deployment

`cmake --build build --target portable` runs `windeployqt`, after which
`build/dist` is self-contained (the development executables stay in `build/bin`):

```
build/dist/
    OpenCardCanvas.exe
    Qt6Core.dll  Qt6Gui.dll  Qt6Widgets.dll  Qt6PrintSupport.dll  Qt6Svg.dll  ...
    platforms/       qwindows.dll
    imageformats/    qjpeg.dll  qtiff.dll  qsvg.dll  ...
    iconengines/     qsvgicon.dll
    styles/          qmodernwindowsstyle.dll
    sqldrivers/      qsqlite.dll
```

Copy that folder anywhere. The target machine needs the **Microsoft Visual C++
2015-2022 Redistributable (x64)**; if it is missing, install it or copy
`msvcp140.dll`, `vcruntime140.dll` and `vcruntime140_1.dll` next to the
executable.

Portable mode is genuinely portable: all state still lives under the user
profile, so nothing is written into the folder and a read-only medium works.

## 4. Uninstalling

*Settings → Apps → OpenCardCanvas → Uninstall*, or the Start Menu uninstall
entry. The Qt runtime, docs and file associations are removed; your projects,
templates, settings and logs are left in place. Delete the folders in the table
above if you want a completely clean removal.

## 5. Card printer driver

OpenCardCanvas **does not** ship the Entrust / Datacard printer driver; it is
licensed separately. Install the *XPS Card Printer Driver* that came with your
printer following the vendor's instructions, then see
[PRINTER_SETUP.md](PRINTER_SETUP.md) to confirm OpenCardCanvas detects it.

Without a card printer the application is still fully usable: you can design,
personalize, preview and export, and the clearly-labelled **Simulator** backend
lets you rehearse a print workflow without hardware.

## 6. First run

1. **File → New** (Ctrl+N). Pick *ISO/IEC 7810 ID-1* and press OK.
2. Add a **Text** object, a **Photo**, a **QR code** and a **Barcode** from the
   Tools panel; edit them in the Properties panel on the right.
3. Switch to **BACK** with the toggle in the status bar and add content there.
4. **File → Save As** (Ctrl+Shift+S) to write a `.occard` project.
5. **File → Export → PNG (300 dpi)** to check the output pixel size.
6. **Print → Print Preview** to see exactly what will be sent to the printer.
7. Choose your printer in **Print → Print Preview**, then click **Save printer
   for future printing**. Alternatively use **Print → Printer Settings → Refresh**,
   select a queue, and click **Save printer**. This selection is independent of
   the Windows default printer. An unavailable saved queue is retained rather
   than silently replaced with another printer or the simulator.

If anything misbehaves, **Help → Diagnostics** shows the log tail, the paths and
a project validation report, and can open the log folder for you.
