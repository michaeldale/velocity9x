@ECHO OFF
ECHO Velocity9x driver deployment from this package folder
ECHO.
ECHO Run this from a real DOS prompt (F8, Command prompt only), never from
ECHO a DOS box inside Windows: the files it replaces are in use there.
ECHO.
REM One typo on 2026-09-13 wrote V9XHAL.DLL to a FILE named C:\WINDOWS\SYSTE
REM instead of into C:\WINDOWS\SYSTEM, and a later boot spent itself finding
REM out. Every path below is written once, here, and verified after the copy.

IF NOT "%1"=="" GOTO ARGDIR
SET V9XSYS=C:\WINDOWS\SYSTEM
GOTO HAVEDIR
:ARGDIR
SET V9XSYS=%1
:HAVEDIR

IF NOT EXIST V9XDISP.DRV GOTO NOTHERE
IF NOT EXIST V9XMINI.VXD GOTO NOTHERE
IF NOT EXIST V9XHAL.DLL GOTO NOTHERE
IF NOT EXIST V9XSETP.DLL GOTO NOTHERE
IF NOT EXIST %V9XSYS%\GDI.EXE GOTO NOSYS

ECHO Source: this directory
ECHO Target: %V9XSYS%
ECHO.
ECHO Copying V9XDISP.DRV ...
COPY V9XDISP.DRV %V9XSYS%\V9XDISP.DRV
IF ERRORLEVEL 1 GOTO FAILED
ECHO Copying V9XMINI.VXD ...
COPY V9XMINI.VXD %V9XSYS%\V9XMINI.VXD
IF ERRORLEVEL 1 GOTO FAILED
ECHO Copying V9XHAL.DLL ...
COPY V9XHAL.DLL %V9XSYS%\V9XHAL.DLL
IF ERRORLEVEL 1 GOTO FAILED
ECHO Copying V9XSETP.DLL ...
COPY V9XSETP.DLL %V9XSYS%\V9XSETP.DLL
IF ERRORLEVEL 1 GOTO FAILED

ECHO.
ECHO Verifying ...
IF NOT EXIST %V9XSYS%\V9XDISP.DRV GOTO FAILED
IF NOT EXIST %V9XSYS%\V9XMINI.VXD GOTO FAILED
IF NOT EXIST %V9XSYS%\V9XHAL.DLL GOTO FAILED
IF NOT EXIST %V9XSYS%\V9XSETP.DLL GOTO FAILED

REM A stale arm file would otherwise re-arm or block the next boot with a
REM token belonging to a build that is no longer installed.
IF NOT EXIST C:\V9XDIAG\INTELARM.TXT GOTO NOARM
ECHO.
ECHO Clearing the old arm state (this deployment is a new build) ...
ECHO [Velocity9x]>C:\V9XDIAG\INTELARM.TXT
ECHO IntelEnableThisBoot=0>>C:\V9XDIAG\INTELARM.TXT
REM Written explicitly so a freshly reset file is not mistaken for one that
REM predates the flag, which the armers refuse as ambiguous.
ECHO IntelIncomplete=0>>C:\V9XDIAG\INTELARM.TXT
:NOARM

ECHO.
ECHO Done. All four files are in %V9XSYS%.
ECHO.
ECHO Next: shut down, cold boot on AC, let the desktop appear, shut down
ECHO again, then read C:\V9XDIAG on the development host. Check that
ECHO V9XMODES.INI reports this package's build.
GOTO END

:NOTHERE
ECHO FAIL: run this from inside the package folder, for example:
ECHO         C:
ECHO         CD \INTELGMA
ECHO         V9XCOPY
GOTO END

:NOSYS
ECHO FAIL: %V9XSYS% does not look like a Windows SYSTEM directory.
ECHO       Pass the correct path, for example:  V9XCOPY C:\WIN98\SYSTEM
GOTO END

:FAILED
ECHO FAIL: a copy did not complete. Nothing may be assumed about the
ECHO       installed driver. Check free space and that Windows is not
ECHO       running, then run this again.

:END
SET V9XSYS=
ECHO.
PAUSE
