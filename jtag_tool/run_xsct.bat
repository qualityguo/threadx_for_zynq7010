@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion
color 0A
set XSCT_PATH=D:\Xilinx2021\Vitis\2021.1\bin\xsct.bat 

:MAIN
echo ===============================
echo   JTAG-UART连接+JTAG-TraceX导出
echo ===============================
echo 1. 连接ZYNQ7000-UART
echo 2. 连接ZYNQMP-UART
echo 3. 导出ZYNQ7000-TraceX
echo 4. 导出ZYNQMP-TraceX
echo 5. 退出
echo .
set /p choice=请选择:

if "%choice%"=="1" goto LINK_A9_UART
if "%choice%"=="2" goto LINK_A53_UART
if "%choice%"=="3" goto OUT_A9_TRACEX
if "%choice%"=="4" goto OUT_A53_TRACEX
if "%choice%"=="5" exit /b
goto MAIN

:LINK_A9_UART
set TCL_SCRIPT=%~dp0jtag_terminal.tcl
echo 正在为zynq7000运行tcl脚本:%TCL_SCRIPT%
call "%XSCT_PATH%"  "%TCL_SCRIPT%" "zynq"
pause
goto MAIN


:LINK_A53_UART
set TCL_SCRIPT=%~dp0jtag_terminal.tcl
echo 正在为zynqmp运行tcl脚本:%TCL_SCRIPT%
call "%XSCT_PATH%"  "%TCL_SCRIPT%" "zynqmp"
pause
goto MAIN

:OUT_A9_TRACEX
set TCL_SCRIPT=%~dp0jtag_tracex.tcl
echo 正在为zynq7000运行tcl脚本:%TCL_SCRIPT%
call "%XSCT_PATH%"  "%TCL_SCRIPT%" "zynq"
pause
goto MAIN


:OUT_A53_TRACEX
set TCL_SCRIPT=%~dp0jtag_tracex.tcl
echo 正在为zynqmp运行tcl脚本:%TCL_SCRIPT%
call "%XSCT_PATH%"  "%TCL_SCRIPT%" "zynqmp"
pause
goto MAIN