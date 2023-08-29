@echo off
@rem //------------------------------------------------------
@rem // File : gen.bat
@rem // Desc : Binary Format Generator.
@rem // Copyright(c) Project Asura. All right reserved.
@rem //-------------------------------------------------------
setlocal

@rem --- 以下に追加してください --
call :CompileFormat scene
call :CompileFormat camera


@rem --- おしまい ---
endlocal
exit /b

@rem --- コンパイルコマンド ---
:CompileFormat
flatc.exe -c %1.fbs --natural-utf8 --filename-suffix _format
move "%1_format.h" "..\..\include\generated\%1_format.h"
exit /b
