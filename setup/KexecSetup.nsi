# WinKexec: kexec for Windows
# Copyright (C) 2008-2009 John Stumpo
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

!include "..\revtag\revtag.nsh"
!include ".\EnvVarUpdate.nsh"

!include "MUI2.nsh"

Name "WinKexec"
OutFile KexecSetup.exe

InstallDir "$PROGRAMFILES\WinKexec"
InstallDirRegKey HKLM "Software\WinKexec" InstallRoot

SetCompressor /SOLID lzma

RequestExecutionLevel admin

!define MUI_ABORTWARNING
!define MUI_UNABORTWARNING
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_UNFINISHPAGE_NOAUTOCLOSE
!define MUI_LICENSEPAGE_RADIOBUTTONS

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${RES_VERSION_STR}"
VIAddVersionKey /LANG=1033 "CompanyName" "John Stumpo"
VIAddVersionKey /LANG=1033 "FileDescription" "WinKexec Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION_STR}"
VIAddVersionKey /LANG=1033 "InternalName" "KexecSetup.exe"
VIAddVersionKey /LANG=1033 "LegalCopyright" "� 2008-2009 John Stumpo.  GNU GPL v3 or later."
VIAddVersionKey /LANG=1033 "OriginalFilename" "KexecSetup.exe"
VIAddVersionKey /LANG=1033 "ProductName" "WinKexec"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION_STR}"

ShowInstDetails show
ShowUninstDetails show

Function .onInit
  # Allow only one instance at a time.
  System::Call 'kernel32::CreateMutexA(i 0, i 0, t "KexecInstallerMutex") i .r1 ?e'
  Pop $R0
  StrCmp $R0 0 +3
    MessageBox MB_OK|MB_ICONSTOP "WinKexec Setup is already running." /SD IDOK
    Abort
FunctionEnd

Function un.onInit
  # Allow only one instance at a time.
  System::Call 'kernel32::CreateMutexA(i 0, i 0, t "KexecUninstallerMutex") i .r1 ?e'
  Pop $R0
  StrCmp $R0 0 +3
    MessageBox MB_OK|MB_ICONSTOP "WinKexec Uninstall is already running." /SD IDOK
    Abort
FunctionEnd

Section "Kexec"
  SectionIn RO
  # First, check if WinKexec is already installed, and, if so,
  # figure out how to uninstall it.
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinKexec" "UninstallString"
  IfErrors NoKexecUninstall
  # Remove it by doing what it wrote into the Registry.
  DetailPrint "Found an existing installation of WinKexec on the system."
  DetailPrint "Removing it before installing this one."
  ExpandEnvStrings $1 $0
  ExecWait "$1 /S _?=$INSTDIR"
  Delete $1
  Goto DoneKexecUninstall
  # No existing installation.
NoKexecUninstall:
  ClearErrors
  # Now WinKexec is not on the system.
  # (Either it wasn't there, or we nuked it ourselves.)
DoneKexecUninstall:
  SetOutPath $INSTDIR
  File ..\clientdll\KexecCommon.dll
  File ..\cliclient\kexec.exe
  File ..\guiclient\KexecGui.exe
  File KexecDriver.exe
  # Install the driver.
  ExecWait "$\"$INSTDIR\KexecDriver.exe$\" /S"
  WriteUninstaller "$INSTDIR\KexecUninstall.exe"
  # Make our InstallDirRegKey (and thus, the uninstaller!) useful.
  WriteRegStr HKLM "Software\WinKexec" InstallRoot $INSTDIR
  # Add us to Add/Remove Programs.
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinKexec" "DisplayName" "WinKexec ${VERSION_STR}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinKexec" "UninstallString" "$\"$INSTDIR\KexecUninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinKexec" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinKexec" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinKexec" "NoRepair" 1
  # Add us to the PATH.
  ${EnvVarUpdate} $0 PATH A HKLM $INSTDIR
SectionEnd

Section /o "Developer Components"
  SetOutPath $INSTDIR\devel
  File ..\include\kexec.h
  File ..\include\KexecCommon.h
  File ..\clientdll\KexecCommon.lib
SectionEnd

Section "Uninstall"
  # Unload the driver.
  ExecWait "$\"$INSTDIR\kexec.exe$\" /u"
  # Check if the driver is there.
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Kexec" "UninstallString"
  IfErrors NoDriverUninstall
  # Remove it.
  ExecWait "$\"$SYSDIR\KexecDriverUninstall.exe$\" /S _?=$SYSDIR"
  Delete "$SYSDIR\KexecDriverUninstall.exe"
  Goto DoneDriverUninstall
  # No driver was installed.
NoDriverUninstall:
  DetailPrint "Not uninstalling driver because no kexec driver is installed."
  ClearErrors
  # The driver is not longer on the system.
  # (Either it wasn't there, or we nuked it ourselves.)
DoneDriverUninstall:
  # Remove us from the PATH.
  ${un.EnvVarUpdate} $0 PATH R HKLM $INSTDIR
  Delete $INSTDIR\devel\kexec.h
  Delete $INSTDIR\devel\KexecCommon.h
  Delete $INSTDIR\devel\KexecCommon.lib
  RmDir $INSTDIR\devel
  Delete $INSTDIR\kexec.exe
  Delete $INSTDIR\KexecGui.exe
  Delete $INSTDIR\KexecCommon.dll
  Delete $INSTDIR\KexecDriver.exe
  # Our InstallDirRegKey is no longer useful.
  DeleteRegValue HKLM "Software\WinKexec" InstallRoot
  # Kill the Registry key with our settings.
  DeleteRegKey /ifempty HKLM "Software\WinKexec"
  # Kill the Registry key with our Add/Remove Programs data.
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinKexec"
  # Kill us!
  Delete "$INSTDIR\KexecUninstall.exe"
  # And kill the install folder if it's empty.
  RmDir $INSTDIR
SectionEnd
