#ifndef UNROM02_HEADER
#define UNROM02_HEADER

void unrom02_get_pointer_at_prg_address(unsigned char* data, unsigned int address, unsigned char access_type);

void unrom02_save_state(FILE* save_file);
void unrom02_load_state(FILE* save_file);

#endif