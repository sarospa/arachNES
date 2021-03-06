#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mmc1_01.h"
#include "../emu_nes.h"
#include "../cartridge.h"
#include "../nes_cpu.h"
#include "../nes_ppu.h"

unsigned char shift_register = 0;
unsigned char shift_count = 0;
unsigned char control_register = 0x0C;
unsigned char chr_bank_0_register = 0;
unsigned char chr_bank_1_register = 0;
unsigned char prg_bank_register = 0;

// MMC1 is a mapper that appears on multiple boards, known as the SxROM family.
// Some of them use MMC1's registers in special ways, so we need to determine
// whether these special boards are being used, which can be inferred from
// the configuration of PRG ROM and CHR ROM.
unsigned char large_prg_banks = 0;
unsigned char extra_prg_ram = 0;

void mmc1_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		// PRG RAM
		if ((address >= 0x6000) && (address <= 0x7FFF))
		{
			*data = prg_ram[address & 0x1FFF];
		}
		// PRG ROM
		else if (address >= 0x8000)
		{
			unsigned char prg_control = (control_register >> 2) & 0b11;
			// Which of the two CPU banks the address is in (top or bottom).
			unsigned char cpu_bank = (address & 0x4000) >> 14;
			// The address within the selected bank.
			unsigned int bank_address = address & 0x3FFF;
			unsigned char bank_select = 0;
			unsigned char outer_bank_select = 0;
			unsigned char rom_pages = prg_rom_pages;
			// If we're using large PRG banks, then bit 4 of the CHR 0 bank register
			// switches two 256 KB outer PRG banks. Any 'fixed' banks should operate
			// within the outer bank.
			if (large_prg_banks)
			{
				outer_bank_select = (chr_bank_0_register >> 4) & 0b1;
				rom_pages = prg_rom_pages / 2;
			}
			switch (prg_control)
			{
				case 0b00:
				case 0b01:
				{
					bank_select = (prg_bank_register & 0b1110) + cpu_bank + (outer_bank_select * rom_pages);
					break;
				}
				case 0b10:
				{
					if (cpu_bank == 0)
					{
						bank_select = outer_bank_select * rom_pages;
					}
					else
					{
						bank_select = (prg_bank_register & 0b1111) + (outer_bank_select * rom_pages);
					}
					break;
				}
				case 0b11:
				{
					if (cpu_bank == 1)
					{
						bank_select = (rom_pages - 1) + (outer_bank_select * rom_pages);
					}
					else
					{
						bank_select = (prg_bank_register & 0b1111) + (outer_bank_select * rom_pages);
					}
					break;
				}
			}
			// Shouldn't be necessary, but there's no reason to risk reading off the end of the ROM.
			bank_select = bank_select % prg_rom_pages;
			*data = prg_rom[bank_address | (bank_select << 14)];
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
		else if (address >= 0x8000)
		{
			unsigned char reset_bit = (*data >> 7) & 0b1;
			unsigned char data_bit = *data & 0b1;
			if (reset_bit)
			{
				shift_register = 0;
				shift_count = 0;
				control_register = control_register | 0x0C;
			}
			else
			{
				shift_register = (shift_register >> 1) | (data_bit << 4);
				shift_count++;
				if (shift_count >= 5)
				{
					// Only bit 13 and 14 matter for deciding which register to use.
					switch ((address >> 13) & 0b11)
					{
						// 0x8000 through 0x9FFF
						case 0b00:
						{
							control_register = shift_register;
							break;
						}
						// 0xA000 through 0xBFFF
						case 0b01:
						{
							chr_bank_0_register = shift_register;
							break;
						}
						// 0xC000 through 0xDFFF
						case 0b10:
						{
							chr_bank_1_register = shift_register;
							break;
						}
						// 0xE000 through 0xFFFF
						case 0b11:
						{
							prg_bank_register = shift_register;
							break;
						}
					}
					shift_register = 0;
					shift_count = 0;
				}
			}
		}
	}
}

void mmc1_access_chr_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	unsigned char chr_pages = chr_rom_pages;
	if (use_chr_ram)
	{
		chr_pages = 1;
	}
	unsigned char chr_control = (control_register >> 4) & 0b1;
	// Which of the two PPU banks the address is in (top or bottom);
	unsigned char ppu_bank = (address >> 12) & 0b1;
	// The address within the selected bank.
	unsigned int bank_address = address & 0x0FFF;
	unsigned char bank_select = 0;
	if (chr_control == 0b0)
	{
		bank_select = (chr_bank_0_register & 0b11110) + ppu_bank;
	}
	else // chr_control == 0b1
	{
		if (ppu_bank == 0b0)
		{
			bank_select = chr_bank_0_register;
		}
		else // ppu_bank == 0b1
		{
			bank_select = chr_bank_1_register;
		}
	}
	bank_select = bank_select % (chr_pages * 2);
	
	if (access_type == READ)
	{
		if (use_chr_ram)
		{
			*data = chr_ram[bank_address | (bank_select << 12)];
		}
		else
		{
			*data = chr_rom[bank_address | (bank_select << 12)];
		}
	}
	else // access_type == WRITE
	{
		if (use_chr_ram)
		{
			chr_ram[bank_address | (bank_select << 12)] = *data;
		}
	}
}

void mmc1_access_nametable_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	unsigned char mirror_control = control_register & 0b11;
	unsigned int nametable_address = 0;
	switch (mirror_control)
	{
		// One screen, lower bank
		case 0b00:
		{
			nametable_address = address & 0x3FF;
			break;
		}
		// One screen, upper bank
		case 0b01:
		{
			nametable_address = (address & 0x3FF) | 0x400;
			break;
		}
		// Vertical
		case 0b10:
		{
			nametable_address = address & 0x7FF;
			break;
		}
		// Horizontal
		case 0b11:
		{
			nametable_address = address & 0x3FF;
			if ((address & 0x800) == 0x800)
			{
				nametable_address += 0x400;
			}
			break;
		}
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

void mmc1_save_state(FILE* save_file)
{
	fwrite(&shift_register, sizeof(char), 1, save_file);
	fwrite(&shift_count, sizeof(char), 1, save_file);
	fwrite(&control_register, sizeof(char), 1, save_file);
	fwrite(&chr_bank_0_register, sizeof(char), 1, save_file);
	fwrite(&chr_bank_1_register, sizeof(char), 1, save_file);
	fwrite(&prg_bank_register, sizeof(char), 1, save_file);
}

void mmc1_load_state(FILE* save_file)
{
	fread(&shift_register, sizeof(char), 1, save_file);
	fread(&shift_count, sizeof(char), 1, save_file);
	fread(&control_register, sizeof(char), 1, save_file);
	fread(&chr_bank_0_register, sizeof(char), 1, save_file);
	fread(&chr_bank_1_register, sizeof(char), 1, save_file);
	fread(&prg_bank_register, sizeof(char), 1, save_file);
}

void mmc1_init()
{
	// 512 KB ROM is 'large'. This should be 32 pages 16 KB big.
	if (prg_rom_pages == 32)
	{
		large_prg_banks = 1;
	}
	
	// If there's only 8 KB of CHR ROM/RAM, then there's room for the higher
	// bits of the CHR bank select to be used for switching PRG RAM. This
	// may add extra RAM for some boards that don't need it, but it probably
	// won't have any particular effect.
	if (chr_rom_pages <= 1)
	{
		extra_prg_ram = 1;
		prg_ram = malloc(sizeof(char) * 0x8000 * 4);
		for (unsigned int i = 0; i < 0x8000 * 4; i++)
		{
			prg_ram[i] = 0;
		}
		prg_ram_size = 0x8000;
	}
	else
	{
		prg_ram = malloc(sizeof(char) * 0x2000);
		for (unsigned int i = 0; i < 0x2000; i++)
		{
			prg_ram[i] = 0;
		}
		prg_ram_size = 0x2000;
	}
}