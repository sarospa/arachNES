cls
gcc -c nes_cpu.c -o bin\nes_cpu.o -Wall -Wextra
gcc -c nes_ppu.c -o bin\nes_ppu.o -Wall -Wextra
gcc -c controller.c -o bin\controller.o -Wall -Wextra
gcc -c emu_nes.c -IC:\C_Libraries\include\SDL2 -LC:\C_Libraries\lib -w -Wl,-subsystem,windows -lmingw32 -lSDL2main -lSDL2 -o bin\emu_nes.o -Wall -Wextra
gcc bin\nes_cpu.o bin\nes_ppu.o bin\emu_nes.o bin\controller.o -IC:\C_Libraries\include\SDL2 -LC:\C_Libraries\lib -w -Wl,-subsystem,windows -lmingw32 -lSDL2main -lSDL2 -o bin\arachNES.exe -Wall -Wextra