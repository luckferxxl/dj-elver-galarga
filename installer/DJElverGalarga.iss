; Instalador de DJ Elver Galarga (antes Ramiro DJ) (Inno Setup 6). Se compila desde compilar.ps1.
#define AppName "DJ Elver Galarga"
#define AppVersion "0.11.1"
#define AppExe "DJElverGalarga.exe"

[Setup]
AppId={{6B1C3B0E-4C59-4E8B-9F1A-52D3A7C0D911}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=DJ Elver Galarga
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=DJElverGalarga-Setup-{#AppVersion}
SetupIconFile=..\res\djelvergalarga.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; Permite instalar solo para el usuario actual sin permisos de administrador.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
LicenseFile=..\COPYING.txt

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"

[Files]
Source: "..\build\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LEEME.md"; DestDir: "{app}"; DestName: "LEEME.txt"; Flags: ignoreversion isreadme
Source: "..\licencias\*"; DestDir: "{app}\licencias"; Flags: ignoreversion
Source: "..\COPYING.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\RACK.md"; DestDir: "{app}"; DestName: "RACK.txt"; Flags: ignoreversion

[InstallDelete]
; La versión con el nombre anterior.
Type: files; Name: "{app}\RamiroDJ.exe"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Abrir {#AppName}"; Flags: nowait postinstall skipifsilent

; La biblioteca, las preferencias y las asignaciones MIDI (%APPDATA%\RamiroDJ, la misma carpeta de antes)
; y las grabaciones (Música\DJ Elver Galarga - Grabaciones) se conservan al desinstalar.
