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

unsigned char* apu_read(unsigned int address);
unsigned char* apu_write(unsigned int address);
void apu_tick();
void apu_init();

#endif