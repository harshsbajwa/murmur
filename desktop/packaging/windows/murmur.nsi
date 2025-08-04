; Murmur Desktop NSIS Installer Script
!define APPNAME "Murmur Desktop"
!define COMPANYNAME "Murmur"
!define DESCRIPTION "P2P Video Transcription Application"
!define VERSIONMAJOR 1
!define VERSIONMINOR 0
!define VERSIONBUILD 0
!define HELPURL "https://murmur.app/help"
!define UPDATEURL "https://murmur.app/update"
!define ABOUTURL "https://murmur.app"
!define INSTALLSIZE 150000

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

Name "${APPNAME}"
OutFile "MurmurDesktop-${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}-Setup.exe"
InstallDir "$PROGRAMFILES64\${COMPANYNAME}\${APPNAME}"
InstallDirRegKey HKLM "Software\${COMPANYNAME}\${APPNAME}" "InstallPath"
RequestExecutionLevel admin

VIProductVersion "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "CompanyName" "${COMPANYNAME}"
VIAddVersionKey "LegalCopyright" "© 2025 ${COMPANYNAME}. All rights reserved."
VIAddVersionKey "FileDescription" "${DESCRIPTION}"
VIAddVersionKey "FileVersion" "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}.0"
VIAddVersionKey "ProductVersion" "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}.0"

!define MUI_ABORTWARNING
!define MUI_ICON "..\..\resources\icons\app.ico"
!define MUI_UNICON "..\..\resources\icons\app.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "..\..\resources\images\installer-welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "..\..\resources\images\installer-welcome.bmp"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

InstType "Full Installation"
InstType "Minimal Installation"

Function .onInit
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_OK|MB_ICONSTOP "This application requires Windows 10 or later."
        Abort
    ${EndIf}
    
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "This application requires a 64-bit version of Windows."
        Abort
    ${EndIf}
    
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString"
    StrCmp $R0 "" done
    
    MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION "${APPNAME} is already installed. $\n$\nClick OK to remove the previous version or Cancel to cancel this upgrade." IDOK uninst
    Abort
    
    uninst:
        ClearErrors
        ExecWait '$R0 _?=$INSTDIR'
        
        IfErrors no_remove_uninstaller done
        no_remove_uninstaller:
    
    done:
FunctionEnd

Section "Murmur Desktop" SecMain
    SectionIn 1 2 RO
    
    SetOutPath "$INSTDIR"
    
    File "MurmurDesktop.exe"
    File /r "*.dll"
    File /r "platforms\"
    File /r "imageformats\"
    File /r "multimedia\"
    File /r "qml\"
    
    WriteUninstaller "$INSTDIR\uninstall.exe"
    
    WriteRegStr HKLM "Software\${COMPANYNAME}\${APPNAME}" "InstallPath" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayIcon" "$INSTDIR\MurmurDesktop.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Publisher" "${COMPANYNAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "HelpLink" "${HELPURL}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "URLUpdateInfo" "${UPDATEURL}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "URLInfoAbout" "${ABOUTURL}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "VersionMajor" ${VERSIONMAJOR}
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "VersionMinor" ${VERSIONMINOR}
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoRepair" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "EstimatedSize" ${INSTALLSIZE}
SectionEnd

Section "Desktop Shortcut" SecDesktop
    SectionIn 1
    CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\MurmurDesktop.exe"
SectionEnd

Section "Start Menu Shortcuts" SecStartMenu
    SectionIn 1 2
    CreateDirectory "$SMPROGRAMS\${COMPANYNAME}"
    CreateShortCut "$SMPROGRAMS\${COMPANYNAME}\${APPNAME}.lnk" "$INSTDIR\MurmurDesktop.exe"
    CreateShortCut "$SMPROGRAMS\${COMPANYNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section "File Associations" SecFileAssoc
    SectionIn 1
    
    WriteRegStr HKCR ".mp4\OpenWithList\MurmurDesktop.exe" "" ""
    WriteRegStr HKCR ".avi\OpenWithList\MurmurDesktop.exe" "" ""
    WriteRegStr HKCR ".mkv\OpenWithList\MurmurDesktop.exe" "" ""
    WriteRegStr HKCR ".mov\OpenWithList\MurmurDesktop.exe" "" ""
    WriteRegStr HKCR ".wmv\OpenWithList\MurmurDesktop.exe" "" ""
    WriteRegStr HKCR ".flv\OpenWithList\MurmurDesktop.exe" "" ""
    WriteRegStr HKCR ".webm\OpenWithList\MurmurDesktop.exe" "" ""
    WriteRegStr HKCR ".m4v\OpenWithList\MurmurDesktop.exe" "" ""
    
    System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Core application files (required)"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create a desktop shortcut"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Create Start Menu shortcuts"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecFileAssoc} "Associate video files with Murmur Desktop"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    Delete "$INSTDIR\MurmurDesktop.exe"
    Delete "$INSTDIR\*.dll"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\multimedia"
    RMDir /r "$INSTDIR\qml"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    
    Delete "$DESKTOP\${APPNAME}.lnk"
    Delete "$SMPROGRAMS\${COMPANYNAME}\${APPNAME}.lnk"
    Delete "$SMPROGRAMS\${COMPANYNAME}\Uninstall ${APPNAME}.lnk"
    RMDir "$SMPROGRAMS\${COMPANYNAME}"
    
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
    DeleteRegKey HKLM "Software\${COMPANYNAME}\${APPNAME}"
    DeleteRegKey /ifempty HKLM "Software\${COMPANYNAME}"
    
    DeleteRegKey HKCR ".mp4\OpenWithList\MurmurDesktop.exe"
    DeleteRegKey HKCR ".avi\OpenWithList\MurmurDesktop.exe"
    DeleteRegKey HKCR ".mkv\OpenWithList\MurmurDesktop.exe"
    DeleteRegKey HKCR ".mov\OpenWithList\MurmurDesktop.exe"
    DeleteRegKey HKCR ".wmv\OpenWithList\MurmurDesktop.exe"
    DeleteRegKey HKCR ".flv\OpenWithList\MurmurDesktop.exe"
    DeleteRegKey HKCR ".webm\OpenWithList\MurmurDesktop.exe"
    DeleteRegKey HKCR ".m4v\OpenWithList\MurmurDesktop.exe"
    
    System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'
SectionEnd