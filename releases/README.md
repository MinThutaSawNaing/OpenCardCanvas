# Windows installer

`OpenCardCanvas_Setup.exe` installs OpenCardCanvas 1.0.2 for Windows 10/11 x64.
It includes print responsiveness and progress fixes, working color pickers,
and explicit printer selection and saving independent of the Windows default.

To upgrade, run this installer over the existing installation; do not uninstall
first. Setup remembers the installation location and scope. Save your work and
close the application before upgrading. User projects and settings are preserved.

Download the executable using GitHub's **Download raw file** button, then run it.
The installer is unsigned; Windows SmartScreen may show a reputation warning.
The Microsoft Visual C++ 2015–2022 Redistributable (x64) is required. Printer
drivers are installed separately; see `docs/PRINTER_SETUP.md`.

`SHA256SUMS.txt` records the SHA-256 checksum of this installer.

To reproduce it, regenerate the icon with `tools/make_icon.ps1`, build and test
with `tools/build.ps1 -Tests -Portable`, then compile
`installer/OpenCardCanvas.iss` using Inno Setup 6. Copy the resulting installer
from `installer/Output` here and regenerate its checksum.