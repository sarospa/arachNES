#Roadmap

This is my plan for where arachNES, as a project, is ultimately going. Since this is primarily a hobby project, the goal here is to follow my interests, rather than worrying too much about making a great emulator. There's no need for me to compete with all the great NES emulators that are already out there, when I'm just doing this for fun. That said, here are the items I want to finish before I'll consider this project complete.

##1. Emulating the stock NES hardware

The obvious first step. This covers the console itself, the controllers, the basic ability to emulate cartridge hardware, and any related I/O. This is basically done already. There are still a few lingering details and quirks of the NES remaining, mostly little-used features or obscure PPU behaviors that aren't often relevant to emulation. But for the most part, the NES is complete, and the lion's share of the work will go into emulating mappers.

##2. Mapper coverage for the officially-licensed NES/Famicom library

Most of my mapper work will be focused on covering all officially-licensed games. This includes all regions, and I might tentatively extend this to support for the PAL NES if it's necessary for PAL-specific carts.

##3. Accurate emulation of key games in the NES/Famicom library

I'm not going to try and make sure /every/ game works in arachNES. That's simply too much work, and I don't really have anyone to help me test these games. However, there are a handful of key games that I want to fully support. The list isn't decided yet, but it will consist mainly of popular, well-known games (Super Mario Bros, Castlevania, Final Fantasy, etc) and games known to have difficult-to-emulate quirks (Battletoads, Micro Machines, etc). Once I've figured out the full list, it'll go here. This probably won't be a high priority until I've covered all the necessary mappers, though.

##4. Zapper support

I'm not going to support terribly many NES peripherals, but come on. You gotta have the Zapper.

##5. Support for the Famicom Disk System

This is perhaps the most ambitious item here, and it isn't going to be a priority until pretty much everything else on this list is squared away. Although the FDS is not a part of the NES's history here in the US, I feel that it's too important, too unique, and most of all, too interesting to possibly leave out. I guess you could say that this will be the final boss of arachNES's development.