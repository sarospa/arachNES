#ifndef MMC3_04_HEADER
#define MMC3_04_HEADER

void mmc3_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void mmc3_access_chr_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void mmc3_access_nametable_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void mmc3_save_state(FILE* save_file);
void mmc3_load_state(FILE* save_file);
void mmc3_init();

#endif