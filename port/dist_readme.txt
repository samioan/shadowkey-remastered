Shadowkey Remastered
====================

A PC port of The Elder Scrolls Travels: Shadowkey (N-Gage, 2004), rebuilt
from the original game's own code.


WHAT YOU NEED
-------------

This download does NOT include the game. It cannot: the game is Bethesda's,
and the font it draws with is Nokia's. You supply both.

  1. Your own copy of the N-Gage game files. The launcher takes the folder
     you extracted them to and finds the right subfolder itself -- you do
     not need to hunt for "system\apps\6r51".

  2. Optional: Ceurope.gdr, the N-Gage's system font. Without it the game
     runs perfectly well, but the menus draw with stand-in letters instead
     of the real ones. If you have an N-Gage ROM or an EKA2L1 setup, the
     launcher will usually find this on its own.


HOW TO PLAY
-----------

Run Shadowkey.exe. Point it at your game folder. Press Play.

Everything you add is copied into this folder, so you can delete or move
your original download afterwards and nothing breaks.


CONTROLS
--------

  Moving

    W / S            walk forward / backward
    A / D            turn left / right
    Left / Right     sidestep
    Up / Down        look up / down
    Space            jump

  Doing things

    E                use - doors, people, chests, anything you can pick up
    Left mouse       attack with your right hand
    Q                attack with your left hand
    G                cycle what is in your right hand
    C                cycle what is in your left hand

  Screens

    Tab              character manager - inventory, spells, stats
    M                map
    Enter            confirm
    Esc              back / cancel. During play this opens the menu,
                     which is where Save Game lives.

All of these can be rebound in Options -> Customize Controls. The window
size (2x, 3x or 4x) is set in the launcher, not in the game.

These are the N-Gage's own controls mapped onto a keyboard: the original
had a D-pad and a numeric keypad, so W/A/S/D is the D-pad and the rest of
the keys stand in for the number keys.


WHERE YOUR FILES GO
-------------------

  data\    the game files you supplied
  user\    your saved games, your settings, and the log
  bin\     the game engine itself

To uninstall, delete this folder. Nothing is written anywhere else -- no
registry keys, no AppData, no installer.

To move it to another drive, move the whole folder. To back up your saves,
copy user\.


IF SOMETHING GOES WRONG
-----------------------

user\shadowkey_port.log records what the game did, including anything it
could not do. Attach it to a bug report.


CREDITS
-------

The Elder Scrolls and Shadowkey are trademarks of ZeniMax Media Inc. This
project is not affiliated with or endorsed by Bethesda Softworks, ZeniMax,
Vir2L Studios or Nokia. See NOTICE.md.
