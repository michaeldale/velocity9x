@ECHO OFF
ECHO Velocity9x Intel runtime Direct3D permission
ECHO.
REM This is NOT an armer. It sets a RETAINED permission that every subsequent
REM boot reads, rather than a one-shot token that the next boot consumes.
REM
REM What it governs: whether the display driver may bring the command ring up
REM and advertise Direct3D to applications. With it clear - which is how a
REM freshly deployed machine reads, because V9XCOPY does not write the key -
REM the driver maps the engine's windows and stops there. It writes no ring
REM register, claims no Direct3D capability, and DirectDraw runs exactly as it
REM did before any of this existed.
REM
REM It is deliberately absent by default. The machine this package installs on
REM has never executed application geometry on this engine; a package that
REM turned the ring on by being installed would take that decision on the
REM operator's behalf, and there would be nothing to reverse it with.
REM
REM TO RECOVER after a hang, from real DOS, either:
REM     V9X3D OFF
REM or, equivalently and with everything else reset too:
REM     C:
REM     CD \INTELGMA
REM     V9XCOPY
REM V9XCOPY rewrites the arm file without this key, so it disables the runtime
REM path as a side effect of resetting the arm state. That is intended: the
REM documented recovery command already turns this off.
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
GOTO USAGE

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
IF NOT EXIST %V9XARMFILE% GOTO FAILED
ECHO Runtime Direct3D is now PERMITTED on the next and every later boot.
ECHO.
ECHO This is the experiment, not a release. No guest has ever executed a
ECHO stream built from application geometry on this engine. Shut down, cold
ECHO boot on AC power, and run one Direct3D program.
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
IF NOT EXIST %V9XARMFILE% GOTO FAILED
ECHO Runtime Direct3D is now REFUSED. The driver will map the engine's
ECHO windows, write no ring register and claim no Direct3D capability.
ECHO.
ECHO Cold boot on AC power. If the machine still hangs, the cause is not the
ECHO runtime 3D path and RECOVER.TXT describes how to remove the driver.
GOTO DONE

:USAGE
ECHO Usage:  V9X3D ON     permit runtime Direct3D from the next boot
ECHO         V9X3D OFF    refuse it, and recover a machine that hung
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
