#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nrom_00.h"
#include "cnrom_03.h"
#include "../emu_nes.h"
#include "../cartridge.h"
#include "../nes_cpu.h"
#include "../nes_ppu.h"

const unsigned int AXROM_BANK_SIZE = 0x8000;
unsigned char axrom_bank_select = 0;
unsigned char axrom_mirroring = 0;

void axrom_07_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		unsigned char bank = axrom_bank_select % (prg_rom_pages / 2);
		unsigned int prg_rom_address = (address - 0x8000) + (bank * AXROM_BANK_SIZE);
		*data = prg_rom[prg_rom_address];
	}
	else // access_type == WRITE
	{
		axrom_bank_select = *data % (prg_rom_pages / 2);
		axrom_mirroring = (*data >> 4) & 0b1;
	}
}

void axrom_07_access_nametable(unsigned char* data, unsigned int address, unsigned char access_type)
{
	unsigned int nametable_address = (address % 0x400) + (axrom_mirroring * 0x400);
	if (access_type == READ)
	{
		*data = ppu_ram[nametable_address];
	}
	else
	{
		ppu_ram[nametable_address] = *data;
	}
}

void axrom_07_save_state(FILE* save_file)
{
	fwrite(&axrom_bank_select, sizeof(char), 1, save_file);
	fwrite(&axrom_mirroring, sizeof(char), 1, save_file);
}

void axrom_07_load_state(FILE* save_file)
{
	fread(&axrom_bank_select, sizeof(char), 1, save_file);
	fread(&axrom_mirroring, sizeof(char), 1, save_file);
}