#ifndef CONTROLLER_HEADER
#define CONTROLLER_HEADER

extern unsigned char controller_bus;

extern unsigned char controller_1_data;
extern unsigned char controller_2_data;

void controller_init();
void controller_tick();
void write_controller_state(unsigned char* data, unsigned int address);
void read_controller_state(unsigned char* data, unsigned int address);

void controller_save_state(FILE* save_file);
void controller_load_state(FILE* save_file);

#endif