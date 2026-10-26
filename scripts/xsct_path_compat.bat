@echo off
call "C:\Xilinx\SDK\2018.3\bin\xsct.bat" "%~dp0xsct_path_compat.tcl" %*
exit /b %ERRORLEVEL%
