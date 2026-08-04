KOLOBOK - an MS-DOS VGA platform game
=====================================

Grandmother and Grandfather need eight forest berries. Roll away from the
cottage, collect every berry, then return home. Hazards send Kolobok back to
the checkpoint, but collected berries are kept. There are no limited lives.

CONTROLS
  Left/Right or A/D   Roll
  Space or Up         Jump (hold for a higher jump)
  S                   Toggle PC-speaker sound
  Escape              Pause; Escape again to quit
  Enter               Start/resume/restart after winning

FILES
  KOLOBOK.EXE and KOLOBOK.DAT must be in the same current directory.

OPTIONS
  KOLOBOK.EXE -nosound    Disable PC-speaker effects
  KOLOBOK.EXE -selftest   Run the automated target test and exit
  KOLOBOK.EXE -benchmark  Measure render and 30 Hz pacing performance
  KOLOBOK.EXE -capture    Write a deterministic KOLOBOK.PPM screenshot

REQUIREMENTS
  386DX-40 or faster, VGA, DOS 3.3 or later, and at least 160 KB of free
  conventional memory. The game uses planar 320x200x256 Mode X.

Source and build instructions: https://github.com/ddanila/kolobok
License: MIT
