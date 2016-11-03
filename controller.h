#ifndef CONTROLLER_HEADER
#define CONTROLLER_HEADER

void controller_init();
void controller_tick();
unsigned char* write_controller_state(unsigned int address);
unsigned char* read_controller_state(unsigned int address);

extern unsigned char controller_bus;

extern unsigned char controller_1_data;
extern unsigned char controller_2_data;

#endif