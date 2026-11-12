Unicode true

!include "MUI2.nsh"

!define APP_VERSION "$%REPS2_APP_VERSION%"
!define APP_FILE_VERSION "$%REPS2_APP_FILE_VERSION%"
!define PUBLISH_DIR "$%REPS2_PUBLISH_DIR%"
!define OUTPUT_DIR "$%REPS2_OUTPUT_DIR%"

!if "${APP_VERSION}" == ""
  !error "REPS2_APP_VERSION must be set before compiling the installer"
!endif
!if "${APP_FILE_VERSION}" == ""
  !error "REPS2_APP_FILE_VERSION must be set before compiling the installer"
!endif
!if "${PUBLISH_DIR}" == ""
  !error "REPS2_PUBLISH_DIR must be set before compiling the installer"
!endif
!if "${OUTPUT_DIR}" == ""
  !error "REPS2_OUTPUT_DIR must be set before compiling the installer"
!endif

Name "REPS2 Desktop Alpha"
Caption "REPS2 Desktop Alpha Setup"
OutFile "${OUTPUT_DIR}\REPS2-Setup-windows-x64.exe"
InstallDir "$LOCALAPPDATA\Programs\REPS2"
RequestExecutionLevel user
; Prevent Windows from bitmap-scaling the installer UI on high-DPI displays.
; This keeps wizard text crisp at non-100% Windows display scaling factors.
ManifestDPIAware true
SetCompressor /SOLID zlib
ShowInstDetails show
ShowUninstDetails show
BrandingText "RidTheWann - REPS2"

VIProductVersion "${APP_FILE_VERSION}"
VIFileVersion "${APP_FILE_VERSION}"
VIAddVersionKey "ProductName" "REPS2 Desktop Alpha"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "FileVersion" "${APP_FILE_VERSION}"
VIAddVersionKey "FileDescription" "REPS2 Desktop Alpha Windows x64 installer"
VIAddVersionKey "CompanyName" "RidTheWann"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026 RidTheWann"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Welcome to REPS2 Desktop Alpha Setup"
!define MUI_WELCOMEPAGE_TEXT "This wizard installs REPS2 Desktop Alpha for the current Windows user. If the Microsoft Visual C++ x64 runtime is missing, Windows may request administrator approval to install that shared prerequisite. The installer includes the desktop application, REPS2 SDK bundle, update helper and runtime prerequisite. It does not include PS2 game images or BIOS files."
!define MUI_LICENSEPAGE_TEXT_TOP "Review the REPS2 license before continuing."
!define MUI_FINISHPAGE_RUN "$INSTDIR\REPS2.Desktop.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch REPS2 Desktop"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PUBLISH_DIR}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function EnsureVCRuntime
  SetRegView 64
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  StrCmp $0 1 vc_runtime_ready

  DetailPrint "Checking Microsoft Visual C++ x64 runtime prerequisite..."
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File "${PUBLISH_DIR}\prerequisites\vc_redist.x64.exe"
  ExecShellWait "runas" "$PLUGINSDIR\vc_redist.x64.exe" "/install /quiet /norestart" SW_HIDE
  Delete "$PLUGINSDIR\vc_redist.x64.exe"

  ; ExecShellWait does not return a process exit code. Verify the runtime key
  ; rather than assuming that a returned ShellExecute call means installation succeeded.
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  StrCmp $0 1 vc_runtime_ready
  MessageBox MB_ICONSTOP|MB_OK "Microsoft Visual C++ x64 Runtime could not be installed. Approve the Windows prompt and try again."
  Abort

vc_runtime_ready:
FunctionEnd

Function .onInit
  SetRegView 64
  SetShellVarContext current
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext current
  ReadRegStr $INSTDIR HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "InstallLocation"
  StrCmp $INSTDIR "" 0 +2
    StrCpy $INSTDIR "$LOCALAPPDATA\Programs\REPS2"
FunctionEnd

Section "REPS2 Desktop (required)" SEC_MAIN
  SectionIn RO
  Call EnsureVCRuntime
  SetOutPath "$INSTDIR"
  File /r "${PUBLISH_DIR}\*.*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\REPS2"
  CreateShortCut "$SMPROGRAMS\REPS2\REPS2 Desktop.lnk" "$INSTDIR\REPS2.Desktop.exe" "" "$INSTDIR\REPS2.Desktop.exe"
  CreateShortCut "$SMPROGRAMS\REPS2\Uninstall REPS2.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\RidTheWann\REPS2" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "DisplayName" "REPS2 Desktop Alpha"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "Publisher" "RidTheWann"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "DisplayIcon" "$INSTDIR\REPS2.Desktop.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "URLInfoAbout" "https://github.com/RidTheWann/REPS2-Project"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop" "NoRepair" 1
SectionEnd

Section /o "Create a desktop shortcut" SEC_DESKTOP
  CreateShortCut "$DESKTOP\REPS2 Desktop.lnk" "$INSTDIR\REPS2.Desktop.exe" "" "$INSTDIR\REPS2.Desktop.exe"
SectionEnd

Section "Uninstall"
  ; Capture the install location in un.onInit before removing its registry entry.
  SetOutPath "$TEMP"
  Delete "$SMPROGRAMS\REPS2\REPS2 Desktop.lnk"
  Delete "$SMPROGRAMS\REPS2\Uninstall REPS2.lnk"
  RMDir "$SMPROGRAMS\REPS2"
  Delete "$DESKTOP\REPS2 Desktop.lnk"

  ; Explicitly remove the application entry point, then recursively remove the package.
  Delete "$INSTDIR\REPS2.Desktop.exe"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\REPS2Desktop"
  DeleteRegKey HKCU "Software\RidTheWann\REPS2"
  RMDir /r "$INSTDIR"
SectionEnd
