#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "emu_nes.h"
#include "cartridge.h"
#include "nes_cpu.h"
#include "nes_ppu.h"
#include "mappers/nrom00.h"
#include "mappers/unrom02.h"

typedef void (*get_ptr_handler) (unsigned char*, unsigned int, unsigned char);
typedef void (*mapper_init) (void);
typedef void (*save_file_handler) (FILE*);

const int PRG_ROM_PAGE = 1024 * 16;
const int CHR_ROM_PAGE = 1024 * 8;
const unsigned char HORIZONTAL = 0;
const unsigned char VERTICAL = 0;
const unsigned int CART_RAM_SIZE = 0x2000;

unsigned char* prg_rom;
unsigned char* prg_ram;
unsigned char* chr_rom;
unsigned char* chr_ram;

unsigned int prg_rom_pages;
unsigned int chr_rom_pages;
unsigned int prg_rom_size;
unsigned int chr_rom_size;
unsigned char use_chr_ram;
unsigned char nametable_mirroring;
unsigned char mapper;

mapper_init* init_table;
get_ptr_handler* mapper_prg_table;
get_ptr_handler* mapper_chr_table;
get_ptr_handler* mapper_nametable_table;
save_file_handler* mapper_save_state_table;
save_file_handler* mapper_load_state_table;

void get_pointer_at_prg_address(unsigned char* data, unsigned int address, unsigned char access_type)
{
	return mapper_prg_table[mapper](data, address, access_type);
}

void get_pointer_at_chr_address(unsigned char* data, unsigned int address, unsigned char access_type)
{
	return mapper_chr_table[mapper](data, address, access_type);
}

// The cartridge has control over how the PPU accesses its RAM, normally controlling mirroring.
// It could even alter it to point to cartridge RAM instead.
void get_pointer_at_nametable_address(unsigned char* data, unsigned int address, unsigned char access_type)
{
	return mapper_nametable_table[mapper](data, address, access_type);
}

void cartridge_save_state(FILE* save_file)
{
	fwrite(&mapper, sizeof(char), 1, save_file);
	fwrite(prg_ram, sizeof(char), CART_RAM_SIZE, save_file);
	fwrite(chr_ram, sizeof(char), CART_RAM_SIZE, save_file);
	return mapper_save_state_table[mapper](save_file);
}

void cartridge_load_state(FILE* save_file)
{
	unsigned char save_state_mapper;
	fread(&save_state_mapper, sizeof(char), 1, save_file);
	if (mapper != save_state_mapper)
	{
		printf("Error: Incompatible save state.");
		exit_emulator();
	}
	fread(prg_ram, sizeof(char), CART_RAM_SIZE, save_file);
	fread(chr_ram, sizeof(char), CART_RAM_SIZE, save_file);
	return mapper_load_state_table[mapper](save_file);
}

// Default stub for unimplemented mappers. Best to close gracefully rather than...do whatever
// the emulator will do instead.
void unsupported_init()
{
	printf("Error: Unsupported mapper %02X.\n", mapper);
	exit_emulator();
}

// Initializes the cartridge. The mapper is the internal hardware that handles how cartridge addresses work.
// mirroring controls how PPU nametables are mirrored. 0 is horizontal and 1 is vertical.
void cartridge_init(unsigned char rom_mapper, unsigned char prg_pages, unsigned char chr_pages, unsigned char mirroring, FILE* rom)
{
	prg_rom_pages = prg_pages;
	prg_rom_size = prg_pages * PRG_ROM_PAGE;
	prg_rom = malloc(sizeof(char) * prg_rom_size);
	fread(prg_rom, 1, prg_rom_size, rom);
	
	prg_ram = malloc(sizeof(char) * CART_RAM_SIZE);
	for (unsigned int i = 0; i < CART_RAM_SIZE; i++)
	{
		prg_ram[i] = 0;
	}
	
	chr_rom_pages = chr_pages;
	chr_rom_size = chr_pages * CHR_ROM_PAGE;
	chr_ram = malloc(sizeof(char) * CART_RAM_SIZE);
	for (unsigned int i = 0; i < CART_RAM_SIZE; i++)
	{
		chr_ram[i] = 0;
	}
	if (chr_rom_size == 0)
	{
		use_chr_ram = 1;
	}
	else
	{
		chr_rom = malloc(sizeof(char) * chr_rom_size);
		fread(chr_rom, 1, chr_rom_size, rom);
		use_chr_ram = 0;
	}
	nametable_mirroring = mirroring;
	mapper = rom_mapper;
	
	init_table = calloc(256, sizeof(mapper_init*));
	for (unsigned int i = 0; i < 256; i++)
	{
		init_table[i] = &unsupported_init;
	}
	mapper_prg_table = calloc(256, sizeof(get_ptr_handler*));
	mapper_chr_table = calloc(256, sizeof(get_ptr_handler*));
	mapper_nametable_table = calloc(256, sizeof(get_ptr_handler*));
	mapper_save_state_table = calloc(256, sizeof(save_file_handler*));
	mapper_load_state_table = calloc(256, sizeof(save_file_handler*));
	
	init_table[0x00] = nrom00_init;
	mapper_prg_table[0x00] = nrom00_get_pointer_at_prg_address;
	mapper_chr_table[0x00] = fixed_get_pointer_at_chr_address;
	mapper_nametable_table[0x00] = fixed_get_pointer_at_nametable_address;
	mapper_save_state_table[0x00] = save_nothing;
	mapper_load_state_table[0x00] = load_nothing;
	
	init_table[0x02] = unrom02_init;
	mapper_prg_table[0x02] = unrom02_get_pointer_at_prg_address;
	mapper_chr_table[0x02] = fixed_get_pointer_at_chr_address;
	mapper_nametable_table[0x02] = fixed_get_pointer_at_nametable_address;
	mapper_save_state_table[0x02] = unrom02_save_state;
	mapper_load_state_table[0x02] = unrom02_load_state;
	
	init_table[mapper]();
}