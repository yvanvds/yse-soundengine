$outpath = '.\yse_binary_release_android'
$zipfile = 'libYSE-1.0_android.zip'
#$buildVersion = 'release'
$buildVersion = 'debug'

Write-Host -ForegroundColor Green "Creating Folders..."

#update include files
Invoke-Expression .\generatePublicHeaders.bat
 
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
# New-Item $outpath\demo -ItemType directory | Out-Null
# New-Item $outpath\demo\source -ItemType directory | Out-Null
# New-Item $outpath\demo\compiled -ItemType directory | Out-Null

Write-Host -ForegroundColor Green "Replacing unwanted code..."
# this juce code makes it impossible to create a native library. We can do without.
(get-Content ..\JUCE\modules\juce_audio_devices\native\juce_android_OpenSL.cpp) | 
ForEach-Object { $_ -replace 'AndroidAudioIODevice javaDevice \(String::empty\)\;','//' } | 
ForEach-Object { $_ -replace 'inputLatency  \= \(javaDevice.minBufferSizeIn  \* 2\) / 3\;','inputLatency = 50;'} |
ForEach-Object { $_ -replace 'outputLatency \= \(javaDevice.minBufferSizeOut \* 2\) / 3\;','outputLatency = 50;'} |
Set-Content ..\JUCE\modules\juce_audio_devices\native\juce_android_OpenSL.cpp

(get-Content .\dll\Builds\Android\jni\Android.mk) |
ForEach-Object { $_ -replace 'juce_jni', 'libyse_dll' } |
Set-Content .\dll\Builds\Android\jni\Android.mk

(get-Content .\static_library\Builds\Android\jni\Android.mk) |
ForEach-Object { $_ -replace 'juce_jni', 'libyse' } |
Set-Content .\static_library\Builds\Android\jni\Android.mk

(get-Content .\dll\Builds\Android\build.xml) |
ForEach-Object { $_ -replace 'ndk-build\"', 'ndk-build.cmd\"' } |
Set-Content .\dll\Builds\Android\build.xml

(get-Content .\static_library\Builds\Android\build.xml) |
ForEach-Object { $_ -replace 'ndk-build\"', 'ndk-build.cmd\"' } |
Set-Content .\static_library\Builds\Android\build.xml

# make sure contains something like this, or build will fail!
# %JAVA_HOME%\bin;
# C:\Android\ant\bin;
# C:\Android\sdk\tools;
# C:\Android\ndk;
# C:\Android\ndk\toolchains\arm-linux-androideabi-4.8\prebuilt\windows-x86_64\bin

Write-Host -ForegroundColor Green "Creating static libraries..."
cd .\static_library\Builds\Android
# clean first because of possible fail on resource files
$build = 'ant clean ' + $buildVersion 
Invoke-Expression $build

$build = 'ant ' + $buildVersion
Invoke-Expression $build
cd ..\..\..

Move-Item .\static_library\Builds\Android\libs\* $outpath\lib\static

Write-Host -ForegroundColor Green "Creating dynamic libraries..."
cd .\dll\Builds\Android

# clean first because of possible fail on resource files
$build = 'ant clean ' + $buildVersion 
Invoke-Expression $build

$build = 'ant ' + $buildVersion
Invoke-Expression $build
cd ..\..\..

Move-Item .\dll\Builds\Android\libs\* $outpath\lib\dll

Write-Host -ForegroundColor Green "Copying header files..."
Copy-Item include\* $outpath\include -Recurse

#Write-Host -ForegroundColor Green "Copying demo files..."
#Copy-Item ConsoleDemo\source\* $outpath\demo\source
#Copy-Item bin\*.ogg $outpath\demo\compiled
#Copy-Item bin\*.wav $outpath\demo\compiled
#Copy-Item bin\*.env $outpath\demo\compiled
#Copy-Item bin\*.mid $outpath\demo\compiled
#Copy-Item .\scripts\generateDemoWin.bat $outpath\demo

#$result = Read-Host "Test Demo Files? (y/n)"

#if($result -eq 'y') {
#  cd $outpath\demo
#  Invoke-Expression .\generateDemoWin.bat
#  cd ..\..
#}

Write-Host -ForegroundColor Green "Copying info files..."
Copy-Item .\AUTHORS $outpath
Copy-Item .\COPYING $outpath
Copy-Item .\README $outpath


$result = Read-Host "Create a zip file? (y/n)"

if($result -eq 'y') {
  Write-Zip -Level 9 $outpath $zipfile
}

Write-Host -ForegroundColor Green "End of script."


