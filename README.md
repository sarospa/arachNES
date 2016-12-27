# arachNES
"Average NES emulator contains 3 spiders" factoid actually just statistical error...

If you're reading this somewhere other than github, you can find the source code at https://github.com/sarospa/arachNES
If you distribute a binary of arachNES to anyone else, please include the license and this readme. If either of them are missing (how would you be reading this if the readme is missing?) you can find them at the project github.

An NES emulator written in C. Still very much a work in progress, and there's probably bits and pieces of personal notes that are not useful to anyone but myself.

Feel free to try building it if you want, just bear in mind that it requires SDL2 to work.

I'm not including any ROMs here, for what I hope are fairly obvious reasons, but a number of test ROMs can be found at http://wiki.nesdev.com/w/index.php/Emulator_tests The one I'm working with right now is nestest.

The emulator gets its palette from palettes\ntscpalette.pal. The palette will likely be subject to change, and you can use your own if you want. It was generated with http://bisqwit.iki.fi/utils/nespalette.php or you could modify it yourself - it's just 64 RGB triplets.

If you'd like to submit an issue, please prepend the issue title with the name of the game that the issue was found in, or (in the case of test ROMs or other non-game ROMs) the name of the ROM itself. Currently, only NROM games are supported, and non-NROM games are not expected to boot. If a game does not boot at all and you wish to open an issue, please first open the ROM in a hex editor and check if byte $06 and byte $07 both have a 0 in their high hex digit. If yes, then it's NROM. If no, then it uses some other mapper, and knowing that it does not boot will not currently be helpful. As I add mapper support, I may give a list of supported mappers, but if you aren't sure, the emulator itself should report a warning that a ROM's mapper is unsupported, if you're logging the output to a file.

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