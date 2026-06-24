@echo off
rem **************************************************************************
rem  Clean Script: Mandelbrot Set Renderer
rem
rem  Description:
rem
rem    Removes all build artifacts (object files, executables, and logs).
rem
rem  Usage:
rem
rem    clean
rem
rem **************************************************************************

echo Cleaning build artifacts ...

if exist vesa.obj        del vesa.obj
if exist app.obj         del app.obj
if exist main.obj        del main.obj
if exist fractal.exe     del fractal.exe
if exist build.log       del build.log

echo Done.
