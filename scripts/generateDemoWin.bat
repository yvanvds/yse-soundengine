@echo off

copy ..\lib\dll\libyse32.dll compiled

if not defined DevEnvDir (
  call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat"
)

SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
for %%i in ("source\*.cpp") do (
  cl /EHsc /Fecompiled\%%~ni %%i ..\lib\dll\libyse32.lib /I..\include 
  choice /c yn /m "Run This demo?"

  if !ERRORLEVEL!==1 (
    cls
    cd compiled
    call "%%~ni.exe"
    cd ..
  )

  cls
)
ENDLOCAL

del *.obj
