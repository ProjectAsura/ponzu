@echo off
setlocal
pushd "%~dp0"

set SRC_DIR=%1
set DST_DIR=%2
set RES_DIR=%3

mkdir %DST_DIR%
mkdir %DST_DIR%\res

xcopy %SRC_DIR%\rtc.exe %DST_DIR% /y /c
::xcopy %SRC_DIR%\dxil.dll %DST_DIR% /y /c
::xcopy %SRC_DIR%\dxcompiler.dll %DST_DIR% /y /c
xcopy %SRC_DIR%\D3D12 %DST_DIR%\D3D12 /y /i /c
xcopy %RES_DIR%\ibl\*.dds %DST_DIR%\res\ibl /y /i /c
xcopy %RES_DIR%\scene\*.scn %DST_DIR%\res\scene /y /i /c
xcopy %RES_DIR%\scene\*.cam %DST_DIR%\res\scene /y /i /c
xcopy ".\fps.txt" %DST_DIR% /y /c
xcopy ".\run.ps1" %DST_DIR% /y /c

del %DST_DIR%\D3D12\*.pdb /q

popd
