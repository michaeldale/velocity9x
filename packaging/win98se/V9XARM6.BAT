@ECHO OFF
ECHO Velocity9x Intel Phase 6 one-shot arm (five scenes, one boot)
ECHO.
REM This arms the NEXT boot to run all five Phase 6 scenes once. The build id and the
REM COMBINED command CRC below are stamped in by the package build, from the
REM same generated table the mini-VDD's arm tables come from, so they always
REM describe the binaries sitting beside this file. A stick armed against an
REM older build, or against a stale CRC, is refused by the driver.
REM
REM The package build refuses to produce this file at all unless the Phase 5
REM errata-gate decision AND its 2026-09-16 five-draw amendment are on
REM record in docs\decisions. Five draws in one boot is a different risk
REM assessment from one, and that amendment is where it was taken. The
REM check is host-side, where the decisions live; nothing here can bypass
REM it.
REM
REM
REM FIVE DRAWS, not one. The 2026-09-16 amendment authorises up to five
REM independent draws per armed boot and this stick arms exactly that. Each
REM scene re-emits complete state, fills its own target and flushes its own
REM probes before the next submits - a scene inheriting another's state would
REM be outside the amendment even at two.
REM
REM This boot also reads far more through the aperture than any before it:
REM about 726 reads against 45 on the last boot that completed. The capture
REM publishes the expected total BEFORE spending any of it, so a hang is a
REM measurement rather than a guess.
REM Run it from real DOS after V9XCOPY, from inside the package folder:
REM     C:
REM     CD \INTELGMA
REM     V9XCOPY
REM     V9XARM6
REM Then shut down and cold boot ONCE on AC power.
REM
REM Every machine-side gate is unchanged. The driver rechecks the build id,
REM requires IntelArmPhase=6, re-derives the scene CRCs and the combined CRC
REM itself, replays Phase 4 before the first scene, and consumes it once.

REM The arm file path. Assigned BEFORE its first use: it was assigned after
REM the first IF NOT EXIST, so on a normal DOS run - where the variable is
REM unset - that check expanded to nothing and was malformed, breaking the
REM missing-file path it exists to handle.
REM
REM Overridable ONLY so the host self-test can point the guard at a fixture.
IF "%V9XARMFILE%"=="" SET V9XARMFILE=C:\V9XDIAG\INTELARM.TXT

IF NOT EXIST V9XDISP.DRV GOTO NOTHERE
IF NOT EXIST C:\V9XDIAG\NUL GOTO NODIAG

REM An unresolved previous attempt leaves IntelInFlight set. Refuse rather than
REM overwrite it: a chained run leaves the token in flight on EVERY failure
REM path by design, so that record is the expected state after a hang and the
REM only evidence of where it stopped. Run V9XCOPY to clear it deliberately.
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
ECHO IntelArmPhase=6>>%V9XARMFILE%
ECHO IntelArmCrc=@@SCENEARMCRC@@>>%V9XARMFILE%
ECHO IntelArmPhase4Crc=@@P4CRC@@>>%V9XARMFILE%
ECHO IntelArmSceneCrc=@@SCENECRC@@>>%V9XARMFILE%
ECHO IntelArmBuildId=@@BUILDID@@>>%V9XARMFILE%
ECHO IntelInFlight=>>%V9XARMFILE%
ECHO IntelIncomplete=0>>%V9XARMFILE%
ECHO IntelEnableThisBoot=1>>%V9XARMFILE%
ECHO IntelLastResult=>>%V9XARMFILE%
REM Repeat mode, set by running V9XARM6 R. IntelArmOnce survives the boot,
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
ECHO IntelArmRepeat=1>>%V9XARMFILE%
ECHO Repeat mode: this stick will arm again on every successful boot.
:NOREPEAT

IF NOT EXIST %V9XARMFILE% GOTO FAILED
ECHO Armed Phase 6 for build @@BUILDID@@, combined CRC @@SCENEARMCRC@@.
ECHO   Phase 4 replay CRC @@P4CRC@@
ECHO   Five scenes CRC    @@SCENECRC@@
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
ECHO FAIL: %V9XARMFILE% still carries IntelInFlight.
ECHO The previous attempt did not resolve. Collect C:\V9XDIAG first, then run
ECHO V9XCOPY to clear it deliberately.
GOTO DONE

:FAILED
ECHO FAIL: could not write %V9XARMFILE%.
REM Without this the write-failure path fell into :UNRESOLVED and told the
REM operator to run V9XCOPY, which would discard the arm record for a fault
REM that has nothing to do with an unresolved run.
GOTO DONE


:UNRESOLVED
ECHO FAIL: %V9XARMFILE% does not state IntelIncomplete=0.
ECHO Either it predates that flag, or its value is empty or malformed.
ECHO Whether the previous run resolved cannot be told from here, and the
ECHO record of a hang must not be overwritten. Boot once to let the driver
ECHO normalise it, or run V9XCOPY to reset the arm state deliberately.
GOTO DONE
:DONE
PAUSE
