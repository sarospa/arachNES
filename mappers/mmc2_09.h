#ifndef MMC2_09_HEADER
#define MMC2_09_HEADER

void mmc2_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void mmc2_access_chr_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void mmc2_access_nametable_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void mmc2_save_state(FILE* save_file);
void mmc2_load_state(FILE* save_file);

#endif