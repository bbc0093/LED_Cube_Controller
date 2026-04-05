@echo off
REM ---------------------------------------------------------
REM  ESP-IDF Environment Launcher
REM  Opens a new terminal with the IDF environment activated
REM ---------------------------------------------------------

REM *** CHANGE THIS TO YOUR ESP-IDF PATH ***
set IDF_PATH=C:\esp\v6.0\esp-idf

REM Load ESP-IDF environment
call "%IDF_PATH%\export.bat"

REM Stay in the terminal so you can run idf.py commands
cmd.exe /K