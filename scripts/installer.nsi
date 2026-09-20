Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
Name "Compositor Windows (development)"
OutFile "${OUTPUT_DIR}\CompositorWindows-${APP_VERSION}-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\CompositorWindows"
RequestExecutionLevel user
SetCompressor /SOLID zlib
BrandingText "Compositor Windows development build"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Function .onVerifyInstDir
 IfFileExists "$INSTDIR\install.json" 0 +2
 Abort
FunctionEnd
Section "Compositor Windows"
 SetShellVarContext current
 IfFileExists "$INSTDIR\install.json" 0 +3
 MessageBox MB_ICONSTOP "An installation already exists here. Choose an empty directory or use the verified updater."
 Abort
 SetOutPath "$INSTDIR"
 File /r "${PACKAGE_DIR}\*"
 WriteUninstaller "$INSTDIR\Uninstall.exe"
 CreateShortcut "$SMPROGRAMS\Compositor Windows.lnk" "$INSTDIR\CompositorLauncher.exe"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows" "DisplayName" "Compositor Windows (development)"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows" "DisplayVersion" "${APP_VERSION}"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows" "InstallLocation" "$INSTDIR"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows" "DisplayIcon" "$INSTDIR\CompositorLauncher.exe"
 WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows" "NoModify" 1
 WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows" "NoRepair" 1
 WriteRegStr HKCU "Software\Classes\Directory\shell\CompositorWindows" "" "Open in Compositor"
 WriteRegStr HKCU "Software\Classes\Directory\shell\CompositorWindows" "AppliesTo" 'System.FileName:~>$\".comp$\"'
 WriteRegStr HKCU "Software\Classes\Directory\shell\CompositorWindows" "MultiSelectModel" "Single"
 WriteRegStr HKCU "Software\Classes\Directory\shell\CompositorWindows" "Icon" "$INSTDIR\CompositorLauncher.exe"
 WriteRegStr HKCU "Software\Classes\Directory\shell\CompositorWindows\command" "" '$\"$INSTDIR\CompositorLauncher.exe$\" $\"%1$\"'
SectionEnd
Section "Uninstall"
 SetShellVarContext current
 ExecWait '$\"$INSTDIR\CompositorUpdater.exe$\" --uninstall-payloads --root $\"$INSTDIR$\" --allow-test-key' $0
 ${If} $0 != 0
  MessageBox MB_ICONSTOP "Verified payload cleanup failed. Installation files have been retained."
  Abort
 ${EndIf}
 !include "${OUTPUT_DIR}\uninstall-root.nsh"
 Delete "$INSTDIR\state\active.json"
 Delete "$INSTDIR\state\update.lock"
 RMDir "$INSTDIR\state"
 RMDir "$INSTDIR\versions"
 Delete "$INSTDIR\Uninstall.exe"
 RMDir "$INSTDIR"
 ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows" "InstallLocation"
 ${If} $0 == $INSTDIR
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows"
  DeleteRegKey HKCU "Software\Classes\Directory\shell\CompositorWindows"
  Delete "$SMPROGRAMS\Compositor Windows.lnk"
 ${EndIf}
SectionEnd
