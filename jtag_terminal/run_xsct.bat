@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion
color 0A
set XSCT_PATH=D:\Xilinx2021\Vitis\2021.1\bin\xsct.bat 
set TCL_SCRIPT=%~dp0jtag_terminal.tcl

:MAIN
echo ===============================
echo       JTAG-UART连接工具
echo ===============================
echo 1. 连接ZYNQ7000
echo 2. 连接ZYNQMP
echo 3. 退出
echo .
set /p choice=请选择:

if "%choice%"=="1" goto LINK_A9
if "%choice%"=="2" goto LINK_A53
if "%choice%"=="3" exit /b
goto MAIN

:LINK_A9
echo 正在为zynq7000运行tcl脚本:%TCL_SCRIPT%
call "%XSCT_PATH%"  "%TCL_SCRIPT%" "zynq"
pause
goto MAIN


:LINK_A53
echo 正在为zynqmp运行tcl脚本:%TCL_SCRIPT%
call "%XSCT_PATH%"  "%TCL_SCRIPT%" "zynqmp"
pause
goto MAIN

