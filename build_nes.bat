cls
gcc -c nes_cpu.c -o bin\nes_cpu.o -Wall -Wextra -m32
gcc -c nes_ppu.c -o bin\nes_ppu.o -Wall -Wextra -m32
gcc -c nes_apu.c -o bin\nes_apu.o -Wall -Wextra -m32
gcc -c controller.c -o bin\controller.o -Wall -Wextra -m32
gcc -c cartridge.c -o bin\cartridge.o -Wall -Wextra -m32
gcc -c mappers\nrom00.c -o bin\nrom00.o -Wall -Wextra -m32
gcc -c mappers\unrom02.c -o bin\unrom02.o -Wall -Wextra -m32
gcc -c emu_nes.c -IC:\C_Libraries\include\SDL2 -LC:\C_Libraries\lib -w -Wl,-subsystem,windows -lmingw32 -lSDL2main -lSDL2 -o bin\emu_nes.o -Wall -Wextra -m32
gcc -c arach_play.c -o bin\arach_play.o -Wall -Wextra -m32
gcc -c arach_movie.c -o bin\arach_movie.o -Wall -Wextra -m32
gcc bin\nes_cpu.o bin\nes_ppu.o bin\emu_nes.o bin\controller.o bin\cartridge.o bin\nrom00.o bin\unrom02.o bin\nes_apu.o bin\arach_play.o -IC:\C_Libraries\include\SDL2 -LC:\C_Libraries\lib -w -Wl,-subsystem,windows -lmingw32 -lSDL2main -lSDL2 -o bin\arachNES.exe -Wall -Wextra -m32
gcc bin\nes_cpu.o bin\nes_ppu.o bin\emu_nes.o bin\controller.o bin\cartridge.o bin\nrom00.o bin\unrom02.o bin\nes_apu.o bin\arach_movie.o -IC:\C_Libraries\include\SDL2 -LC:\C_Libraries\lib -w -Wl,-subsystem,windows -lmingw32 -lSDL2main -lSDL2 -o bin\arachNESmovie.exe -Wall -Wextra -m32