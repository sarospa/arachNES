# arachNES
"Average NES emulator contains 3 spiders" factoid actually just statistical error...

If you're reading this somewhere other than github, you can find the source code at https://github.com/sarospa/arachNES<br />
If you distribute a binary of arachNES to anyone else, please include the license and this readme. If either of them are missing (how would you be reading this if the readme is missing?) you can find them at the project github.

An NES emulator written in C. Still very much a work in progress, and there's probably bits and pieces of personal notes that are not useful to anyone but myself.

Feel free to try building it if you want, just bear in mind that it requires SDL2 to work.

arachNES has two binaries, arachnes.exe and arach_movie.exe. They run from the command line; 'arachnes.exe <rom>' runs the chosen ROM, and 'arach_movie.exe <rom> <movie>' runs the chosen ROM and plays the inputs from the chosen movie. There's no checking that the ROM and the movie actually match right now, so do be careful of that.

I'm not including any ROMs here, for what I hope are fairly obvious reasons, but a number of test ROMs can be found at http://wiki.nesdev.com/w/index.php/Emulator_tests The one I'm working with right now is nestest.

The emulator gets its palette from palettes\ntscpalette.pal. The palette will likely be subject to change, and you can use your own if you want. It was generated with http://bisqwit.iki.fi/utils/nespalette.php or you could modify it yourself - it's just 64 RGB triplets.

If you'd like to submit an issue, please prepend the issue title with the name of the game that the issue was found in, or (in the case of test ROMs or other non-game ROMs) the name of the ROM itself. Currently, the following mappers are supported:

00: NROM<br />
01: MMC1<br />
02: UNROM<br />
04: MMC3<br />
07: AxROM<br />
09: MMC2

If you aren't sure what mapper a game has, the emulator itself should report a warning that a ROM's mapper is unsupported, if you're logging the output to a file. If not, it's fairly safe to assume that if the game doesn't boot, its mapper is not supported. If you wish to open an issue for a game not booting, please confirm that the ROM has a supported mapper first.

Here's the controls! They aren't currently rebindable, but that functionality will come, eventually, probably.

Controller:<br />
Control Pad: Control Pad<br />
A Button: A or Y [X or Triangle on Playstation controller]<br />
B Button: B or X [Square or Circle on Playstation controller]<br />
Start: Start<br />
Select: Select<br />
Save State: Left shoulder button<br />
Load State: Right shoulder button

Keyboard:<br />
Control Pad: Arrow keys<br />
A Button: Z<br />
B Button: X<br />
Start: Enter<br />
Select: Shift<br />
Save State: F1<br />
Load State: F2

Debug Keys:<br />
Toggle pulse 1 enable: 1<br />
Toggle pulse 2 enable: 2<br />
Toggle triangle enable: 3<br />
Toggle noise disable: 4<br />
Toggle emulator pause: Pause<br />
Output sound debug (slows down the emulator a lot): S<br />
ROM dump: R<br />
Nametable dump: N<br />
Pattern table dump: P



    Copyright (C) 2016  Christopher Rohrbaugh

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
