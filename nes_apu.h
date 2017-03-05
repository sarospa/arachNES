#ifndef APU_HEADER
#define APU_HEADER

extern unsigned char apu_status;
extern unsigned char triangle_counter_control;
extern unsigned char triangle_timer_low;
extern unsigned char triangle_timer_high;

extern float* mixer_buffer;
extern unsigned int apu_buffer_length;
extern unsigned const int apu_buffer_max;

extern unsigned char pulse_1_silence;
extern unsigned char pulse_2_silence;
extern unsigned char triangle_silence;
extern unsigned char noise_silence;
extern unsigned char sample_silence;

void apu_read(unsigned char* data, unsigned int address);
void apu_write(unsigned char* data, unsigned int address);
void apu_tick();
void apu_init();

void apu_save_state(FILE* save_file);
void apu_load_state(FILE* save_file);

#endif