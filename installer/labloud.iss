; Instalador Windows — LABLOUD by Piradex (VST3, 64 bits)
[Setup]
AppId={{6C1F6B7A-2E54-4B8E-9A3C-5D0A1B2C3D4E}
AppName=LABLOUD by Piradex
AppVersion=0.2.0
AppPublisher=Piradex
AppPublisherURL=https://www.beatfreakstudio.com
DefaultDirName={autopf}\Piradex\LABLOUD
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..
OutputBaseFilename=LABLOUD-Setup-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "..\build\Loudnex_artefacts\Release\VST3\LABLOUD by Piradex.vst3\*"; DestDir: "{commoncf64}\VST3\LABLOUD by Piradex.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[Messages]
FinishedLabel=O LABLOUD by Piradex foi instalado na pasta VST3 do sistema. Reinicie o seu DAW e faça um novo scan de plugins.
