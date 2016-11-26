#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<stdint.h>
#include "nes_apu.h"
#include "nes_cpu.h"

unsigned int apu_half_clock_count;
const unsigned int four_step_frame_length = 29830;
const unsigned int five_step_frame_length = 37282;

unsigned char apu_status;
unsigned char apu_frame_settings;
// Contains the linear control flag in bit 7 and the linear load value in bits 6-0.
// Bit 7 is also the length counter halt flag?
unsigned char triangle_linear_control;
// Controls the period of the triangle wave. Counts down every half clock.
unsigned char triangle_timer_low;
// The low 3 bits are the high bits of the period timer. The high 5 bits are for the length load value.
unsigned char triangle_timer_high;
unsigned int triangle_timer_count;
unsigned char triangle_linear_reload;

unsigned char* length_table;
unsigned char triangle_length_counter;

unsigned char triangle_linear_counter;

unsigned char* sequencer;
unsigned char sequencer_index;
const unsigned char sequencer_size = 32;

float* mixer_buffer;
unsigned int apu_buffer_length;
unsigned const int apu_buffer_max = 0x10000;

unsigned int apu_register_accessed;
unsigned char access_type;

// Return this for registers we haven't implemented yet.
unsigned char dummy;

unsigned char* apu_write(unsigned int address)
{
	apu_register_accessed = address;
	access_type = WRITE;
	switch(address)
	{
		case 0x4000:
		{
			return &dummy;
			break;
		}
		case 0x4001:
		{
			return &dummy;
			break;
		}
		case 0x4002:
		{
			return &dummy;
			break;
		}
		case 0x4003:
		{
			return &dummy;
			break;
		}
		case 0x4004:
		{
			return &dummy;
			break;
		}
		case 0x4005:
		{
			return &dummy;
			break;
		}
		case 0x4006:
		{
			return &dummy;
			break;
		}
		case 0x4007:
		{
			return &dummy;
			break;
		}
		case 0x4008:
		{
			return &triangle_linear_control;
			break;
		}
		case 0x400A:
		{
			return &triangle_timer_low;
			break;
		}
		case 0x400B:
		{
			triangle_linear_reload = 1;
			return &triangle_timer_high;
			break;
		}
		case 0x400C:
		{
			return &dummy;
			break;
		}
		case 0x400E:
		{
			return &dummy;
			break;
		}
		case 0x400F:
		{
			return &dummy;
			break;
		}
		case 0x4010:
		{
			return &dummy;
			break;
		}
		case 0x4011:
		{
			return &dummy;
			break;
		}
		case 0x4012:
		{
			return &dummy;
			break;
		}
		case 0x4013:
		{
			return &dummy;
			break;
		}
		case 0x4015:
		{
			return &apu_status;
			break;
		}
		case 0x4017:
		{
			return &apu_frame_settings;
			break;
		}
		default:
		{
			printf("Unhandled APU register %04X\n", address);
			exit_emulator();
			return NULL;
		}
	}
}

unsigned char* apu_read(unsigned int address)
{
	apu_register_accessed = address;
	access_type = READ;
	switch(address)
	{
		case 0x4000:
		{
			return &dummy;
			break;
		}
		case 0x4001:
		{
			return &dummy;
			break;
		}
		case 0x4002:
		{
			return &dummy;
			break;
		}
		case 0x4003:
		{
			return &dummy;
			break;
		}
		case 0x4004:
		{
			return &dummy;
			break;
		}
		case 0x4005:
		{
			return &dummy;
			break;
		}
		case 0x4006:
		{
			return &dummy;
			break;
		}
		case 0x4007:
		{
			return &dummy;
			break;
		}
		case 0x4008:
		{
			return &triangle_linear_control;
			break;
		}
		case 0x400A:
		{
			return &triangle_timer_low;
			break;
		}
		case 0x400B:
		{
			return &triangle_timer_high;
			break;
		}
		case 0x400C:
		{
			return &dummy;
			break;
		}
		case 0x400E:
		{
			return &dummy;
			break;
		}
		case 0x400F:
		{
			return &dummy;
			break;
		}
		case 0x4010:
		{
			return &dummy;
			break;
		}
		case 0x4011:
		{
			return &dummy;
			break;
		}
		case 0x4012:
		{
			return &dummy;
			break;
		}
		case 0x4013:
		{
			return &dummy;
			break;
		}
		case 0x4015:
		{
			return &apu_status;
			break;
		}
		case 0x4017:
		{
			return &apu_frame_settings;
			break;
		}
		default:
		{
			printf("Unhandled APU register %04X\n", address);
			exit_emulator();
			return NULL;
		}
	}
}

void quarter_frame_clock()
{
	if (triangle_linear_reload)
	{
		triangle_linear_counter = triangle_linear_control & 0b1111111;
	}
	else if (triangle_linear_counter > 0)
	{
		triangle_linear_counter--;
	}
	
	if ((triangle_linear_control & 0b10000000) == 0)
	{
		triangle_linear_reload = 0;
	}
}

void half_frame_clock()
{
	if ((triangle_length_counter > 0) && ((triangle_linear_control & 0b10000000) == 0))
	{
		triangle_length_counter--;
	}
}

void mix_audio()
{
	if (apu_buffer_length >= apu_buffer_max)
	{
		return;
	}
	
	float tnd_out;
	//tnd_out = 159.79 / ((1.0 / (sequencer[sequencer_index] / 8227.0)) + 100.0); Check for division by zero if we use this
	tnd_out = 0.03125 * sequencer[sequencer_index];
	mixer_buffer[apu_buffer_length] = tnd_out + 0.234375;
	apu_buffer_length++;
}

// For convenience, each tick will represent a half clock for the APU.
// Period timers count down every clock, except triangle timer, which counts down every half clock.
// Linear counters count down every quarter frame. Length counters count down every half frame.
void apu_tick()
{
	switch(apu_register_accessed)
	{
		case 0x400B:
		{
			if (access_type == WRITE)
			{
				// Reload the length counter with the value from the APU's length lookup table.
				triangle_length_counter = length_table[(triangle_timer_high & 0b11111000) >> 3];
			}
			break;
		}
	}
	apu_register_accessed = 0;
	
	if ((triangle_linear_counter > 0) && (triangle_length_counter > 0))
	{
		if (triangle_timer_count == 0)
		{
			sequencer_index = (sequencer_index + 1) % sequencer_size;
			triangle_timer_count = triangle_timer_low + ((triangle_timer_high & 0b111) << 8);
		}
		else
		{
			triangle_timer_count--;
		}
	}
	
	if ((apu_status & 0b100) == 0)
	{
		triangle_length_counter = 0;
	}
	
	if ((apu_frame_settings & 0b10000000) == 0b10000000)
	{
		switch(apu_half_clock_count)
		{
			case 14913:
			case 37281:
			{
				half_frame_clock();
			}
			case 7457:
			case 22371:
			{
				quarter_frame_clock();
			}
		}
		apu_half_clock_count = (apu_half_clock_count + 1) % five_step_frame_length;
	}
	else
	{
		switch(apu_half_clock_count)
		{
			case 14913:
			case 29829:
			{
				half_frame_clock();
			}
			case 7457:
			case 22371:
			{
				quarter_frame_clock();
			}
		}
		apu_half_clock_count = (apu_half_clock_count + 1) % four_step_frame_length;
	}
	
	if ((apu_half_clock_count % 2) == 0)
	{
		mix_audio();
	}
}

void apu_init()
{
	apu_status = 0;
	apu_frame_settings = 0;
	
	apu_half_clock_count = 0;
	
	sequencer = malloc(sizeof(char) * sequencer_size);
	// Sequencer values range from 15 to 0, then 0 back up to 15.
	for (int i = 0; i < 16; i++)
	{
		sequencer[i + 16] = i;
		sequencer[15 - i] = i;
	}
	apu_buffer_length = 0;
	mixer_buffer = malloc(sizeof(int) * apu_buffer_max);
	
	triangle_linear_reload = 0;
	triangle_length_counter = 0;
	length_table = malloc(sizeof(char) * 32);
	length_table[0] = 10;
	length_table[1] = 254;
	length_table[2] = 20;
	length_table[3] = 2;
	length_table[4] = 40;
	length_table[5] = 4;
	length_table[6] = 80;
	length_table[7] = 6;
	length_table[8] = 160;
	length_table[9] = 8;
	length_table[10] = 60;
	length_table[11] = 10;
	length_table[12] = 14;
	length_table[13] = 12;
	length_table[14] = 26;
	length_table[15] = 14;
	length_table[16] = 12;
	length_table[17] = 16;
	length_table[18] = 24;
	length_table[19] = 18;
	length_table[20] = 48;
	length_table[21] = 20;
	length_table[22] = 96;
	length_table[23] = 22;
	length_table[24] = 192;
	length_table[25] = 24;
	length_table[26] = 72;
	length_table[27] = 26;
	length_table[28] = 16;
	length_table[29] = 28;
	length_table[30] = 32;
	length_table[31] = 30;
	
	triangle_timer_count = 0;
	
	apu_register_accessed = 0;
}