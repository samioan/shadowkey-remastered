<!--
M114: the body of every GitHub release, with {{TAG}} substituted by
.github/workflows/release.yml. Kept as a file rather than inline YAML so
that changing what a release says is a normal edit and not a workflow edit.
-->
## Shadowkey Remastered {{TAG}}

A PC port of *The Elder Scrolls Travels: Shadowkey* (N-Gage, 2004), rebuilt
from the original game's own code.

### Getting it running

1. Download `ShadowkeyRemastered-{{TAG}}-win64.zip` below and unzip it
   anywhere.
2. Run **Shadowkey.exe**.
3. Point it at your own copy of the N-Gage game files. Pick the folder you
   extracted them to — it finds the right subfolder itself.
4. Press **Play**.

Optionally, give it `Ceurope.gdr` (the N-Gage's system font) as well. Without
it the game runs perfectly, but menus draw with stand-in letters instead of
the real ones.

### This download does not include the game

It cannot: the game is Bethesda's and the font is Nokia's. You supply both.
Nothing here will work without your own copy.

### Notes

- Windows 64-bit. No installer and no Visual C++ redistributable — unzip and
  run.
- Everything stays in the folder you unzipped: your game files are copied to
  `data\`, saves and settings go to `user\`. To uninstall, delete the folder.
  No registry keys, nothing in AppData.
- If something goes wrong, `user\shadowkey_port.log` says what the game did.
  Attach it to a bug report.

*The Elder Scrolls* and *Shadowkey* are trademarks of ZeniMax Media Inc.
This project is not affiliated with or endorsed by Bethesda Softworks,
ZeniMax, Vir2L Studios or Nokia.
