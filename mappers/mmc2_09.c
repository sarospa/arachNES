#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mmc2_09.h"
#include "../emu_nes.h"
#include "../cartridge.h"
#include "../nes_cpu.h"
#include "../nes_ppu.h"

unsigned int MMC2_PRG_ROM_BANK = 0x2000;
unsigned int MMC2_CHR_ROM_BANK = 0x1000;

unsigned char mmc2_prg_bank_select = 0;
unsigned char mmc2_chr_bank_left_FD_select = 0;
unsigned char mmc2_chr_bank_left_FE_select = 0;
// Flag for the left pattern table bank select. 0 for $FD, 1 for $FE.
unsigned char mmc2_chr_bank_left_select = 0;
unsigned char mmc2_chr_bank_right_FD_select = 0;
unsigned char mmc2_chr_bank_right_FE_select = 0;
// Flag for the right pattern table bank select. 0 for $FD, 1 for $FE.
unsigned char mmc2_chr_bank_right_select = 0;
unsigned char mmc2_mirroring_select = 0;

void mmc2_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		 // PRG RAM
		if ((address >= 0x6000) && (address <= 0x7FFF))
		{
			*data = prg_ram[address & 0x1FFF];
		}
		// PRG ROM
		else
		{
			// Which of the four 8 KB CPU banks the address is in.
			unsigned char cpu_bank = (address & 0x6000) >> 13;
			unsigned int bank_address = address & 0x1FFF;
			unsigned char bank_select = 0;
			switch (cpu_bank)
			{
				case 0b00:
				{
					// Switchable bank
					bank_select = mmc2_prg_bank_select;
					break;
				}
				case 0b01:
				{
					// Fixed bank -3
					bank_select = (prg_rom_pages * 2) - 3;
					break;
				}
				case 0b10:
				{
					// Fixed bank -2
					bank_select = (prg_rom_pages * 2) - 2;
					break;
				}
				case 0b11:
				{
					// Fixed bank -1
					bank_select = (prg_rom_pages * 2) - 1;
					break; 
				}
			}
			*data = prg_rom[bank_address | (bank_select << 13)];
		}
	}
	else // access_type == WRITE
	{
		// PRG RAM
		if ((address >= 0x6000) && (address <= 0x7FFF))
		{
			prg_ram[address & 0x1FFF] = *data;
		}
		// PRG ROM
		{
			// Only the most significant hex digit is relevant to the register select.
			unsigned char reg_select = ((address >> 12) & 0b1111);
			switch (reg_select)
			{
				case 0xA:
				{
					mmc2_prg_bank_select = *data % (prg_rom_pages * 2);
					break;
				}
				case 0xB:
				{
					mmc2_chr_bank_left_FD_select = *data % (chr_rom_pages * 2);
					break;
				}
				case 0xC:
				{
					mmc2_chr_bank_left_FE_select = *data % (chr_rom_pages * 2);
					break;
				}
				case 0xD:
				{
					mmc2_chr_bank_right_FD_select = *data % (chr_rom_pages * 2);
					break;
				}
				case 0xE:
				{
					mmc2_chr_bank_right_FE_select = *data % (chr_rom_pages * 2);
					break;
				}
				case 0xF:
				{
					mmc2_mirroring_select = *data & 0b1;
					break;
				}
			}
		}
	}
}

void mmc2_access_chr_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		// Which of the two 4 KB PPU banks the address is in.
		unsigned char ppu_bank = (address & 0x1000) >> 12;
		// The address within the selected bank.
		unsigned int bank_address = address & 0xFFF;
		unsigned char bank_select = 0;
		switch (ppu_bank)
		{
			case 0b0:
			{
				if (mmc2_chr_bank_left_select == 0)
				{
					bank_select = mmc2_chr_bank_left_FD_select;
				}
				else
				{
					bank_select = mmc2_chr_bank_left_FE_select;
				}
				
				// Bankswitch on top row of special tiles $FD and $FE
				if (bank_address == 0xFD8)
				{
					mmc2_chr_bank_left_select = 0;
				}
				else if (bank_address == 0xFE8)
				{
					mmc2_chr_bank_left_select = 1;
				}
				break;
			}
			case 0b1:
			{
				if (mmc2_chr_bank_right_select == 0)
				{
					bank_select = mmc2_chr_bank_right_FD_select;
				}
				else
				{
					bank_select = mmc2_chr_bank_right_FE_select;
				}
				
				// Bankswitch on all rows of special tiles $FD and $FE
				if ((bank_address & 0xFF8) == 0xFD8)
				{
					mmc2_chr_bank_right_select = 0;
				}
				else if ((bank_address & 0xFF8) == 0xFE8)
				{
					mmc2_chr_bank_right_select = 1;
				}
				break;
			}
		}
		*data = chr_rom[bank_address | (bank_select << 12)];
	}
}

void mmc2_access_nametable_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	unsigned int nametable_address;
	if (mmc2_mirroring_select == 0b1) //horizontal
	{
		nametable_address = address % 0x400;
		if ((address & 0x800) == 0x800)
		{
			nametable_address += 0x400;
		}
	}
	else //vertical
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

void mmc2_save_state(FILE* save_file)
{
	fwrite(&mmc2_prg_bank_select, sizeof(char), 1, save_file);
	fwrite(&mmc2_chr_bank_left_FD_select, sizeof(char), 1, save_file);
	fwrite(&mmc2_chr_bank_left_FE_select, sizeof(char), 1, save_file);
	fwrite(&mmc2_chr_bank_left_select, sizeof(char), 1, save_file);
	fwrite(&mmc2_chr_bank_right_FD_select, sizeof(char), 1, save_file);
	fwrite(&mmc2_chr_bank_right_FE_select, sizeof(char), 1, save_file);
	fwrite(&mmc2_chr_bank_right_select, sizeof(char), 1, save_file);
}

void mmc2_load_state(FILE* save_file)
{
	fread(&mmc2_prg_bank_select, sizeof(char), 1, save_file);
	fread(&mmc2_chr_bank_left_FD_select, sizeof(char), 1, save_file);
	fread(&mmc2_chr_bank_left_FE_select, sizeof(char), 1, save_file);
	fread(&mmc2_chr_bank_left_select, sizeof(char), 1, save_file);
	fread(&mmc2_chr_bank_right_FD_select, sizeof(char), 1, save_file);
	fread(&mmc2_chr_bank_right_FE_select, sizeof(char), 1, save_file);
	fread(&mmc2_chr_bank_right_select, sizeof(char), 1, save_file);
}