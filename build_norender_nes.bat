cls
gcc -c nes_cpu.c -o bin\nes_cpu.o -Wall -Wextra
gcc -c nes_ppu.c -o bin\nes_ppu.o -Wall -Wextra
gcc -c emu_nes.c -o bin\emu_nes.o -Wall -Wextra
gcc bin\nes_cpu.o bin\nes_ppu.o bin\emu_nes.o -o bin\arachNES.exe -Wall -Wextra