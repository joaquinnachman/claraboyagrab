; Instalador de Claraboya Grab (Windows). Lo compila GitHub Actions con Inno Setup 6:
;   ISCC /DAppVersion=1.3.0 /DBuildNumber=12 installer\ClaraboyaGrab.iss
; Instala el plugin en la carpeta VST3 común y el programa suelto en Archivos de programa.
; Si ya estaba instalado, lo actualiza (avisa si Cubase o la app tienen los archivos abiertos).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildNumber
  #define BuildNumber "0"
#endif

#define AppName "Claraboya Grab"
#define SrcDir  "..\build\ClaraboyaGrab_artefacts\Release"

[Setup]
AppId={{6FEA03A0-5107-44B3-B31F-9BE3AEDC995F}
AppName={#AppName}
AppVersion={#AppVersion} (build {#BuildNumber})
AppVerName={#AppName} {#AppVersion} (build {#BuildNumber})
AppPublisher=Claraboya Estudio
AppPublisherURL=https://claraboyaestudio.netlify.app
AppSupportURL=https://github.com/joaquinnachman/claraboyagrab
VersionInfoVersion={#AppVersion}.{#BuildNumber}
DefaultDirName={autopf}\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\salida
OutputBaseFilename=ClaraboyaGrab-Instalador
SetupIconFile=..\Resources\brand\icono.ico
UninstallDisplayIcon={app}\{#AppName}.exe
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
; Si Cubase o la app tienen abierto el plugin/programa, el instalador lo avisa
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll,*.vst3
RestartApplications=no

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "escritorio"; Description: "Crear un acceso directo en el escritorio"; Flags: unchecked

[Files]
Source: "{#SrcDir}\Standalone\{#AppName}.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs restartreplace uninsrestartdelete

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppName}.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Tasks: escritorio

[Run]
Filename: "{app}\{#AppName}.exe"; Description: "Abrir {#AppName}"; Flags: nowait postinstall skipifsilent

[Messages]
es.FinishedLabel=Listo. El plugin quedó en la carpeta VST3 (en Cubase: Studio > VST Plug-in Manager > actualizar) y el programa suelto en el menú Inicio.
