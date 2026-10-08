; ---------------------------------------------------------------------------
;  OpenCardCanvas - Inno Setup script
;
;  Build:
;      cmake --build build --target portable          (produces the Qt runtime)
;      "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\OpenCardCanvas.iss
;
;  Produces installer\Output\OpenCardCanvas_Setup.exe
;
;  The Entrust / Datacard printer driver is deliberately NOT bundled: it is
;  licensed separately and must be installed by the user. The installer states
;  this requirement during setup.
; ---------------------------------------------------------------------------

#define AppName        "OpenCardCanvas"
#define AppVersion     "1.0.1"
#define AppPublisher   "OpenCardCanvas"
#define AppExeName     "OpenCardCanvas.exe"
#ifndef SourceDir
#define SourceDir      "..\build\dist"
#endif

[Setup]
AppId={{8B2E5C1A-4F3D-4A6E-9C21-7D5B3E9A1C44}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName} {#AppVersion}
SetupIconFile=..\resources\win\app.ico
OutputDir=Output
OutputBaseFilename=OpenCardCanvas_Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
LicenseFile=..\LICENSE
InfoAfterFile=..\docs\INSTALLATION.md
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} - ID card design and printing
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "occardassoc"; Description: "Associate .occard project files with {#AppName}"; GroupDescription: "File associations:"

[Files]
; Application + Qt runtime (windeployqt output, produced by the "portable" target)
Source: "{#SourceDir}\{#AppExeName}";      DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\*.dll";              DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\platforms\*";        DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\imageformats\*";     DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\iconengines\*";      DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\styles\*";           DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\sqldrivers\*";       DestDir: "{app}\sqldrivers"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\generic\*";          DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\translations\*";     DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\tls\*";              DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Documentation shipped with the product
Source: "..\README.md";                    DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\LICENSE";                      DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\*.md";                    DestDir: "{app}\docs"; Flags: ignoreversion

; Product-supplied templates (a small starter set, no sample cardholder data)
Source: "..\resources\templates\*";        DestDir: "{app}\templates"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}";                      Filename: "{app}\{#AppExeName}"
Name: "{group}\{#AppName} - Printer setup guide"; Filename: "{app}\docs\PRINTER_SETUP.md"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";                Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; Register the .occard project file type (per user when not elevated)
Root: HKA; Subkey: "Software\Classes\.occard"; ValueType: string; ValueName: ""; ValueData: "OpenCardCanvas.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\OpenCardCanvas.Project"; ValueType: string; ValueName: ""; ValueData: "OpenCardCanvas Card Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\OpenCardCanvas.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName},0"
Root: HKA; Subkey: "Software\Classes\OpenCardCanvas.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".occard"; ValueData: ""; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Only the Qt runtime and docs are removed; user projects, templates, settings and
; logs live under the user profile and are deliberately left in place.
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\sqldrivers"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\translations"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\docs"
Type: filesandordirs; Name: "{app}\templates"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    { Nothing to do: the printer driver is installed separately by the user.
      The note below is shown by InfoAfterFile. }
  end;
end;
