@ECHO OFF
ECHO Velocity9x Intel Phase 4 one-shot arm
ECHO.
REM This arms the NEXT boot to run the Phase 4 first-write experiment once.
REM The build id and command CRC below are stamped in by the package build, so
REM they always describe the binaries sitting beside this file. The driver
REM rechecks both, re-derives the CRC itself, and refuses on any mismatch.
REM
REM Run it from real DOS after V9XCOPY, from inside the package folder:
REM     C:
REM     CD \INTELGMA
REM     V9XCOPY
REM     V9XARM
REM Then shut down and cold boot ONCE on AC power.

REM The arm file path. Assigned BEFORE its first use: it was assigned after
REM the first IF NOT EXIST, so on a normal DOS run - where the variable is
REM unset - that check expanded to nothing and was malformed, breaking the
REM missing-file path it exists to handle.
REM
REM Overridable ONLY so the host self-test can point the guard at a fixture.
IF "%V9XARMFILE%"=="" SET V9XARMFILE=C:\V9XDIAG\INTELARM.TXT

IF NOT EXIST V9XDISP.DRV GOTO NOTHERE
IF NOT EXIST C:\V9XDIAG\NUL GOTO NODIAG

REM An unfinished previous attempt leaves IntelInFlight set. Refuse rather
REM than overwrite it: that record is the only evidence of where it stopped.
IF NOT EXIST %V9XARMFILE% GOTO WRITE
REM Refuse unless the file states plainly that the previous run RESOLVED.
REM
REM IntelInFlight is authoritative, but FIND matches substrings and cannot
REM tell its empty form from a non-empty one, so the driver mirrors it into
REM IntelIncomplete and re-derives that from the authority every boot.
REM
REM Testing for the absence of =1 is not enough: IntelIncomplete= and
REM IntelIncomplete=garbage both have the key, both lack =1, and both would
REM have reached the overwrite. So =0 must be positively present, which also
REM covers a file written before the flag existed - it has no such line.
REM
REM Residual, stated rather than hidden: FIND still matches substrings, so a
REM hand-edited IntelIncomplete=0X would be accepted. The driver only ever
REM writes 0 or 1, so that is a deliberate edit rather than a state the
REM system produces.
FIND "IntelIncomplete=1" %V9XARMFILE% >NUL
IF NOT ERRORLEVEL 1 GOTO INFLIGHT
FIND "IntelIncomplete=0" %V9XARMFILE% >NUL
IF ERRORLEVEL 1 GOTO UNRESOLVED

:WRITE
ECHO Token:  %V9XARMTOKEN%
IF "%V9XARMTOKEN%"=="" ECHO   (empty: using the packaged default below)
ECHO.
ECHO [Velocity9x]>%V9XARMFILE%
ECHO IntelAccelDefault=0>>%V9XARMFILE%
IF "%V9XARMTOKEN%"=="" ECHO IntelArmOnce=@@TOKEN@@>>%V9XARMFILE%
IF NOT "%V9XARMTOKEN%"=="" ECHO IntelArmOnce=%V9XARMTOKEN%>>%V9XARMFILE%
ECHO IntelArmCrc=@@ARMCRC@@>>%V9XARMFILE%
ECHO IntelArmBuildId=@@BUILDID@@>>%V9XARMFILE%
REM Phase 4, stated rather than implied. The driver reads an absent
REM IntelArmPhase as Phase 4 so that sticks written before 2026-09-15 still
REM boot, but nothing written from here on should depend on that.
ECHO IntelArmPhase=4>>%V9XARMFILE%
ECHO IntelInFlight=>>%V9XARMFILE%
ECHO IntelIncomplete=0>>%V9XARMFILE%
ECHO IntelEnableThisBoot=0>>%V9XARMFILE%
ECHO IntelLastResult=>>%V9XARMFILE%

IF NOT EXIST %V9XARMFILE% GOTO FAILED
ECHO Armed for build @@BUILDID@@, CRC @@ARMCRC@@.
ECHO.
ECHO Shut down and cold boot ONCE, on AC power. Do not change resolution and
ECHO do not open a DOS box on that boot. Afterwards copy C:\V9XDIAG off the
ECHO stick and read INTELRNG.TXT.
GOTO END

:INFLIGHT
ECHO REFUSED: %V9XARMFILE% still has IntelInFlight set, so the
ECHO          previous attempt never completed. Copy C:\V9XDIAG off first;
ECHO          that file says where it stopped. Running V9XCOPY clears it.
GOTO END

:NOTHERE
ECHO FAIL: run this from inside the package folder, for example:
ECHO         C:
ECHO         CD \INTELGMA
ECHO         V9XARM
GOTO END

:NODIAG
ECHO FAIL: C:\V9XDIAG does not exist. Boot the driver once first so it
ECHO       creates the diagnostic directory.
GOTO END

:FAILED
ECHO FAIL: could not write %V9XARMFILE%.


:UNRESOLVED
ECHO FAIL: %V9XARMFILE% does not state IntelIncomplete=0.
ECHO Either it predates that flag, or its value is empty or malformed.
ECHO Whether the previous run resolved cannot be told from here, and the
ECHO record of a hang must not be overwritten. Boot once to let the driver
ECHO normalise it, or run V9XCOPY to reset the arm state deliberately.
GOTO END
:END
ECHO.
PAUSE
