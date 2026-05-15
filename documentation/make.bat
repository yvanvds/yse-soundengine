@ECHO OFF
REM Convenience launcher for the libYSE documentation on Windows.
REM
REM Usage:
REM   make.bat html       full build: doxygen XML + sphinx HTML
REM   make.bat doxygen    just regenerate Doxygen XML
REM   make.bat sphinx     just rebuild Sphinx HTML
REM   make.bat serve      python http server on http://localhost:8000
REM   make.bat clean      delete build\ and source\_doxygen\

pushd %~dp0

if "%SPHINXBUILD%" == "" (
    set SPHINXBUILD=sphinx-build
)
set SOURCEDIR=source
set BUILDDIR=build

if "%1" == "" goto help
if "%1" == "help" goto help
if "%1" == "doxygen" goto doxygen
if "%1" == "sphinx" goto sphinx
if "%1" == "html" goto html
if "%1" == "serve" goto serve
if "%1" == "clean" goto clean
goto help

:doxygen
doxygen Doxyfile
goto end

:sphinx
%SPHINXBUILD% -b html "%SOURCEDIR%" "%BUILDDIR%\html"
goto end

:html
doxygen Doxyfile
if errorlevel 1 goto end
%SPHINXBUILD% -b html "%SOURCEDIR%" "%BUILDDIR%\html"
goto end

:serve
echo Serving on http://localhost:8000 (Ctrl-C to stop)
pushd "%BUILDDIR%\html"
python -m http.server 8000
popd
goto end

:clean
if exist "%BUILDDIR%" rmdir /s /q "%BUILDDIR%"
if exist "%SOURCEDIR%\_doxygen" rmdir /s /q "%SOURCEDIR%\_doxygen"
goto end

:help
echo Targets: html, doxygen, sphinx, serve, clean
goto end

:end
popd
