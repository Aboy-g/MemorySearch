REM  MemorySearch gg_tool Runner
REM  adb connect ip:port
adb push .\out\build\ndk-arm64\test\gg_tool /data/local/tmp
cls
echo ==============================================
echo   MemorySearch gg_tool - com.gameplier.kontra
echo ==============================================
adb shell "su -c 'chmod 777 /data/local/tmp/gg_tool && /data/local/tmp/gg_tool'"

REM 切换到 MemorySearch 交互模式 (注释掉 gg_tool 行并取消下面注释)
@REM adb push .\out\build\ndk-arm64\MemorySearch /data/local/tmp
@REM cls
@REM adb shell "su -c 'chmod 777 /data/local/tmp/MemorySearch && /data/local/tmp/MemorySearch'"
