# arachNES
"Average NES emulator contains 3 spiders" factoid actually just statistical error...

An NES emulator written in C. Still very much a work in progress, and there's probably bits and pieces of personal notes that are not useful to anyone but myself.

Feel free to try building it if you want, just bear in mind that it requires SDL to work. SDL.dll is included, but I'm still figuring out how C libraries work and some assembly may be required. build_nes.bat is a quick-and-easy way to build the project, although it probably won't help you much if you aren't building on Windows with MinGW gcc. run_nes.bat does the same thing but also launches the EXE. build_norender_nes.bat builds without SDL, although it requires #define RENDER to be set to 0 in emu_nes.c or else thing go a bit sideways.

There's lots of debugging junk at the moment. I'll try not to make commits that say, output several MB per second, but it tends to change around a lot depending on what I'm trying to fix.

I'm not including any ROMs here, for what I hope are fairly obvious reasons, but a number of test ROMs can be found at http://wiki.nesdev.com/w/index.php/Emulator_tests The one I'm working with right now is nestest.

Inputs now work! Current settings are: arrow keys for D-pad, shift for select, enter for start, Z for A, and X for B. Configurable mappings will come later. Also supports controllers, if you can get that to work it should work about as expected, probably.

The emulator gets its palette from palettes\ntscpalette.pal. The palette will likely be subject to change, and you can use your own if you want. It was generated with http://bisqwit.iki.fi/utils/nespalette.php or you could modify it yourself - it's just 64 RGB triplets.

If you'd like to submit an issue, please prepend the issue title with the name of the game that the issue was found in, or (in the case of test ROMs or other non-game ROMs) the name of the ROM itself. Currently, only NROM games are supported, and non-NROM games are not expected to boot. If a game does not boot at all and you wish to open an issue, please first open the ROM in a hex editor and check if byte $06 and byte $07 both have a 0 in their high hex digit. If yes, then it's NROM. If no, then it uses some other mapper, and knowing that it does not boot will not currently be helpful. As I add mapper support, I may give a list of supported mappers, but if you aren't sure, the emulator itself should report a warning that a ROM's mapper is unsupported, if you're logging the output to a file.
