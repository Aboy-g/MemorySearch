REM  m.bat | $CMakeCurrentTargetName$ | $ProjectFileDir$
REM adb connect ip:port
adb push .\out\build\ndk-arm64\test\test /data/local/tmp
cls
adb shell "su -c 'chmod 777 /data/local/tmp/test && /data/local/tmp/test'"