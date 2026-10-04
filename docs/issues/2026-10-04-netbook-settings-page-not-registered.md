# Netbook: the Velocity9x tab is missing from Display Properties

Date: 2026-10-04. Machine: MICHAEL-NETBOOK (945GSE), build `795ea49`
installed by WININIT rename the same day. Status: open. Evidence:
`docs/probe/netbook-vsync-and-textures-2026-10-04/display-properties-no-velocity9x-tab.png`.

## Symptom

Display Properties, opened through `RUNDLL32 shell32.dll,Control_RunDLL
desk.cpl`, shows Background, Screen Saver, Appearance, Effects, Web and
Settings, but no Velocity9x tab. `C:\WINDOWS\SYSTEM\V9XSETP.DLL` is
present: 48,128 bytes before the deploy and 50,688 after.

## What is known

- The page appears only when the shell handler is registered. The INF
  does that through `RunOnce rundll32 v9xsetp.dll,V9xRegisterPage`,
  which writes the machine-specific Tag (`settings_propsheet.c`,
  `V9xRegisterPage`). This machine's driver was first installed through
  Device Manager on 2026-09-24, and every update since has been a
  WININIT rename, which runs no INF.
- Not checked: whether the handler key exists without a valid Tag (the
  shell deletes such a key), or was never written. Nobody read the
  registry.

## Next

Read `HKLM\...\Controls Folder\Display\shellex\PropertySheetHandlers`
on the netbook. If the key is absent, run `V9xRegisterPage` once, with
Michael's agreement, since it writes persistent shell registration.
