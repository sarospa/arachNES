#ifndef CARTRIDGE_HEADER
#define CARTRIDGE_HEADER

extern const int PRG_ROM_PAGE;
extern const int CHR_ROM_PAGE;

extern unsigned char* prg_rom;
extern unsigned char* prg_ram;
extern unsigned char* chr_rom;
extern unsigned char* chr_ram;

void cartridge_init(unsigned char mapper, unsigned char prg_rom_pages, unsigned char chr_rom_pages, unsigned char mirroring, FILE* rom);
unsigned char* get_pointer_at_prg_address(unsigned int address);
unsigned char* get_pointer_at_chr_address(unsigned int address, unsigned char access_type);
unsigned char* get_pointer_at_nametable_address(unsigned int address);

#endif