Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!ifndef APP_VERSION
!error "APP_VERSION, PAYLOAD_DIR, OUTPUT_FILE and UNINSTALL_LIST are required"
!endif
!ifdef TEST_MODE
!define REG_PREFIX "Software\EinzigViewerInstallerTest\"
!else
!define REG_PREFIX "Software\"
!endif
!define APP_KEY "${REG_PREFIX}EinzigViewer"
!define CLASSES "${REG_PREFIX}Classes"
!define UNINSTALL_KEY "${REG_PREFIX}Microsoft\Windows\CurrentVersion\Uninstall\EinzigViewer"

Name "EinzigViewer ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\EinzigViewer"
InstallDirRegKey HKCU "${APP_KEY}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32
CRCCheck force
AllowSkipFiles off
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "EinzigViewer"
VIAddVersionKey "FileDescription" "EinzigViewer installer"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "EinzigViewer Project"
!define MUI_ICON "..\resources\icon\EinzigViewer.ico"
!define MUI_UNICON "..\resources\icon\EinzigViewer.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\EinzigViewer.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "TradChinese"
!insertmacro MUI_LANGUAGE "English"

LangString CloseViewer ${LANG_TRADCHINESE} "請先關閉這個安裝位置的 EinzigViewer，再重試。"
LangString CloseViewer ${LANG_ENGLISH} "Close EinzigViewer running from this installation folder, then retry."
LangString Unsupported ${LANG_TRADCHINESE} "EinzigViewer 需要 64 位元 Windows 10 或更新版本。"
LangString Unsupported ${LANG_ENGLISH} "EinzigViewer requires 64-bit Windows 10 or newer."
LangString PortableFolder ${LANG_TRADCHINESE} "這個資料夾包含免安裝版，請另選安裝資料夾，以保留原有設定。"
LangString PortableFolder ${LANG_ENGLISH} "This folder contains a portable edition. Choose another installation folder to keep its settings."

!macro CheckViewerClosed PREFIX
Function ${PREFIX}CheckViewerClosed
    IfFileExists "$INSTDIR\EinzigViewer.exe" 0 done
retry:
    System::Call 'kernel32::CreateFileW(w "$INSTDIR\EinzigViewer.exe", i 0x40000000, i 7, p 0, i 3, i 0, p 0) p .r0'
    ${If} $0 == -1
        IfSilent failed
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(CloseViewer)" IDRETRY retry
failed:
        SetErrorLevel 2
        Abort
    ${EndIf}
    System::Call 'kernel32::CloseHandle(p r0)'
done:
FunctionEnd
!macroend
!insertmacro CheckViewerClosed ""
!insertmacro CheckViewerClosed "un."

Function .onInit
    SetShellVarContext current
    SetRegView 64
    ${IfNot} ${RunningX64}
    ${OrIfNot} ${AtLeastWin10}
        MessageBox MB_OK|MB_ICONSTOP "$(Unsupported)"
        SetErrorLevel 3
        Abort
    ${EndIf}
FunctionEnd
Function un.onInit
    SetShellVarContext current
    SetRegView 64
FunctionEnd

!macro RegisterExtension EXT PROGID
    WriteRegStr HKCU "${APP_KEY}\Capabilities\FileAssociations" "${EXT}" "${PROGID}"
    WriteRegStr HKCU "${CLASSES}\${EXT}\OpenWithProgids" "${PROGID}" ""
    WriteRegStr HKCU "${CLASSES}\Applications\EinzigViewer.exe\SupportedTypes" "${EXT}" ""
!macroend
!macro UnregisterExtension EXT PROGID
    DeleteRegValue HKCU "${CLASSES}\${EXT}\OpenWithProgids" "${PROGID}"
    DeleteRegKey /ifempty HKCU "${CLASSES}\${EXT}\OpenWithProgids"
    DeleteRegKey /ifempty HKCU "${CLASSES}\${EXT}"
!macroend
!macro EachExtension ACTION
    !insertmacro ${ACTION} ".jpg" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".jpeg" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".jpe" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".png" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".bmp" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".dib" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".gif" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".tif" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".tiff" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".webp" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".heic" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".heif" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".avif" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".jxl" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".ico" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".wdp" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".jxr" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".hdp" "EinzigViewer.Image"
    !insertmacro ${ACTION} ".epub" "EinzigViewer.Epub"
!macroend

Section "EinzigViewer"
    IfFileExists "$INSTDIR\portable.ini" 0 install
        IfSilent rejectPortable
        MessageBox MB_OK|MB_ICONEXCLAMATION "$(PortableFolder)"
rejectPortable:
        SetErrorLevel 5
        Abort
install:
    Call CheckViewerClosed
    SetOutPath "$INSTDIR"
    SetOverwrite on
    File /r "${PAYLOAD_DIR}\*.*"
    WriteINIStr "$INSTDIR\installed.ini" "installation" "version" "${APP_VERSION}"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${APP_KEY}\Capabilities" "ApplicationName" "EinzigViewer"
    WriteRegStr HKCU "${APP_KEY}\Capabilities" "ApplicationDescription" "Image and EPUB viewer with NVIDIA RTX enhancement"
    WriteRegStr HKCU "${APP_KEY}\Capabilities" "ApplicationIcon" '"$INSTDIR\EinzigViewer.exe",0'
    WriteRegStr HKCU "${REG_PREFIX}RegisteredApplications" "EinzigViewer" "${APP_KEY}\Capabilities"
    WriteRegStr HKCU "${CLASSES}\EinzigViewer.Image" "" "EinzigViewer Image"
    WriteRegStr HKCU "${CLASSES}\EinzigViewer.Epub" "" "EinzigViewer EPUB"
    WriteRegStr HKCU "${CLASSES}\EinzigViewer.Image\DefaultIcon" "" '"$INSTDIR\EinzigViewer.exe",0'
    WriteRegStr HKCU "${CLASSES}\EinzigViewer.Epub\DefaultIcon" "" '"$INSTDIR\EinzigViewer.exe",0'
    WriteRegStr HKCU "${CLASSES}\EinzigViewer.Image\shell\open\command" "" '"$INSTDIR\EinzigViewer.exe" "%1"'
    WriteRegStr HKCU "${CLASSES}\EinzigViewer.Epub\shell\open\command" "" '"$INSTDIR\EinzigViewer.exe" "%1"'
    WriteRegStr HKCU "${CLASSES}\Applications\EinzigViewer.exe" "FriendlyAppName" "EinzigViewer"
    WriteRegStr HKCU "${CLASSES}\Applications\EinzigViewer.exe\shell\open\command" "" '"$INSTDIR\EinzigViewer.exe" "%1"'
    !insertmacro EachExtension RegisterExtension
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "EinzigViewer"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "EinzigViewer Project"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\EinzigViewer.exe,0"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/akaJooooooker/EinzigViewer"
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    !ifdef TEST_MODE
        CreateDirectory "$INSTDIR\TestShortcuts"
        CreateShortcut "$INSTDIR\TestShortcuts\EinzigViewer.lnk" "$INSTDIR\EinzigViewer.exe"
    !else
        CreateDirectory "$SMPROGRAMS\EinzigViewer"
        CreateShortcut "$SMPROGRAMS\EinzigViewer\EinzigViewer.lnk" "$INSTDIR\EinzigViewer.exe"
        CreateShortcut "$SMPROGRAMS\EinzigViewer\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    !endif
SectionEnd

Section "Uninstall"
    Call un.CheckViewerClosed
    IfFileExists "$INSTDIR\installed.ini" 0 invalid
    ReadRegStr $0 HKCU "${APP_KEY}" "InstallDir"
    ${If} $0 == $INSTDIR
        !insertmacro EachExtension UnregisterExtension
        DeleteRegKey HKCU "${CLASSES}\EinzigViewer.Image"
        DeleteRegKey HKCU "${CLASSES}\EinzigViewer.Epub"
        DeleteRegKey HKCU "${CLASSES}\Applications\EinzigViewer.exe"
        DeleteRegValue HKCU "${REG_PREFIX}RegisteredApplications" "EinzigViewer"
        DeleteRegKey /ifempty HKCU "${REG_PREFIX}RegisteredApplications"
        DeleteRegKey HKCU "${UNINSTALL_KEY}"
        DeleteRegKey HKCU "${APP_KEY}"
        !ifndef TEST_MODE
            Delete "$SMPROGRAMS\EinzigViewer\EinzigViewer.lnk"
            Delete "$SMPROGRAMS\EinzigViewer\Uninstall.lnk"
            RMDir "$SMPROGRAMS\EinzigViewer"
            System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
        !endif
    ${EndIf}
    !ifdef TEST_MODE
        Delete "$INSTDIR\TestShortcuts\EinzigViewer.lnk"
        RMDir "$INSTDIR\TestShortcuts"
    !endif
    !include "${UNINSTALL_LIST}"
    Delete "$INSTDIR\installed.ini"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    Goto done
invalid:
    SetErrorLevel 4
    Abort
done:
SectionEnd
