@rem Windows build script (gcc/clang + MSVC libs)
@echo off

where /Q gcc && (set "CC=gcc")
where /Q clang && (set "CC=clang")

if "%CC%" EQU "" (
    echo [1;41mERROR: no suitable compiler found ^(aborting^)[0m
    exit /b 1
)

set "FLAGS=-std=c99 -Wall -Wextra -pedantic -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -lshell32 -lkernel32 -lmsvcrt -Xlinker /NODEFAULTLIB:libcmt"
set "D_FLAGS=-g -gcodeview"
set "R_FLAGS=-DNDEBUG -O2"
set "file=wavgen"

if /I "%~1" EQU "debug" (
    set "mode=DEBUG"
    set "flags=%FLAGS% %D_FLAGS%"
) else (
    set "mode=RELEASE"
    set "flags=%FLAGS% %R_FLAGS%"
)

echo [1;44mBuilding wavgen in %mode% mode...[0m
@echo on
%CC% -o %file%.exe %file%.c %flags%
@echo off

if /I "%~1" NEQ "debug" (
    echo:
    echo [1;41mRemoving MSVC debug files ^(.pdb, .ilk, .obj^)[0m
    call :clean
)

setlocal disabledelayedexpansion
goto :eof

:clean
del /Q /S *.o *.obj *.ilk *.pdb >NUL 2>&1
goto :eof
