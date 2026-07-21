REM  MemorySearch Benchmark Runner
REM  adb connect ip:port
adb push .\out\build\ndk-arm64\test\benchmark /data/local/tmp
cls
echo ==============================================
echo   MemorySearch Benchmark - com.gameplier.kontra
echo ==============================================
adb shell "su -c 'chmod 777 /data/local/tmp/benchmark && /data/local/tmp/benchmark'"

REM 切换到 MemorySearch 交互模式 (注释掉 benchmark 行并取消下面注释)
@REM adb push .\out\build\ndk-arm64\MemorySearch /data/local/tmp
@REM cls
@REM adb shell "su -c 'chmod 777 /data/local/tmp/MemorySearch && /data/local/tmp/MemorySearch'"
