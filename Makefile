CFLAGS = -O2 -ggdb -Wall -Wextra -std=c99 -Wno-unused-parameter -Wno-switch
LDFLAGS = -lm
CC = gcc
appname = arachnes
moviename = arach_movie

all: bin/$(appname) bin/$(moviename)
clean:
	rm -f bin/$(appname) bin/$(moviename) bin/*.o
.PHONY: all clean valgrind test

sdl_cflags := $(shell pkg-config --cflags sdl2)
sdl_libs := $(shell pkg-config --libs sdl2)
override CFLAGS += $(sdl_cflags)
override LIBS += $(sdl_libs)

bin/%.o: %.c
	mkdir -p bin
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

bin/%.o: mappers/%.c
	mkdir -p bin
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

bin/$(appname): bin/emu_nes.o  bin/nes_cpu.o  bin/nes_ppu.o bin/controller.o bin/cartridge.o bin/nes_apu.o bin/nrom_00.o bin/mmc1_01.o bin/unrom_02.o bin/cnrom_03.o bin/mmc3_04.o bin/axrom_07.o bin/arach_play.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

bin/$(moviename): bin/emu_nes.o  bin/nes_cpu.o  bin/nes_ppu.o bin/controller.o bin/cartridge.o bin/nes_apu.o bin/nrom_00.o bin/mmc1_01.o bin/unrom_02.o bin/cnrom_03.o bin/mmc3_04.o bin/axrom_07.o bin/arach_movie.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

valgrind: bin/$(appname)
	valgrind --log-file=valgrind.log bin/arachnes nestest.nes

test:
	bin/arachnes nestest.nes
