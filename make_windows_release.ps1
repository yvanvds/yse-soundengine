$outpath = '.\yse_binary_release_windows'
$zipfile = 'libYSE-1.0_windows.zip'

Write-Host -ForegroundColor Green "Creating Folders..."


# remove previous build
if (Test-Path $outpath -PathType Container) {
    Remove-Item $outpath -Force -Recurse
}

if(Test-Path .\$zipfile -PathType Leaf) {
  Remove-Item .\$zipfile
}

# create directories
New-Item $outpath -ItemType directory | Out-Null
New-Item $outpath\include -ItemType directory | Out-Null
New-Item $outpath\lib -ItemType directory | Out-Null
New-Item $outpath\lib\static -ItemType directory | Out-Null
New-Item $outpath\lib\dll -ItemType directory | Out-Null
New-Item $outpath\demo -ItemType directory | Out-Null
New-Item $outpath\demo\source -ItemType directory | Out-Null
New-Item $outpath\demo\compiled -ItemType directory | Out-Null

Invoke-Expression .\generatePublicHeaders.bat

Write-Host -ForegroundColor Green "Setting visual studio environment variables..."
# If the next command fails, you have to install the powershell community extensions.
# After installing, copy the Pscx folder to your modules path, which can be found with
# Get-ChildItem Env:\PSModulePath
Import-VisualStudioVars -VisualStudioVersion 140


Write-Host -ForegroundColor Green "Creating static libraries..."
$build = 'devenv static_library\Builds\VisualStudio2015\yse_static_library.sln /rebuild "release32|Win32"' 
Invoke-Expression $build
Move-Item .\bin\libyse32.lib $outpath\lib\static

$build = 'devenv static_library\Builds\VisualStudio2015\yse_static_library.sln /rebuild "release64|x64"'
Invoke-Expression $build
Move-Item .\bin\libyse64.lib $outpath\lib\static

Write-Host -ForegroundColor Green "Creating dynamic libraries..."
$build = 'devenv dll\Builds\VisualStudio2015\yse_dll.sln /rebuild "release32|Win32"' 
Invoke-Expression $build
Move-Item .\bin\libyse32.* $outpath\lib\dll

$build = 'devenv dll\Builds\VisualStudio2015\yse_dll.sln /rebuild "release64|x64"'
Invoke-Expression $build
Move-Item .\bin\libyse64.* $outpath\lib\dll

Write-Host -ForegroundColor Green "Copying header files..."
Copy-Item include\* $outpath\include -Recurse

Write-Host -ForegroundColor Green "Copying demo files..."
Copy-Item ConsoleDemo\source\* $outpath\demo\source
Copy-Item bin\*.ogg $outpath\demo\compiled
Copy-Item bin\*.wav $outpath\demo\compiled
Copy-Item bin\*.env $outpath\demo\compiled
Copy-Item bin\*.mid $outpath\demo\compiled
Copy-Item .\scripts\generateDemoWin.bat $outpath\demo

$result = Read-Host "Test Demo Files? (y/n)"

if($result -eq 'y') {
  cd $outpath\demo
  Invoke-Expression .\generateDemoWin.bat
  cd ..\..
}



Write-Host -ForegroundColor Green "Copying info files..."
Copy-Item .\AUTHORS $outpath
Copy-Item .\COPYING $outpath
Copy-Item .\README $outpath
Copy-Item .\YSE_user_manual.pdf $outpath


$result = Read-Host "Create a zip file? (y/n)"

if($result -eq 'y') {
  Write-Zip -Level 9 $outpath $zipfile
}

Write-Host -ForegroundColor Green "End of script."


