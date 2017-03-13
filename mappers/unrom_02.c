#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unrom_02.h"
#include "../emu_nes.h"
#include "../cartridge.h"
#include "../nes_cpu.h"
#include "../nes_ppu.h"

const unsigned int UNROM_BANK_SIZE = 0x4000;
unsigned char unrom_bank_select = 0;

void unrom02_get_pointer_at_prg_address(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		// 0x6000 through 0x7FFF are for the cartridge's PRG RAM.
		if (address >= 0x6000 && address <= 0x7FFF)
		{
			*data = prg_ram[address - 0x6000];
		}
		// 0x6000 through 0x7FFF are for the switchable bank.
		else if (address >= 0x8000 && address <= 0xBFFF)
		{
			unsigned char bank = unrom_bank_select % prg_rom_pages;
			unsigned int prg_rom_address = (address - 0x8000) + (bank * UNROM_BANK_SIZE);
			*data = prg_rom[prg_rom_address];
		}
		// 0x8000 through 0xBFFF are for the last bank, always fixed to this address range.
		else if (address >= 0xC000 && address <= 0xFFFF)
		{
			unsigned char bank = prg_rom_pages - 1;
			unsigned int prg_rom_address = (address - 0xC000) + (bank * UNROM_BANK_SIZE);
			*data = prg_rom[prg_rom_address];
		}
	}
	else // access_type == WRITE
	{
		unrom_bank_select = *data % prg_rom_pages;
	}
}

void unrom02_save_state(FILE* save_file)
{
	fwrite(&unrom_bank_select, sizeof(char), 1, save_file);
}

void unrom02_load_state(FILE* save_file)
{
	fread(&unrom_bank_select, sizeof(char), 1, save_file);
}