set SRC_DIR=%1
set DST_DIR=%2
set RES_DIR=%3

mkdir %DST_DIR%
mkdir %DST_DIR%\res

xcopy %SRC_DIR%\ponzu.exe %DST_DIR% /Y /E
xcopy %SRC_DIR%\D3D12 %DST_DIR%\D3D12 /Y /E /I
xcopy %RES_DIR%\ibl %DST_DIR%\res\ibl /Y /E /I
xcopy %RES_DIR%\scene %DST_DIR%\res\scene /Y /E /I
xcopy ".\fps.txt" %DST_DIR% /Y /E/

del %DST_DIR%\D3D12\*.pdb /Q
rmdir %DST_DIR%\r3d.tlog /s /q
