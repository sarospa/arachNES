#ifndef CARTRIDGE_HEADER
#define CARTRIDGE_HEADER

extern const int PRG_ROM_PAGE;
extern const int CHR_ROM_PAGE;
extern const unsigned char HORIZONTAL;
extern const unsigned char VERTICAL;

extern unsigned char* prg_rom;
extern unsigned char* prg_ram;
extern unsigned char* chr_rom;
extern unsigned char* chr_ram;

extern unsigned int prg_rom_pages;
extern unsigned int chr_rom_pages;
extern unsigned int prg_rom_size;
extern unsigned int prg_ram_size;
extern unsigned int chr_rom_size;
extern unsigned char use_chr_ram;
extern unsigned char nametable_mirroring;
extern unsigned char mapper;

void cartridge_init(unsigned char mapper, unsigned char prg_rom_pages, unsigned char chr_rom_pages, unsigned char mirroring, FILE* rom);
void get_pointer_at_prg_address(unsigned char* data, unsigned int address, unsigned char access_type);
void get_pointer_at_chr_address(unsigned char* data, unsigned int address, unsigned char access_type);
void get_pointer_at_nametable_address(unsigned char* data, unsigned int address, unsigned char access_type);

void cartridge_save_state(FILE* save_file);
void cartridge_load_state(FILE* save_file);

#endif