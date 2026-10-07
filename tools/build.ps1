<#
  tools/build.ps1 - configure, build and test OpenCardCanvas.

  Usage (from anywhere):
      powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1
      powershell ... -File tools\build.ps1 -Clean
      powershell ... -File tools\build.ps1 -Target OpenCardCanvas
      powershell ... -File tools\build.ps1 -Tests

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
    [string] $QtPrefix = 'C:/Qt/6.8.3/msvc2022_64',
    [switch] $Clean,
    [switch] $Tests,
    [switch] $Portable
)

$ErrorActionPreference = 'Continue'
$ProgressPreference    = 'SilentlyContinue'

$root      = Split-Path -Parent $PSScriptRoot
$buildDir  = Join-Path $root 'build'
$logFile   = Join-Path $buildDir 'build.log'
$ninja     = 'C:/Users/HP/AppData/Local/Programs/Python/Python310/Scripts/ninja.exe'
$vcvars    = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir -Force | Out-Null }
if ($Clean -and (Test-Path $buildDir)) {
    Get-ChildItem $buildDir | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
}

function Log([string]$text) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $text
    $line | Out-File -FilePath $logFile -Append -Encoding utf8
}

"" | Out-File -FilePath $logFile -Append -Encoding utf8
Log "=== OpenCardCanvas build start (Config=$Config Target=$Target Tests=$Tests Clean=$Clean) ==="

if (-not (Test-Path $vcvars)) { Log "FATAL: vcvars64.bat not found at $vcvars"; exit 1 }

# --- locate Qt -------------------------------------------------------------
if (-not (Test-Path (Join-Path $QtPrefix 'lib/cmake/Qt6'))) {
    $candidates = @(
        'C:/Qt/6.8.3/msvc2022_64','C:/Qt/6.9.0/msvc2022_64','C:/Qt/6.7.3/msvc2022_64'
    ) | Where-Object { Test-Path (Join-Path $_ 'lib/cmake/Qt6') }
    if ($candidates.Count -eq 0) { Log "FATAL: no Qt6 kit found under $QtPrefix or C:/Qt/<ver>/msvc2022_64"; exit 1 }
    $QtPrefix = $candidates[0]
}
Log "Qt kit: $QtPrefix"

# --- build a helper batch file --------------------------------------------
$bat = Join-Path $buildDir '_build_step.bat'
$lines = @()
$lines += '@echo off'
$lines += "call `"$vcvars`" >nul"
$lines += 'if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)'
$lines += "cd /d `"$root`""
$lines += 'cmake -S . -B build -G Ninja ' +
          "-DCMAKE_BUILD_TYPE=$Config " +
          "-DCMAKE_PREFIX_PATH=$QtPrefix " +
          "-DCMAKE_MAKE_PROGRAM=$ninja " +
          '-DOCC_BUILD_TESTS=ON 2>&1'
$lines += 'if errorlevel 1 (echo CONFIGURE_FAILED & exit /b 1)'
if ($Target -ne '') {
    $lines += "cmake --build build --target $Target 2>&1"
} else {
    $lines += 'cmake --build build 2>&1'
}
$lines += 'if errorlevel 1 (echo BUILD_FAILED & exit /b 1)'
if ($Tests) {
    # The test executables need the Qt runtime; add the kit's bin directory to
    # PATH for this step so ctest can start them without a deployment pass.
    $lines += "set PATH=$QtPrefix/bin;%PATH%"
    $lines += 'ctest --test-dir build --output-on-failure --timeout 120 2>&1'
    $lines += 'if errorlevel 1 (echo TESTS_FAILED & exit /b 1)'
}
if ($Portable) {
    $lines += 'cmake --build build --target portable'
    $lines += 'if errorlevel 1 (echo PORTABLE_FAILED & exit /b 1)'
}
$lines += 'echo BUILD_STEP_OK'
$lines -join "`r`n" | Out-File -FilePath $bat -Encoding ascii

# --- run it, streaming output into the log --------------------------------
& cmd.exe /c $bat 2>&1 | ForEach-Object { Log $_ }
$code = $LASTEXITCODE
Log "=== exit code: $code ==="
exit $code
