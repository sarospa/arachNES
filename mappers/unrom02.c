#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unrom02.h"
#include "..\emu_nes.h"
#include "..\cartridge.h"
#include "..\nes_cpu.h"
#include "..\nes_ppu.h"

const unsigned int BANK_SIZE = 0x4000;
unsigned char bank_select;

unsigned char* unrom02_get_pointer_at_prg_address(unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		// 0x6000 through 0x7FFF are for the cartridge's PRG RAM.
		if (address >= 0x6000 && address <= 0x7FFF)
		{
			return &prg_ram[address - 0x6000];
		}
		// 0x6000 through 0x7FFF are for the switchable bank.
		else if (address >= 0x8000 && address <= 0xBFFF)
		{
			unsigned char bank = bank_select % prg_rom_pages;
			unsigned int prg_rom_address = (address - 0x8000) + (bank * BANK_SIZE);
			return &prg_rom[prg_rom_address];
		}
		// 0x8000 through 0xBFFF are for the last bank, always fixed to this address range.
		else if (address >= 0xC000 && address <= 0xFFFF)
		{
			unsigned char bank = prg_rom_pages - 1;
			unsigned int prg_rom_address = (address - 0xC000) + (bank * BANK_SIZE);
			return &prg_rom[prg_rom_address];
		}
		else
		{
			return &dummy;
			return NULL;
		}
	}
	else
	{
		return &bank_select;
	}
}

void unrom02_init()
{
	bank_select = 0;
}