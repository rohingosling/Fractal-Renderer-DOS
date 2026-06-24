@echo off
rem **************************************************************************
rem  Build Script: Mandelbrot Set Renderer
rem  Compiler:     Borland Turbo C++ 3.1
rem  Environment:  DOS / DOSBox
rem
rem  Description:
rem
rem    Compiles VESA.C (C with inline assembly), APP.CPP (C++), and
rem    MAIN.CPP (C++), then links them into FRACTAL.EXE.
rem
rem    Uses the large memory model (-ml) for far pointer and farmalloc
rem    support. Floating point emulation is included automatically by
rem    the compiler when math functions are used.
rem
rem    All compiler and linker output is logged to BUILD.LOG.
rem
rem  Usage:
rem
rem    build          Build FRACTAL.EXE
rem
rem **************************************************************************

echo ============================================ >build.log
echo  Mandelbrot Set Renderer - Build Log >>build.log
echo  Compiler: Borland Turbo C++ 3.1 >>build.log
echo ============================================ >>build.log
echo. >>build.log

rem --------------------------------------------------------------------------
rem  Compile
rem --------------------------------------------------------------------------

echo Compiling VESA.C ...
echo ---- VESA.C ---- >> build.log
bcc -c -ml -2 vesa.c >> build.log
if errorlevel 1 goto fail

echo Compiling INI.CPP ...
echo ---- INI.CPP ---- >> build.log
bcc -c -ml -2 ini.cpp >> build.log
if errorlevel 1 goto fail

echo Compiling APP.CPP ...
echo ---- APP.CPP ---- >> build.log
bcc -c -ml -2 app.cpp >> build.log
if errorlevel 1 goto fail

echo Compiling MAIN.CPP ...
echo ---- MAIN.CPP ---- >> build.log
bcc -c -ml -2 main.cpp >> build.log
if errorlevel 1 goto fail

rem --------------------------------------------------------------------------
rem  Link
rem --------------------------------------------------------------------------

echo Linking FRACTAL.EXE ...
echo ---- LINK FRACTAL.EXE ---- >> build.log
bcc -ml -efractal.exe main.obj app.obj ini.obj vesa.obj >> build.log
if errorlevel 1 goto fail

echo. >> build.log
echo ============================================ >> build.log
echo  Build successful: FRACTAL.EXE >> build.log
echo ============================================ >> build.log

echo.
echo Build successful: FRACTAL.EXE
goto end

rem --------------------------------------------------------------------------
rem  Error
rem --------------------------------------------------------------------------

:fail
echo. >> build.log
echo *** BUILD FAILED *** >> build.log

echo.
echo Build failed! See BUILD.LOG for details.

:end
