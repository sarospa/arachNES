#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nrom_00.h"
#include "cnrom_03.h"
#include "../emu_nes.h"
#include "../cartridge.h"
#include "../nes_cpu.h"
#include "../nes_ppu.h"

const unsigned int CNROM_BANK_SIZE = 0x2000;
unsigned char cnrom_bank_select = 0;

void cnrom_03_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		fixed_get_pointer_at_prg_address(data, address, access_type);
	}
	else // access_type == WRITE
	{
		cnrom_bank_select = *data % chr_rom_pages;
	}
}

void cnrom_03_access_chr_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		unsigned char bank = cnrom_bank_select % chr_rom_pages;
		unsigned int chr_rom_address = address + (bank * CNROM_BANK_SIZE);
		*data = chr_rom[chr_rom_address];
	}
}

void cnrom_03_save_state(FILE* save_file)
{
	fwrite(&cnrom_bank_select, sizeof(char), 1, save_file);
}

void cnrom_03_load_state(FILE* save_file)
{
	fread(&cnrom_bank_select, sizeof(char), 1, save_file);
}