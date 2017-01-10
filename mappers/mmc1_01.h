#ifndef MMC1_01_HEADER
#define MMC1_01_HEADER

void mmc1_init();
void mmc1_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void mmc1_access_chr_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void mmc1_access_nametable_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void mmc1_save_state(FILE* save_file);
void mmc1_load_state(FILE* save_file);

#endif