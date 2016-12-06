#ifndef NROM00_HEADER
#define NROM00_HEADER

void nrom00_get_pointer_at_prg_address(unsigned char* data, unsigned int address, unsigned char access_type);
void fixed_get_pointer_at_chr_address(unsigned char* data, unsigned int address, unsigned char access_type);
void fixed_get_pointer_at_nametable_address(unsigned char* data, unsigned int address, unsigned char access_type);
void nrom00_init();

#endif