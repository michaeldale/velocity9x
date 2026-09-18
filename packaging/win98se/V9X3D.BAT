@ECHO OFF
ECHO Velocity9x Intel runtime Direct3D permission
ECHO.
REM This is NOT an armer. It sets a RETAINED permission that every subsequent
REM boot reads, rather than a one-shot token that the next boot consumes.
REM
REM What it governs: whether the display driver may bring the command ring up
REM and advertise Direct3D to applications, and (IntelFlip) whether it moves
REM the scanout itself on Flip. BOTH ARE ON BY DEFAULT from 2026-09-18: a key
REM that is absent - which is how V9XCOPY leaves the file - permits, and only
REM a key reading 0 refuses. Until then absent meant off, because no machine
REM had executed application geometry on this engine; intel59 through
REM intel66 did, on every boot, and the operator took the decision to make it
REM the default. So ON and FLIP now change nothing on a fresh install; they
REM exist to turn the path back on after an OFF.
REM
REM TO RECOVER after a hang, from real DOS, either:
REM     V9X3D OFF
REM or, equivalently and with everything else reset too:
REM     C:
REM     CD \INTELGMA
REM     V9XCOPY
REM V9XCOPY rewrites the arm file without either key, which since 2026-09-18
REM means both are ON again after it. To leave the machine with the runtime
REM path OFF, run V9X3D OFF after V9XCOPY, not before.
REM
REM An ARMED boot ignores this key entirely. The driver clears the permission
REM in memory whenever a one-shot token is consumed, because a diagnostic boot
REM owns the ring - it sets START and CTL itself and clears them on teardown -
REM and a DirectDraw session bringing the ring up underneath that would be two
REM owners for one register file. V9XARM, V9XARM5 and V9XARM6 also rewrite the
REM arm file from scratch, so arming clears this permission on disk as well;
REM run V9X3D ON again after the armed boot has been collected.

REM Assigned BEFORE its first use, for the reason recorded in the armers: a
REM variable referenced before assignment expands to nothing and the IF that
REM uses it is malformed, which silently breaks the path it guards.
REM
REM Overridable ONLY so a host self-test can point this at a fixture.
IF "%V9XARMFILE%"=="" SET V9XARMFILE=C:\V9XDIAG\INTELARM.TXT

IF NOT EXIST V9XDISP.DRV GOTO NOTHERE
IF NOT EXIST C:\V9XDIAG\NUL GOTO NODIAG

REM COMMAND.COM has no IF /I, and a syntax error there does not halt a batch -
REM the next line simply runs. So every comparison is explicit and both cases
REM are spelled out, and anything that is neither falls through to the usage
REM text rather than to a default.
IF "%1"=="ON" GOTO WANTON
IF "%1"=="on" GOTO WANTON
IF "%1"=="OFF" GOTO WANTOFF
IF "%1"=="off" GOTO WANTOFF
IF "%1"=="FLIP" GOTO WANTFLIP
IF "%1"=="flip" GOTO WANTFLIP
GOTO USAGE

:WANTFLIP
REM ON, plus permission for the HAL to move the scanout itself: write the
REM live pipe's plane base register on Flip and read the pipe's display line
REM for the retrace, instead of declining every Flip and letting DirectDraw
REM copy each frame with the CPU. Both were measured on this machine in
REM intel61 through intel66 and are the default; this verb restores them
REM after an ON or OFF. Same refusal as ON for an unresolved armed record.
IF NOT EXIST %V9XARMFILE% GOTO WRITEFLIP
FIND "IntelIncomplete=1" %V9XARMFILE% >NUL
IF NOT ERRORLEVEL 1 GOTO INFLIGHT
FIND "IntelIncomplete=0" %V9XARMFILE% >NUL
IF ERRORLEVEL 1 GOTO UNRESOLVED
:WRITEFLIP
ECHO [Velocity9x]>%V9XARMFILE%
ECHO IntelEnableThisBoot=0>>%V9XARMFILE%
ECHO IntelIncomplete=0>>%V9XARMFILE%
ECHO IntelRuntime3D=1>>%V9XARMFILE%
ECHO IntelFlip=1>>%V9XARMFILE%
IF NOT EXIST %V9XARMFILE% GOTO FAILED
ECHO Runtime Direct3D AND the Intel flip are PERMITTED from the next boot,
ECHO which is also what a fresh install does.
ECHO.
ECHO If the machine hangs or the display is lost: power off, boot to real
ECHO DOS, run V9X3D ON (keeps Direct3D, drops the flip) or V9X3D OFF, and
ECHO collect C:\V9XDIAG before booting Windows again.
GOTO DONE

:WANTON
REM Refuse to overwrite the record of an unresolved armed run. It is the only
REM evidence of where a hang stopped, and a chained run leaves the token in
REM flight on every failure path by design. The armers refuse for the same
REM reason and in the same shape; V9XCOPY is the deliberate reset.
REM
REM Testing for the absence of =1 is not enough: IntelIncomplete= and
REM IntelIncomplete=garbage both have the key, both lack =1, and both would
REM reach the write. So =0 must be positively present.
IF NOT EXIST %V9XARMFILE% GOTO WRITEON
FIND "IntelIncomplete=1" %V9XARMFILE% >NUL
IF NOT ERRORLEVEL 1 GOTO INFLIGHT
FIND "IntelIncomplete=0" %V9XARMFILE% >NUL
IF ERRORLEVEL 1 GOTO UNRESOLVED
:WRITEON
ECHO [Velocity9x]>%V9XARMFILE%
ECHO IntelEnableThisBoot=0>>%V9XARMFILE%
ECHO IntelIncomplete=0>>%V9XARMFILE%
ECHO IntelRuntime3D=1>>%V9XARMFILE%
ECHO IntelFlip=0>>%V9XARMFILE%
IF NOT EXIST %V9XARMFILE% GOTO FAILED
ECHO Runtime Direct3D is PERMITTED (the default) and the Intel flip is
ECHO REFUSED on the next and every later boot.
ECHO.
ECHO If the machine hangs: power off, boot to real DOS, run V9X3D OFF, and
ECHO collect C:\V9XDIAG on the development host before booting Windows again.
GOTO DONE

:WANTOFF
REM No refusal on this path, deliberately. This is the recovery direction, and
REM a recovery command that can decline to run is not one. It rewrites the arm
REM file the way V9XCOPY does, so an unresolved armed record is lost - that is
REM the cost, and it is the right way round: a machine that will not boot
REM matters more than a diagnostic record from a previous run.
ECHO [Velocity9x]>%V9XARMFILE%
ECHO IntelEnableThisBoot=0>>%V9XARMFILE%
ECHO IntelIncomplete=0>>%V9XARMFILE%
ECHO IntelRuntime3D=0>>%V9XARMFILE%
ECHO IntelFlip=0>>%V9XARMFILE%
IF NOT EXIST %V9XARMFILE% GOTO FAILED
ECHO Runtime Direct3D and the Intel flip are now REFUSED. The driver will
ECHO map the engine's windows, write no ring register, claim no Direct3D
ECHO capability and decline every Flip.
ECHO.
ECHO Cold boot on AC power. If the machine still hangs, the cause is not the
ECHO runtime 3D path and RECOVER.TXT describes how to remove the driver.
GOTO DONE

:USAGE
ECHO Usage:  V9X3D ON     runtime Direct3D on, the Intel flip off
ECHO         V9X3D FLIP   both on (the default for a fresh install)
ECHO         V9X3D OFF    both off, and recover a machine that hung
ECHO.
ECHO Nothing was written.
GOTO DONE

:NOTHERE
ECHO FAIL: run this from inside the package folder, beside V9XDISP.DRV.
GOTO DONE

:NODIAG
ECHO FAIL: C:\V9XDIAG does not exist. Boot the driver once first.
GOTO DONE

:INFLIGHT
ECHO FAIL: %V9XARMFILE% still carries IntelInFlight.
ECHO A previous armed run did not resolve. Collect C:\V9XDIAG first, then run
ECHO V9XCOPY to clear it deliberately, then run V9X3D ON again.
GOTO DONE

:UNRESOLVED
ECHO FAIL: %V9XARMFILE% does not state IntelIncomplete=0.
ECHO Either it predates that flag, or its value is empty or malformed.
ECHO Whether the previous armed run resolved cannot be told from here. Boot
ECHO once to let the driver normalise it, or run V9XCOPY to reset it.
GOTO DONE

:FAILED
ECHO FAIL: could not write %V9XARMFILE%.
GOTO DONE

:DONE
PAUSE
