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
IF NOT EXIST V9XGL.DLL GOTO NOTHERE
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
ECHO Copying V9XGL.DLL ...
COPY V9XGL.DLL %V9XSYS%\V9XGL.DLL
IF ERRORLEVEL 1 GOTO FAILED

REM GLIDE2X.DLL has the name every Glide game loads, so a 3dfx card's own
REM may be there already. COPY cannot compare versions as the INF does;
REM only ours carries the text V9XGLIDE.LOG, so a file without it is kept.
REM FIND by full path: a missing FIND would leave COPY's ERRORLEVEL 0 and
REM read as a match.
IF NOT EXIST GLIDE2X.DLL GOTO NOGLIDE
IF NOT EXIST %V9XSYS%\GLIDE2X.DLL GOTO COPYGLIDE
IF NOT EXIST %V9XSYS%\..\COMMAND\FIND.EXE GOTO KEEPGLIDE
%V9XSYS%\..\COMMAND\FIND "V9XGLIDE.LOG" %V9XSYS%\GLIDE2X.DLL >NUL
IF ERRORLEVEL 1 GOTO KEEPGLIDE
:COPYGLIDE
ECHO Copying GLIDE2X.DLL ...
COPY GLIDE2X.DLL %V9XSYS%\GLIDE2X.DLL
IF ERRORLEVEL 1 GOTO FAILED
GOTO NOGLIDE
:KEEPGLIDE
ECHO Keeping %V9XSYS%\GLIDE2X.DLL: it is not Velocity9x's (a 3dfx card's?).
:NOGLIDE

REM GLIDE3X.DLL the same way: ours carries the text V9XGLD3.LOG.
IF NOT EXIST GLIDE3X.DLL GOTO NOGLIDE3
IF NOT EXIST %V9XSYS%\GLIDE3X.DLL GOTO COPYGLIDE3
IF NOT EXIST %V9XSYS%\..\COMMAND\FIND.EXE GOTO KEEPGLIDE3
%V9XSYS%\..\COMMAND\FIND "V9XGLD3.LOG" %V9XSYS%\GLIDE3X.DLL >NUL
IF ERRORLEVEL 1 GOTO KEEPGLIDE3
:COPYGLIDE3
ECHO Copying GLIDE3X.DLL ...
COPY GLIDE3X.DLL %V9XSYS%\GLIDE3X.DLL
IF ERRORLEVEL 1 GOTO FAILED
GOTO NOGLIDE3
:KEEPGLIDE3
ECHO Keeping %V9XSYS%\GLIDE3X.DLL: it is not Velocity9x's (a 3dfx card's?).
:NOGLIDE3

ECHO.
ECHO Verifying ...
IF NOT EXIST %V9XSYS%\V9XDISP.DRV GOTO FAILED
IF NOT EXIST %V9XSYS%\V9XMINI.VXD GOTO FAILED
IF NOT EXIST %V9XSYS%\V9XHAL.DLL GOTO FAILED
IF NOT EXIST %V9XSYS%\V9XSETP.DLL GOTO FAILED
IF NOT EXIST %V9XSYS%\V9XGL.DLL GOTO FAILED

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
