#ifndef NROM00_HEADER
#define NROM00_HEADER

unsigned char* nrom00_get_pointer_at_prg_address(unsigned int address, unsigned char access_type);
unsigned char* fixed_get_pointer_at_chr_address(unsigned int address, unsigned char access_type);
unsigned char* fixed_get_pointer_at_nametable_address(unsigned int address, unsigned char access_type);
void nrom00_init();

#endif