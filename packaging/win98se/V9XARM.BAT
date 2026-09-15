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

IF NOT EXIST V9XDISP.DRV GOTO NOTHERE
IF NOT EXIST C:\V9XDIAG\NUL GOTO NODIAG

REM An unfinished previous attempt leaves IntelInFlight set. Refuse rather
REM than overwrite it: that record is the only evidence of where it stopped.
IF NOT EXIST C:\V9XDIAG\INTELARM.TXT GOTO WRITE
REM FIND matches SUBSTRINGS, so "IntelInFlight=" matched the empty form and
REM every non-empty one alike. The previous test piped FIND into FIND /V on
REM the same string, which always yields nothing, so this guard never fired
REM in either phase. The driver now writes IntelIncomplete=1 alongside a
REM non-empty IntelInFlight and clears it on retirement, which is a value a
REM batch can actually test.
REM Two refusals, not one. IntelInFlight is authoritative, but FIND cannot
REM tell its empty form from a non-empty one, so the driver mirrors it into
REM IntelIncomplete and re-derives that from the authority on every boot.
REM
REM An arm file written before that flag existed carries a token with NO
REM flag, and testing only for =1 would read it as resolved and overwrite the
REM record of a hang. So a file with no IntelIncomplete line at all is refused
REM as ambiguous: boot once to let the driver normalise it, or run V9XCOPY to
REM reset the arm state deliberately.
FIND "IntelIncomplete=" C:\V9XDIAG\INTELARM.TXT >NUL
IF ERRORLEVEL 1 GOTO LEGACY
FIND "IntelIncomplete=1" C:\V9XDIAG\INTELARM.TXT >NUL
IF NOT ERRORLEVEL 1 GOTO INFLIGHT

:WRITE
ECHO Token:  %V9XARMTOKEN%
IF "%V9XARMTOKEN%"=="" ECHO   (empty: using the packaged default below)
ECHO.
ECHO [Velocity9x]>C:\V9XDIAG\INTELARM.TXT
ECHO IntelAccelDefault=0>>C:\V9XDIAG\INTELARM.TXT
IF "%V9XARMTOKEN%"=="" ECHO IntelArmOnce=@@TOKEN@@>>C:\V9XDIAG\INTELARM.TXT
IF NOT "%V9XARMTOKEN%"=="" ECHO IntelArmOnce=%V9XARMTOKEN%>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelArmCrc=@@ARMCRC@@>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelArmBuildId=@@BUILDID@@>>C:\V9XDIAG\INTELARM.TXT
REM Phase 4, stated rather than implied. The driver reads an absent
REM IntelArmPhase as Phase 4 so that sticks written before 2026-09-15 still
REM boot, but nothing written from here on should depend on that.
ECHO IntelArmPhase=4>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelInFlight=>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelIncomplete=0>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelEnableThisBoot=0>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelLastResult=>>C:\V9XDIAG\INTELARM.TXT

IF NOT EXIST C:\V9XDIAG\INTELARM.TXT GOTO FAILED
ECHO Armed for build @@BUILDID@@, CRC @@ARMCRC@@.
ECHO.
ECHO Shut down and cold boot ONCE, on AC power. Do not change resolution and
ECHO do not open a DOS box on that boot. Afterwards copy C:\V9XDIAG off the
ECHO stick and read INTELRNG.TXT.
GOTO END

:INFLIGHT
ECHO REFUSED: C:\V9XDIAG\INTELARM.TXT still has IntelInFlight set, so the
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
ECHO FAIL: could not write C:\V9XDIAG\INTELARM.TXT.


:LEGACY
ECHO FAIL: C:\V9XDIAG\INTELARM.TXT has no IntelIncomplete line.
ECHO It predates the in-flight flag, so whether the previous run resolved
ECHO cannot be told from here. Boot once to let the driver normalise it, or
ECHO run V9XCOPY to reset the arm state deliberately.
GOTO END
:END
ECHO.
PAUSE
