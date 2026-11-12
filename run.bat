@echo off
setlocal
rem Use Git Bash for both modes; the kernel Makefiles need a POSIX shell.
bash "%~dp0run.sh" %*
exit /b %errorlevel%
