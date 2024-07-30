@rem VisualStudioSetup.exe --channelUri https://aka.ms/vs/15/release/channel --productId Microsoft.VisualStudio.Product.WDExpress
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\WDExpress\Common7\Tools\VsDevCmd.bat"
msbuild upnp_cmd.vcxproj /p:configuration=release;platform=x64
pause