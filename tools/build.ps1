<#
  tools/build.ps1 - configure, build and test OpenCardCanvas.

  Usage (from anywhere):
      powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1
      powershell ... -File tools\build.ps1 -Clean
      powershell ... -File tools\build.ps1 -Target OpenCardCanvas
      powershell ... -File tools\build.ps1 -Tests
      powershell ... -File tools\build.ps1 -Tests -Run

  Because a full build can take several minutes, this script writes everything
  to build\build.log.  Launch it detached and poll the log:

      Start-Process powershell -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass',
          '-File','tools\build.ps1' -WindowStyle Hidden
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','RelWithDebInfo')]
    [string] $Config = 'Release',
    [string] $Target = '',
    [string] $QtPrefix = $env:QTDIR,
    [switch] $Clean,
    [switch] $Tests,
    [switch] $Portable,
    [switch] $Run
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$root      = Split-Path -Parent $PSScriptRoot
$buildDir  = Join-Path $root 'build'
$logFile   = Join-Path $buildDir 'build.log'

if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir -Force | Out-Null }
if ($Clean -and (Test-Path $buildDir)) {
    Get-ChildItem $buildDir | Remove-Item -Recurse -Force
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
}

function Log([string]$text) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $text
    $line | Out-File -FilePath $logFile -Append -Encoding utf8
    Write-Host $line
}

"" | Out-File -FilePath $logFile -Append -Encoding utf8
Log "=== OpenCardCanvas build start (Config=$Config Target=$Target Tests=$Tests Clean=$Clean) ==="

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Log 'FATAL: Install Visual Studio Build Tools with Desktop development with C++.'
    exit 1
}
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { Log 'FATAL: No Visual Studio C++ toolchain found.'; exit 1 }
$vcvars = Join-Path $vsRoot 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path $vcvars)) { Log "FATAL: Missing $vcvars"; exit 1 }

function Find-BuildTool([string]$name, [string]$relativePath) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $bundled = Join-Path $vsRoot $relativePath
    if (Test-Path $bundled) { return $bundled }
    throw "Missing $name. Install the Visual Studio C++ CMake tools component or add it to PATH."
}
$cmake = Find-BuildTool 'cmake.exe' 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$ninja = Find-BuildTool 'ninja.exe' 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'

# --- locate Qt -------------------------------------------------------------
if ($QtPrefix -and -not (Test-Path (Join-Path $QtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    Log "FATAL: Invalid Qt kit: $QtPrefix. Pass -QtPrefix pointing at an MSVC x64 Qt 6 kit."
    exit 1
}
if (-not $QtPrefix) {
    $candidates = @(Get-ChildItem 'C:/Qt/*/msvc*_64','D:/Qt/*/msvc*_64' -Directory -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Where-Object { Test-Path (Join-Path $_.FullName 'lib/cmake/Qt6/Qt6Config.cmake') } |
        Select-Object -ExpandProperty FullName)
    if ($candidates.Count -eq 0) {
        Log 'FATAL: No Qt 6 MSVC x64 kit found. See docs/BUILD.md or pass -QtPrefix <kit directory>.'
        exit 1
    }
    $QtPrefix = $candidates[0]
}
$QtPrefix = (Resolve-Path $QtPrefix).Path.Replace('\', '/')
Log "Qt kit: $QtPrefix"

# --- build a helper batch file --------------------------------------------
$bat = Join-Path $buildDir '_build_step.bat'
$lines = @()
$lines += '@echo off'
$lines += "call `"$vcvars`" >nul"
$lines += 'if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)'
$lines += "cd /d `"$root`""
$lines += "set `"PATH=$(Split-Path $cmake);%PATH%`""
$lines += "`"$cmake`" -S . -B build -G Ninja " +
          "-DCMAKE_BUILD_TYPE=$Config " +
          "`"-DOCC_QT_PREFIX=$QtPrefix`" " +
          "`"-DCMAKE_MAKE_PROGRAM=$ninja`" " +
          '-DOCC_BUILD_TESTS=ON 2>&1'
$lines += 'if errorlevel 1 (echo CONFIGURE_FAILED & exit /b 1)'
if ($Target -ne '') {
    $lines += "cmake --build build --target `"$Target`" 2>&1"
} else {
    $lines += 'cmake --build build 2>&1'
}
$lines += 'if errorlevel 1 (echo BUILD_FAILED & exit /b 1)'
if ($Tests) {
    # The test executables need the Qt runtime; add the kit's bin directory to
    # PATH for this step so ctest can start them without a deployment pass.
    $lines += "set `"PATH=$QtPrefix/bin;%PATH%`""
    $lines += "`"$ctest`" --test-dir build --output-on-failure --timeout 120 2>&1"
    $lines += 'if errorlevel 1 (echo TESTS_FAILED & exit /b 1)'
}
if ($Portable -or $Run) {
    $lines += 'cmake --build build --target portable'
    $lines += 'if errorlevel 1 (echo PORTABLE_FAILED & exit /b 1)'
}
$lines += 'echo BUILD_STEP_OK'
$lines -join "`r`n" | Out-File -FilePath $bat -Encoding ascii

# --- run it, streaming output into the log --------------------------------
$ErrorActionPreference = 'Continue'
& cmd.exe /d /c "`"$bat`"" 2>&1 | ForEach-Object { Log $_ }
$code = $LASTEXITCODE
Log "=== exit code: $code ==="
if ($code -eq 0 -and $Run) {
    Start-Process -FilePath (Join-Path $buildDir 'dist/OpenCardCanvas.exe') -WorkingDirectory (Join-Path $buildDir 'dist')
}
exit $code
