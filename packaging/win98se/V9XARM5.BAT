@ECHO OFF
ECHO Velocity9x Intel Phase 5 one-shot arm (3D triangle)
ECHO.
REM This arms the NEXT boot to run the Phase 5 draw once. The build id and the
REM COMBINED command CRC below are stamped in by the package build, from the
REM same generated table the mini-VDD's arm tables come from, so they always
REM describe the binaries sitting beside this file. A stick armed against an
REM older build, or against a stale CRC, is refused by the driver.
REM
REM The package build refuses to produce this file at all unless the Phase 5
REM errata-gate decision is on record in docs\decisions. That check is
REM host-side, where the decision lives; nothing here can bypass it.
REM
REM Run it from real DOS after V9XCOPY, from inside the package folder:
REM     C:
REM     CD \INTELGMA
REM     V9XCOPY
REM     V9XARM5
REM Then shut down and cold boot ONCE on AC power.
REM
REM Every machine-side gate is unchanged. The driver rechecks the build id,
REM requires IntelArmPhase=5, re-derives both phase CRCs and the combined CRC
REM itself, replays Phase 4 before the draw, and consumes the token once.

IF NOT EXIST V9XDISP.DRV GOTO NOTHERE
IF NOT EXIST C:\V9XDIAG\NUL GOTO NODIAG

REM An unresolved previous attempt leaves IntelInFlight set. Refuse rather than
REM overwrite it: a chained run leaves the token in flight on EVERY failure
REM path by design, so that record is the expected state after a hang and the
REM only evidence of where it stopped. Run V9XCOPY to clear it deliberately.
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
ECHO IntelArmPhase=5>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelArmCrc=@@COMBINEDCRC@@>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelArmPhase4Crc=@@P4CRC@@>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelArmPhase5Crc=@@P5CRC@@>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelArmBuildId=@@BUILDID@@>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelInFlight=>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelIncomplete=0>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelEnableThisBoot=1>>C:\V9XDIAG\INTELARM.TXT
ECHO IntelLastResult=>>C:\V9XDIAG\INTELARM.TXT
REM Repeat mode, set by running V9XARM5 R. IntelArmOnce survives the boot,
REM so the stick arms again next time without being re-armed. A run that
REM HANGS still leaves IntelInFlight set and the next boot refuses with
REM INCOMPLETE, so only successful boots repeat.
REM COMMAND.COM has no IF /I. A syntax error there does not halt a batch, so
REM the next line would have run and set repeat mode unconditionally - the
REM opposite of opt-in. Explicit comparisons, and anything that is not
REM exactly R or r branches past.
IF "%1"=="R" GOTO REPEAT
IF "%1"=="r" GOTO REPEAT
GOTO NOREPEAT
:REPEAT
ECHO IntelArmRepeat=1>>C:\V9XDIAG\INTELARM.TXT
ECHO Repeat mode: this stick will arm again on every successful boot.
:NOREPEAT

IF NOT EXIST C:\V9XDIAG\INTELARM.TXT GOTO FAILED
ECHO Armed Phase 5 for build @@BUILDID@@, combined CRC @@COMBINEDCRC@@.
ECHO   Phase 4 replay CRC @@P4CRC@@
ECHO   Phase 5 draw CRC   @@P5CRC@@
ECHO.
ECHO Shut down and cold boot ONCE, on AC power. Do not change resolution and
ECHO do not open a DOS box on the armed boot.
GOTO DONE

:NOTHERE
ECHO FAIL: run this from inside the package folder, beside V9XDISP.DRV.
GOTO DONE

:NODIAG
ECHO FAIL: C:\V9XDIAG does not exist. Boot the driver once first.
GOTO DONE

:INFLIGHT
ECHO FAIL: C:\V9XDIAG\INTELARM.TXT still carries IntelInFlight.
ECHO The previous attempt did not resolve. Collect C:\V9XDIAG first, then run
ECHO V9XCOPY to clear it deliberately.
GOTO DONE

:FAILED
ECHO FAIL: could not write C:\V9XDIAG\INTELARM.TXT.


:LEGACY
ECHO FAIL: C:\V9XDIAG\INTELARM.TXT has no IntelIncomplete line.
ECHO It predates the in-flight flag, so whether the previous run resolved
ECHO cannot be told from here. Boot once to let the driver normalise it, or
ECHO run V9XCOPY to reset the arm state deliberately.
GOTO DONE
:DONE
PAUSE
