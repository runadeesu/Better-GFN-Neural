; Better GFN Neural - per-user installer (no administrator rights required)
; Build: makensis /DVERSION=1.0.0 /DSRCDIR=<staging dir> /DOUTFILE=<path>\BetterGFNNeuralSetup.exe BetterGFNNeural.nsi

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef SRCDIR
  !define SRCDIR "..\dist\stage"
!endif
!ifndef OUTFILE
  !define OUTFILE "..\dist\BetterGFNNeuralSetup.exe"
!endif

!define APPNAME "Better GFN Neural"
!define APPEXE "BetterGFNNeural.exe"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\BetterGFNNeural"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Better GFN Neural"
InstallDirRegKey HKCU "Software\BetterGFNNeural" "InstallDir"
BrandingText "${APPNAME} ${VERSION} - unofficial GeForce NOW companion"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "MIT License. Not affiliated with NVIDIA."

!include "MUI2.nsh"
!define MUI_ICON "${SRCDIR}\app.ico"
!define MUI_UNICON "${SRCDIR}\app.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APPEXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APPNAME}"
!define MUI_WELCOMEPAGE_TEXT "This will install ${APPNAME} ${VERSION}.$\r$\n$\r$\nBetter GFN Neural is an unofficial companion app that enhances the GeForce NOW picture locally on your GPU. It is not affiliated with or endorsed by NVIDIA.$\r$\n$\r$\nNo administrator rights are needed."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Japanese"

Function CloseRunningApp
  nsExec::Exec 'taskkill /IM ${APPEXE} /T'
  Sleep 800
  nsExec::Exec 'taskkill /IM ${APPEXE} /F /T'
FunctionEnd

Function un.CloseRunningApp
  nsExec::Exec 'taskkill /IM ${APPEXE} /T'
  Sleep 800
  nsExec::Exec 'taskkill /IM ${APPEXE} /F /T'
FunctionEnd

Section "Better GFN Neural" SecMain
  SectionIn RO
  Call CloseRunningApp
  SetOutPath "$INSTDIR"
  File "${SRCDIR}\${APPEXE}"
  File "${SRCDIR}\README.md"
  File "${SRCDIR}\LICENSE.txt"
  File "${SRCDIR}\THIRD_PARTY_NOTICES.md"
  File "${SRCDIR}\CHANGELOG.md"
  File "${SRCDIR}\app.ico"
  SetOutPath "$INSTDIR\models"
  File /r "${SRCDIR}\models\*.*"
  SetOutPath "$INSTDIR\shaders"
  File /r "${SRCDIR}\shaders\*.*"
  SetOutPath "$INSTDIR\docs"
  File /r "${SRCDIR}\docs\*.*"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0

  WriteRegStr HKCU "Software\BetterGFNNeural" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "Better GFN Neural contributors"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${APPEXE}"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Call un.CloseRunningApp
  ; "Start with Windows" entry
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "BetterGFNNeural"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APPNAME}"
  Delete "$INSTDIR\${APPEXE}"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.md"
  Delete "$INSTDIR\CHANGELOG.md"
  Delete "$INSTDIR\app.ico"
  RMDir /r "$INSTDIR\models"
  RMDir /r "$INSTDIR\shaders"
  RMDir /r "$INSTDIR\docs"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "Software\BetterGFNNeural"
  MessageBox MB_YESNO|MB_ICONQUESTION "Also remove your Better GFN Neural settings, game profiles and logs?" /SD IDNO IDNO keep
    RMDir /r "$LOCALAPPDATA\BetterGFNNeural"
  keep:
SectionEnd
