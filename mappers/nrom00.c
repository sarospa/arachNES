#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nrom00.h"
#include "../emu_nes.h"
#include "../cartridge.h"
#include "../nes_cpu.h"
#include "../nes_ppu.h"

void nrom00_get_pointer_at_prg_address(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		// 0x6000 through 0x7FFF are for the cartridge's PRG RAM.
		if (address >= 0x6000 && address <= 0x7FFF)
		{
			*data = prg_ram[address - 0x6000];
		}
		// 0x8000 through 0xBFFF addresses the first 16KB bytes of ROM.
		// 0xC000 through 0xFFFF addresses the second 16KB bytes if the exist, otherwise it mirrors the first 16KB instead.
		else if (address >= 0x8000 && address <= 0xFFFF)
		{
			unsigned int prg_rom_address = (address - 0x8000) % prg_rom_size;
			*data = prg_rom[prg_rom_address];
		}
	}
}

// For mappers that do not use CHR bank-switching.
// Multiple mappers could used this.
void fixed_get_pointer_at_chr_address(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		// Typically the pattern tables would be stored here.
		if (address <= 0x1FFF)
		{
			if (use_chr_ram)
			{
				*data = chr_ram[address];
			}
			else
			{
				*data = chr_rom[address];
			}
		}
	}
	else // access type == WRITE
	{
		if (use_chr_ram)
		{
			chr_ram[address] = *data;
		}
	}
}

// For mappers that use a fixed horizontal or vertical nametable configuration.
// Multiple mappers could use this.
void fixed_get_pointer_at_nametable_address(unsigned char* data, unsigned int address, unsigned char access_type)
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
	if (access_type == READ)
	{
		*data = ppu_ram[nametable_address];
	}
	else
	{
		ppu_ram[nametable_address] = *data;
	}
}

void nrom00_init()
{
	// Nothing to do here.
}