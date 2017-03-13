#ifndef CNROM_03_HEADER
#define CNROM_03_HEADER

void cnrom_03_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void cnrom_03_access_chr_memory(unsigned char* data, unsigned int address, unsigned char access_type);

void cnrom_03_save_state(FILE* save_file);
void cnrom_03_load_state(FILE* save_file);

#endif