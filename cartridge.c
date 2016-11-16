#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cartridge.h"
#include "nes_cpu.h"
#include "nes_ppu.h"

const int PRG_ROM_PAGE = 1024 * 16;
const int CHR_ROM_PAGE = 1024 * 8;
const unsigned char HORIZONTAL = 0;
const unsigned char VERTICAL = 0;

unsigned char* prg_rom;
unsigned char* prg_ram;
unsigned char* chr_rom;
unsigned char* chr_ram;

unsigned int prg_rom_size;
unsigned int chr_rom_size;
unsigned char use_chr_ram;
unsigned char nametable_mirroring;

unsigned char* get_pointer_at_prg_address(unsigned int address)
{
	// 0x6000 through 0x7FFF are for the cartridge's PRG RAM.
	if (address >= 0x6000 && address <= 0x7FFF)
	{
		return &prg_ram[address - 0x6000];
	}
	// Assuming for now that we're using mapper type 0.
	// 0x8000 through 0xBFFF addresses the first 16KB bytes of ROM.
	// 0xC000 through 0xFFFF addresses the second 16KB bytes if the exist, otherwise it mirrors the first 16KB instead.
	else if (address >= 0x8000 && address <= 0xFFFF)
	{
		unsigned int prg_rom_address = (address - 0x8000) % prg_rom_size;
		return &prg_rom[prg_rom_address];
	}
	else
	{
		printf("Unhandled PRG address %04X\n", address);
		exit_emulator();
		return NULL;
	}
}

unsigned char* get_pointer_at_chr_address(unsigned int address, unsigned char access_type)
{
	// Typically the pattern tables would be stored here.
	if (address <= 0x1FFF)
	{
		if (use_chr_ram)
		{
			return &chr_ram[address];
		}
		else
		{
			if (access_type == WRITE)
			{
				printf("Illegally attempting to write to CHR ROM at address %04X.\n", address);
				exit_emulator();
			}
			return &chr_rom[address];
		}
	}
	else
	{
		printf("Unhandled CHR address %04X\n", address);
		exit_emulator();
		return NULL;
	}
}

// The cartridge has control over how the PPU accesses its RAM, normally controlling mirroring.
// It could even alter it to point to cartridge RAM instead.
unsigned char* get_pointer_at_nametable_address(unsigned int address)
{
	unsigned int nametable_address;
	if (nametable_mirroring == HORIZONTAL)
	{
		nametable_address = address % 0x400;
		if ((address & 0x800) == 0x800)
		{
			nametable_address += 0x400;
		}
	}
	else
	{
		nametable_address = address % 0x800;
	}
	return &ppu_ram[nametable_address];
}

// Initializes the cartridge. The mapper is the internal hardware that handles how cartridge addresses work.
// mirroring controls how PPU nametables are mirrored. 0 is horizontal and 1 is vertical.
void cartridge_init(unsigned char mapper, unsigned char prg_rom_pages, unsigned char chr_rom_pages, unsigned char mirroring, FILE* rom)
{
	if (mapper != 0)
	{
		printf("Warning: Unsupported mapper %02X.\n", mapper);
	}
	
	prg_rom_size = prg_rom_pages * PRG_ROM_PAGE;
	prg_rom = malloc(sizeof(char) * prg_rom_size);
	fread(prg_rom, 1, prg_rom_size, rom);
	
	prg_ram = malloc(sizeof(char) * KB * 8);
	
	chr_rom_size = chr_rom_pages * CHR_ROM_PAGE;
	if (chr_rom_size == 0)
	{
		chr_ram = malloc(sizeof(char) * KB * 8);
		use_chr_ram = 1;
	}
	else
	{
		chr_rom = malloc(sizeof(char) * chr_rom_size);
		fread(chr_rom, 1, chr_rom_size, rom);
		use_chr_ram = 0;
	}
	nametable_mirroring = mirroring;
}