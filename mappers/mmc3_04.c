#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mmc3_04.h"
#include "../emu_nes.h"
#include "../cartridge.h"
#include "../nes_cpu.h"
#include "../nes_ppu.h"

unsigned char bank_select_register = 0;
unsigned char* bank_selects;
unsigned char mirroring_register = 0;
unsigned char prg_ram_protect_register = 0;
unsigned char irq_latch_register = 0;
unsigned char irq_reload_register = 0;
unsigned char irq_enable_register = 0;
unsigned char last_address_bit_12 = 0;
unsigned char holding_irq = 0;
unsigned char irq_counter = 0;

void check_irq_clock(unsigned int address)
{
	// Scanline counter is clocked when A12 changes from 0 to 1.
	unsigned char address_bit_12 = (address >> 12) & 0b1;
	if ((address_bit_12 == 1) && (last_address_bit_12 == 0))
	{
		if (irq_reload_register)
		{
			irq_reload_register = 0;
			irq_counter = irq_latch_register;
		}
		else if (irq_counter == 0)
		{
			irq_counter = irq_latch_register;
		}
		
		if (irq_counter > 0)
		{
			irq_counter--;
			if ((irq_counter == 0) && irq_enable_register)
			{
				if (!holding_irq)
				{
					pending_interrupt++;
					interrupt_type = IRQ;
				}
				holding_irq = 1;
			}
		}
	}
	
	last_address_bit_12 = address_bit_12;
}

void mmc3_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type)
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
			// The address within the selected bank.
			unsigned int bank_address = address & 0x1FFF;
			unsigned char prg_bank_mode = (bank_select_register >> 6) & 0b1;
			unsigned char bank_select = 0;
			switch (cpu_bank)
			{
				case 0b00:
				{
					if (prg_bank_mode == 1)
					{
						bank_select = (prg_rom_pages * 2) - 2;
					}
					else
					{
						bank_select = bank_selects[6];
					}
					break;
				}
				case 0b01:
				{
					bank_select = bank_selects[7];
					break;
				}
				case 0b10:
				{
					if (prg_bank_mode == 0)
					{
						bank_select = (prg_rom_pages * 2) - 2;
					}
					else
					{
						bank_select = bank_selects[6];
					}
					break;
				}
				case 0b11:
				{
					bank_select = (prg_rom_pages * 2) - 1;
					break;
				}
			}
			bank_select = bank_select % (prg_rom_pages * 2);
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
		else
		{
			unsigned char reg_select = ((address >> 12) & 0b110) | (address & 0b1);
			switch (reg_select)
			{
				// Bank select, $8000 through $9FFF even
				case 0b000:
				{
					bank_select_register = *data;
					break;
				}
				// Bank data, $8000 through $9FFF odd
				case 0b001:
				{
					unsigned char bank_index = bank_select_register & 0b111;
					bank_selects[bank_index] = *data;
					break;
				}
				// Mirroring, $A000 through $BFFF even
				case 0b010:
				{
					mirroring_register = *data;
					break;
				}
				// PRG RAM protect, $A000 through $BFFF odd
				case 0b011:
				{
					prg_ram_protect_register = *data;
					break;
				}
				// IRQ latch, $C000 through $DFFF even
				case 0b100:
				{
					irq_latch_register = *data;
					break;
				}
				// IRQ reload, $C000 through $DFFF odd
				case 0b101:
				{
					irq_reload_register = 1;
					break;
				}
				// IRQ disable, $E000 through $FFFF even
				case 0b110:
				{ 
					irq_enable_register = 0;
					if (holding_irq == 1)
					{
						pending_interrupt--;
					}
					holding_irq = 0;
					break;
				}
				// IRQ enable, $E000 through $FFFF odd
				case 0b111:
				{
					irq_enable_register = 1;
					break;
				}
			}
		}
	}
}

void mmc3_access_chr_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	check_irq_clock(address);
	
	if (access_type == READ)
	{
		// Which of the eight 1 KB PPU banks the address is in.
		unsigned char ppu_bank = (address & 0x1C00) >> 10;
		// The address within the selected bank.
		unsigned int bank_address = address & 0x3FF;
		// CHR inversion bit flips bit 12 of the bank selection.
		unsigned char chr_inversion = (bank_select_register >> 7) & 0b1;
		ppu_bank = ppu_bank ^ (chr_inversion << 2);
		unsigned char bank_select = 0;
		switch (ppu_bank)
		{
			case 0b000:
			{
				bank_select = bank_selects[0] & 0b11111110;
				break;
			}
			case 0b001:
			{
				bank_select = bank_selects[0] | 0b1;
				break;
			}
			case 0b010:
			{
				bank_select = bank_selects[1] & 0b11111110;
				break;
			}
			case 0b011:
			{
				bank_select = bank_selects[1] | 0b1;
				break;
			}
			case 0b100:
			case 0b101:
			case 0b110:
			case 0b111:
			{
				bank_select = bank_selects[ppu_bank - 2];
				break;
			}
		}
		bank_select = bank_select % (chr_rom_pages * 8);
		*data = chr_rom[bank_address | (bank_select << 10)];
	}
}

void mmc3_access_nametable_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	check_irq_clock(address);
	
	unsigned char mirroring = mirroring_register & 0b1;
	unsigned int nametable_address;
	if (mirroring == 0b1) //horizontal
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

void mmc3_save_state(FILE* save_file)
{
	fwrite(&bank_select_register, sizeof(char), 1, save_file);
	fwrite(bank_selects, sizeof(char), 8, save_file);
	fwrite(&mirroring_register, sizeof(char), 1, save_file);
	fwrite(&prg_ram_protect_register, sizeof(char), 1, save_file);
	fwrite(&irq_latch_register, sizeof(char), 1, save_file);
	fwrite(&irq_reload_register, sizeof(char), 1, save_file);
	fwrite(&irq_enable_register, sizeof(char), 1, save_file);
	fwrite(&last_address_bit_12, sizeof(char), 1, save_file);
	fwrite(&holding_irq, sizeof(char), 1, save_file);
	fwrite(&irq_counter, sizeof(char), 1, save_file);
}

void mmc3_load_state(FILE* save_file)
{
	fread(&bank_select_register, sizeof(char), 1, save_file);
	fread(bank_selects, sizeof(char), 8, save_file);
	fread(&mirroring_register, sizeof(char), 1, save_file);
	fread(&prg_ram_protect_register, sizeof(char), 1, save_file);
	fread(&irq_latch_register, sizeof(char), 1, save_file);
	fread(&irq_reload_register, sizeof(char), 1, save_file);
	fread(&irq_enable_register, sizeof(char), 1, save_file);
	fread(&last_address_bit_12, sizeof(char), 1, save_file);
	fread(&holding_irq, sizeof(char), 1, save_file);
	fread(&irq_counter, sizeof(char), 1, save_file);
}

void mmc3_init()
{
	bank_selects = malloc(sizeof(char) * 8);
	for (int i = 0; i < 8; i++)
	{
		bank_selects[i] = 0; 
	}
	
	prg_ram = malloc(sizeof(char) * 0x2000);
	for (unsigned int i = 0; i < 0x2000; i++)
	{
		prg_ram[i] = 0;
	}
	prg_ram_size = 0x2000;
}