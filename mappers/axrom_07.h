#ifndef AXROM_07_HEADER
#define AXROM_07_HEADER

void axrom_07_access_prg_memory(unsigned char* data, unsigned int address, unsigned char access_type);
void axrom_07_access_nametable(unsigned char* data, unsigned int address, unsigned char access_type);

void axrom_07_save_state(FILE* save_file);
void axrom_07_load_state(FILE* save_file);

#endif