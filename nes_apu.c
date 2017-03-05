#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<stdint.h>
#include "emu_nes.h"
#include "nes_apu.h"
#include "nes_cpu.h"

unsigned int apu_half_clock_count;
const unsigned int FOUR_STEP_FRAME_LENGTH = 29830;
const unsigned int FIVE_STEP_FRAME_LENGTH = 37282;
const unsigned char COARSE_MAX_VOLUME = 15;
const unsigned char FINE_MAX_VOLUME = 127;

unsigned char apu_status;
unsigned char apu_frame_settings;

// Bits 6-7 are the duty, bit 5 is the length counter halt, bit 4 is the envelope toggle, bits 0-3
// are the volume/envelope divider period.
unsigned char pulse_1_control;
unsigned char pulse_1_sweep;
// Controls the period of the pulse wave. Counts down every half clock.
unsigned char pulse_1_timer_low;
// The low 3 bits are the high bits of the period timer. The high 5 bits are for the length load value.
unsigned char pulse_1_timer_high;
unsigned int pulse_1_timer_count;
unsigned char pulse_1_duty_index;
unsigned char pulse_1_length_counter;
unsigned char pulse_1_sweep_divider;
unsigned char pulse_1_sweep_reload;
unsigned int pulse_1_target_period;
unsigned char pulse_1_envelope_start;
unsigned char pulse_1_envelope_divider;
unsigned char pulse_1_envelope_decay;

// Bits 6-7 are the duty, bit 5 is the length counter halt, bit 4 is the envelope toggle, bits 0-3
// are the volume/envelope divider period.
unsigned char pulse_2_control;
unsigned char pulse_2_sweep;
// Controls the period of the pulse wave. Counts down every half clock.
unsigned char pulse_2_timer_low;
// The low 3 bits are the high bits of the period timer. The high 5 bits are for the length load value.
unsigned char pulse_2_timer_high;
unsigned int pulse_2_timer_count;
unsigned char pulse_2_duty_index;
unsigned char pulse_2_length_counter;
unsigned char pulse_2_sweep_divider;
unsigned char pulse_2_sweep_reload;
unsigned int pulse_2_target_period;
unsigned char pulse_2_envelope_start;
unsigned char pulse_2_envelope_divider;
unsigned char pulse_2_envelope_decay;

// Contains the linear control flag in bit 7 and the linear load value in bits 6-0.
// Bit 7 is also the length counter halt flag?
unsigned char triangle_linear_control;
// Controls the period of the triangle wave. Counts down every half clock.
unsigned char triangle_timer_low;
// The low 3 bits are the high bits of the period timer. The high 5 bits are for the length load value.
unsigned char triangle_timer_high;
unsigned int triangle_timer_count;
unsigned char triangle_linear_reload;
unsigned char triangle_linear_counter;
unsigned char triangle_length_counter;

// Bit 5 is the envelope loop/disable length counter flag, bit 4 is the constant volume flag, bits 0-3
// are the volume/envelope divider period.
unsigned char noise_control;
// Bit 7 is the mode flag, bits 0-3 control the period.
unsigned char noise_period_control;
unsigned char noise_length_counter_load;
unsigned int noise_timer_count;
// The pseudo-random register used to produce the 'noise'.
unsigned int noise_bit_stream;
unsigned char noise_length_counter;
unsigned char noise_envelope_start;
unsigned char noise_envelope_divider;
unsigned char noise_envelope_decay;

// Bit 7 is the IRQ enable flag. Bit 6 is the loop flag. Bits 0-3 is the rate index.
unsigned char dmc_control = 0;
unsigned char dmc_sample_address = 0;
unsigned char dmc_sample_length = 0;
unsigned char dmc_sample_buffer = 0;
unsigned char dmc_bits_remaining = 0;
unsigned int dmc_current_address = 0;
unsigned int dmc_bytes_remaining = 0;
unsigned int dmc_rate_count = 0;
unsigned char dmc_output_level = 0;
unsigned char dmc_silence_flag = 0;

unsigned int* noise_period_table;

unsigned char* length_table;
unsigned int* dmc_rate_table;

unsigned char* sequencer;
unsigned char sequencer_index;
const unsigned char sequencer_size = 32;

// There are four duty waveforms, each with one bit per sample. So why not stuff all four bits in one char for efficiency?
// This is bad. I'm starting to think like the NES.
unsigned char* duty_sequence;
const unsigned char duty_size = 8;

float* mixer_buffer;
unsigned int apu_buffer_length;
unsigned const int APU_BUFFER_MAX = 0x10000;

// Flags to let the user control which channels are played.
unsigned char pulse_1_silence;
unsigned char pulse_2_silence;
unsigned char triangle_silence;
unsigned char noise_silence;
unsigned char sample_silence;

void apu_write(unsigned char* data, unsigned int address)
{
	switch(address)
	{
		case 0x4000:
		{
			pulse_1_control = *data;
			break;
		}
		case 0x4001:
		{
			pulse_1_sweep_reload = 1;
			pulse_1_sweep = *data;
			break;
		}
		case 0x4002:
		{
			pulse_1_timer_low = *data;
			break;
		}
		case 0x4003:
		{
			pulse_1_envelope_start = 1;
			pulse_1_timer_high = *data;
			pulse_1_length_counter = length_table[(pulse_1_timer_high & 0b11111000) >> 3];
			break;
		}
		case 0x4004:
		{
			pulse_2_control = *data;
			break;
		}
		case 0x4005:
		{
			pulse_2_sweep_reload = 1;
			pulse_2_sweep = *data;
			break;
		}
		case 0x4006:
		{
			pulse_2_timer_low = *data;
			break;
		}
		case 0x4007:
		{
			pulse_2_envelope_start = 1;
			pulse_2_timer_high = *data;
			pulse_2_length_counter = length_table[(pulse_2_timer_high & 0b11111000) >> 3];
			break;
		}
		case 0x4008:
		{
			triangle_linear_control = *data;
			break;
		}
		case 0x400A:
		{
			triangle_timer_low = *data;
			break;
		}
		case 0x400B:
		{
			triangle_linear_reload = 1;
			triangle_timer_high = *data;
			triangle_length_counter = length_table[(triangle_timer_high & 0b11111000) >> 3];
			break;
		}
		case 0x400C:
		{
			noise_control = *data;
			break;
		}
		case 0x400E:
		{
			noise_period_control = *data;
			break;
		}
		case 0x400F:
		{
			noise_envelope_start = 1;
			noise_length_counter_load = *data;
			noise_length_counter = length_table[(noise_length_counter_load & 0b11111000) >> 3];
			break;
		}
		case 0x4010:
		{
			dmc_control = *data;
			dmc_rate_count = dmc_rate_table[dmc_control & 0b1111];
			break;
		}
		case 0x4011:
		{
			dmc_output_level = *data;
			break;
		}
		case 0x4012:
		{
			dmc_sample_address = *data;
			break;
		}
		case 0x4013:
		{
			dmc_sample_length = *data;
			break;
		}
		case 0x4015:
		{
			apu_status = *data;
			// Set the DMC bytes remaining to 0 on disabling DMC,
			// thus halting it.
			if (((apu_status >> 4) & 0b1) == 0)
			{
				dmc_bytes_remaining = 0;
			}
			// If DMC bytes remaining is 0, then 
			else if (dmc_bytes_remaining == 0)
			{
				// Sample address = %11AAAAAA.AA000000
				dmc_current_address = 0xC000 | (dmc_sample_address << 6);
				// Sample length = %LLLL.LLLL0001
				dmc_bytes_remaining = (dmc_sample_length << 4) | 1;
				dmc_bits_remaining = 0;
			}
			break;
		}
		case 0x4017:
		{
			apu_frame_settings = *data;
			break;
		}
		// Unused registers. Is any value stored here?
		// Not sure. Seems simpler to do nothing.
		case 0x4009:
		case 0x400D:
		{
			break;
		}
		default:
		{
			printf("Unhandled APU register %04X\n", address);
			exit_emulator();
		}
	}
}

void apu_read(unsigned char* data, unsigned int address)
{
	switch(address)
	{
		case 0x4000:
		{
			*data = pulse_1_control;
			break;
		}
		case 0x4001:
		{
			*data = pulse_1_sweep;
			break;
		}
		case 0x4002:
		{
			*data = pulse_1_timer_low;
			break;
		}
		case 0x4003:
		{
			*data = pulse_1_timer_high;
			break;
		}
		case 0x4004:
		{
			*data = pulse_2_control;
			break;
		}
		case 0x4005:
		{
			*data = pulse_2_sweep;
			break;
		}
		case 0x4006:
		{
			*data = pulse_2_timer_low;
			break;
		}
		case 0x4007:
		{
			*data = pulse_2_timer_high;
			break;
		}
		case 0x4008:
		{
			*data = triangle_linear_control;
			break;
		}
		case 0x400A:
		{
			*data = triangle_timer_low;
			break;
		}
		case 0x400B:
		{
			*data = triangle_timer_high;
			break;
		}
		case 0x400C:
		{
			*data = noise_control;
			break;
		}
		case 0x400E:
		{
			*data = noise_period_control;
			break;
		}
		case 0x400F:
		{
			*data = noise_length_counter_load;
			break;
		}
		case 0x4010:
		{
			break;
		}
		case 0x4011:
		{
			break;
		}
		case 0x4012:
		{
			break;
		}
		case 0x4013:
		{
			break;
		}
		case 0x4015:
		{
			*data = apu_status;
			break;
		}
		case 0x4017:
		{
			*data = apu_frame_settings;
			break;
		}
		// Unused registers. Is any value stored here?
		// Not sure. Seems simpler to do nothing.
		case 0x4009:
		case 0x400D:
		{
			break;
		}
		default:
		{
			printf("Unhandled APU register %04X\n", address);
			exit_emulator();
		}
	}
}

void apu_save_state(FILE* save_file)
{
	fwrite(&apu_half_clock_count, sizeof(int), 1, save_file);
	fwrite(&apu_status, sizeof(char), 1, save_file);
	fwrite(&apu_frame_settings, sizeof(char), 1, save_file);
	
	fwrite(&pulse_1_control, sizeof(char), 1, save_file);
	fwrite(&pulse_1_sweep, sizeof(char), 1, save_file);
	fwrite(&pulse_1_timer_low, sizeof(char), 1, save_file);
	fwrite(&pulse_1_timer_high, sizeof(char), 1, save_file);
	fwrite(&pulse_1_timer_count, sizeof(int), 1, save_file);
	fwrite(&pulse_1_duty_index, sizeof(char), 1, save_file);
	fwrite(&pulse_1_length_counter, sizeof(char), 1, save_file);
	fwrite(&pulse_1_sweep_divider, sizeof(char), 1, save_file);
	fwrite(&pulse_1_sweep_reload, sizeof(char), 1, save_file);
	fwrite(&pulse_1_target_period, sizeof(int), 1, save_file);
	fwrite(&pulse_1_envelope_start, sizeof(char), 1, save_file);
	fwrite(&pulse_1_envelope_divider, sizeof(char), 1, save_file);
	fwrite(&pulse_1_envelope_decay, sizeof(char), 1, save_file);
	
	fwrite(&pulse_2_control, sizeof(char), 1, save_file);
	fwrite(&pulse_2_sweep, sizeof(char), 1, save_file);
	fwrite(&pulse_2_timer_low, sizeof(char), 1, save_file);
	fwrite(&pulse_2_timer_high, sizeof(char), 1, save_file);
	fwrite(&pulse_2_timer_count, sizeof(int), 1, save_file);
	fwrite(&pulse_2_duty_index, sizeof(char), 1, save_file);
	fwrite(&pulse_2_length_counter, sizeof(char), 1, save_file);
	fwrite(&pulse_2_sweep_divider, sizeof(char), 1, save_file);
	fwrite(&pulse_2_sweep_reload, sizeof(char), 1, save_file);
	fwrite(&pulse_2_target_period, sizeof(int), 1, save_file);
	fwrite(&pulse_2_envelope_start, sizeof(char), 1, save_file);
	fwrite(&pulse_2_envelope_divider, sizeof(char), 1, save_file);
	fwrite(&pulse_2_envelope_decay, sizeof(char), 1, save_file);
	
	fwrite(&triangle_linear_control, sizeof(char), 1, save_file);
	fwrite(&triangle_timer_low, sizeof(char), 1, save_file);
	fwrite(&triangle_timer_high, sizeof(char), 1, save_file);
	fwrite(&triangle_timer_count, sizeof(int), 1, save_file);
	fwrite(&triangle_linear_reload, sizeof(char), 1, save_file);
	fwrite(&triangle_linear_counter, sizeof(char), 1, save_file);
	fwrite(&triangle_length_counter, sizeof(char), 1, save_file);
	
	fwrite(&noise_control, sizeof(char), 1, save_file);
	fwrite(&noise_period_control, sizeof(char), 1, save_file);
	fwrite(&noise_length_counter_load, sizeof(char), 1, save_file);
	fwrite(&noise_timer_count, sizeof(int), 1, save_file);
	fwrite(&noise_bit_stream, sizeof(int), 1, save_file);
	fwrite(&noise_length_counter, sizeof(char), 1, save_file);
	fwrite(&noise_envelope_start, sizeof(char), 1, save_file);
	fwrite(&noise_envelope_divider, sizeof(char), 1, save_file);
	fwrite(&noise_envelope_decay, sizeof(char), 1, save_file);
	
	fwrite(&dmc_control, sizeof(char), 1, save_file);
	fwrite(&dmc_sample_address, sizeof(char), 1, save_file);
	fwrite(&dmc_sample_length, sizeof(char), 1, save_file);
	fwrite(&dmc_sample_buffer, sizeof(char), 1, save_file);
	fwrite(&dmc_bits_remaining, sizeof(char), 1, save_file);
	fwrite(&dmc_current_address, sizeof(int), 1, save_file);
	fwrite(&dmc_bytes_remaining, sizeof(int), 1, save_file);
	fwrite(&dmc_rate_count, sizeof(int), 1, save_file);
	fwrite(&dmc_output_level, sizeof(char), 1, save_file);
	fwrite(&dmc_silence_flag, sizeof(char), 1, save_file);
	
	fwrite(&sequencer_index, sizeof(char), 1, save_file);
}

void apu_load_state(FILE* save_file)
{
	fread(&apu_half_clock_count, sizeof(int), 1, save_file);
	fread(&apu_status, sizeof(char), 1, save_file);
	fread(&apu_frame_settings, sizeof(char), 1, save_file);
	
	fread(&pulse_1_control, sizeof(char), 1, save_file);
	fread(&pulse_1_sweep, sizeof(char), 1, save_file);
	fread(&pulse_1_timer_low, sizeof(char), 1, save_file);
	fread(&pulse_1_timer_high, sizeof(char), 1, save_file);
	fread(&pulse_1_timer_count, sizeof(int), 1, save_file);
	fread(&pulse_1_duty_index, sizeof(char), 1, save_file);
	fread(&pulse_1_length_counter, sizeof(char), 1, save_file);
	fread(&pulse_1_sweep_divider, sizeof(char), 1, save_file);
	fread(&pulse_1_sweep_reload, sizeof(char), 1, save_file);
	fread(&pulse_1_target_period, sizeof(int), 1, save_file);
	fread(&pulse_1_envelope_start, sizeof(char), 1, save_file);
	fread(&pulse_1_envelope_divider, sizeof(char), 1, save_file);
	fread(&pulse_1_envelope_decay, sizeof(char), 1, save_file);
	
	fread(&pulse_2_control, sizeof(char), 1, save_file);
	fread(&pulse_2_sweep, sizeof(char), 1, save_file);
	fread(&pulse_2_timer_low, sizeof(char), 1, save_file);
	fread(&pulse_2_timer_high, sizeof(char), 1, save_file);
	fread(&pulse_2_timer_count, sizeof(int), 1, save_file);
	fread(&pulse_2_duty_index, sizeof(char), 1, save_file);
	fread(&pulse_2_length_counter, sizeof(char), 1, save_file);
	fread(&pulse_2_sweep_divider, sizeof(char), 1, save_file);
	fread(&pulse_2_sweep_reload, sizeof(char), 1, save_file);
	fread(&pulse_2_target_period, sizeof(int), 1, save_file);
	fread(&pulse_2_envelope_start, sizeof(char), 1, save_file);
	fread(&pulse_2_envelope_divider, sizeof(char), 1, save_file);
	fread(&pulse_2_envelope_decay, sizeof(char), 1, save_file);
	
	fread(&triangle_linear_control, sizeof(char), 1, save_file);
	fread(&triangle_timer_low, sizeof(char), 1, save_file);
	fread(&triangle_timer_high, sizeof(char), 1, save_file);
	fread(&triangle_timer_count, sizeof(int), 1, save_file);
	fread(&triangle_linear_reload, sizeof(char), 1, save_file);
	fread(&triangle_linear_counter, sizeof(char), 1, save_file);
	fread(&triangle_length_counter, sizeof(char), 1, save_file);
	
	fread(&noise_control, sizeof(char), 1, save_file);
	fread(&noise_period_control, sizeof(char), 1, save_file);
	fread(&noise_length_counter_load, sizeof(char), 1, save_file);
	fread(&noise_timer_count, sizeof(int), 1, save_file);
	fread(&noise_bit_stream, sizeof(int), 1, save_file);
	fread(&noise_length_counter, sizeof(char), 1, save_file);
	fread(&noise_envelope_start, sizeof(char), 1, save_file);
	fread(&noise_envelope_divider, sizeof(char), 1, save_file);
	fread(&noise_envelope_decay, sizeof(char), 1, save_file);
	
	fread(&dmc_control, sizeof(char), 1, save_file);
	fread(&dmc_sample_address, sizeof(char), 1, save_file);
	fread(&dmc_sample_length, sizeof(char), 1, save_file);
	fread(&dmc_sample_buffer, sizeof(char), 1, save_file);
	fread(&dmc_bits_remaining, sizeof(char), 1, save_file);
	fread(&dmc_current_address, sizeof(int), 1, save_file);
	fread(&dmc_bytes_remaining, sizeof(int), 1, save_file);
	fread(&dmc_rate_count, sizeof(int), 1, save_file);
	fread(&dmc_output_level, sizeof(char), 1, save_file);
	fread(&dmc_silence_flag, sizeof(char), 1, save_file);
	
	fread(&sequencer_index, sizeof(char), 1, save_file);
	
	apu_buffer_length = 0;
}

// Handles the sweep algorithm that manipulates the frequency of the pulse channels.
void sweep_pulse()
{
	// Pulse 1
	unsigned char sweep_enabled = (pulse_1_sweep & 0b10000000) == 0b10000000;
	unsigned char sweep_negate = (pulse_1_sweep & 0b00001000) == 0b00001000;
	unsigned int current_period = pulse_1_timer_low + ((pulse_1_timer_high & 0b111) << 8);
	unsigned char sweep_shift = pulse_1_sweep & 0b111;
	unsigned char sweep_period = (pulse_1_sweep >> 4) & 0b111;
	
	if ((pulse_1_sweep_divider == 0) && sweep_enabled)
	{
		signed char sweep_sign;
		// Due to a bit of oddness in pulse channel 1, in negate mode it subtracts the shifted
		// value minus one. So this should be accounted for.
		unsigned char sweep_modifier;
		if (sweep_negate)
		{
			sweep_sign = -1;
			sweep_modifier = 1;
		}
		else
		{
			sweep_sign = 1;
			sweep_modifier = 0;
		}
		pulse_1_target_period = current_period + (((current_period >> sweep_shift) * sweep_sign) + sweep_modifier);
		if ((pulse_1_target_period <= 0x7FF) && (sweep_shift > 0))
		{
			pulse_1_timer_low = pulse_1_target_period & 0xFF;
			pulse_1_timer_high = (pulse_1_timer_high & 0b11111000) | ((pulse_1_target_period >> 8) & 0b111);
		}
	}
	
	if (((pulse_1_sweep_divider == 0) && sweep_enabled) || pulse_1_sweep_reload)
	{
		pulse_1_sweep_divider = sweep_period;
	}
	else if (pulse_1_sweep_divider > 0)
	{
		pulse_1_sweep_divider--;
	}
	
	pulse_1_sweep_reload = 0;
	
	// Pulse 2
	sweep_enabled = (pulse_2_sweep & 0b10000000) == 0b10000000;
	sweep_negate = (pulse_2_sweep & 0b00001000) == 0b00001000;
	current_period = pulse_2_timer_low + ((pulse_2_timer_high & 0b111) << 8);
	sweep_shift = pulse_2_sweep & 0b111;
	sweep_period = (pulse_2_sweep >> 4) & 0b111;
	
	if ((pulse_2_sweep_divider == 0) && sweep_enabled)
	{
		signed char sweep_sign;
		if (sweep_negate)
		{
			sweep_sign = -1;
		}
		else
		{
			sweep_sign = 1;
		}
		pulse_2_target_period = current_period + ((current_period >> sweep_shift) * sweep_sign);
		if ((pulse_2_target_period <= 0x7FF) && (sweep_shift > 0))
		{
			pulse_2_timer_low = pulse_2_target_period & 0xFF;
			pulse_2_timer_high = (pulse_2_timer_high & 0b11111000) | ((pulse_2_target_period >> 8) & 0b111);
		}
	}
	
	if (((pulse_2_sweep_divider == 0) && sweep_enabled) || pulse_2_sweep_reload)
	{
		pulse_2_sweep_divider = sweep_period;
	}
	else if (pulse_2_sweep_divider > 0)
	{
		pulse_2_sweep_divider--;
	}
	
	pulse_2_sweep_reload = 0;
}

void clock_envelope()
{
	// Pulse 1
	unsigned char pulse_1_envelope_loop = (pulse_1_control & 0b00100000) == 0b00100000;
	
	if (pulse_1_envelope_start)
	{
		pulse_1_envelope_decay = COARSE_MAX_VOLUME;
		pulse_1_envelope_divider = pulse_1_control & 0b1111;
		pulse_1_envelope_start = 0;
	}
	else
	{
		if (pulse_1_envelope_divider > 0)
		{
			pulse_1_envelope_divider--;
		}
		else
		{
			pulse_1_envelope_divider = pulse_1_control & 0b1111;
			if (pulse_1_envelope_decay > 0)
			{
				pulse_1_envelope_decay--;
			}
			else if (pulse_1_envelope_loop)
			{
				pulse_1_envelope_decay = COARSE_MAX_VOLUME;
			}
		}
	}
	
	// Pulse 2
	unsigned char pulse_2_envelope_loop = (pulse_1_control & 0b00100000) == 0b00100000;
	
	if (pulse_2_envelope_start)
	{
		pulse_2_envelope_decay = COARSE_MAX_VOLUME;
		pulse_2_envelope_divider = pulse_2_control & 0b1111;
		pulse_2_envelope_start = 0;
	}
	else
	{
		if (pulse_2_envelope_divider > 0)
		{
			pulse_2_envelope_divider--;
		}
		else
		{
			pulse_2_envelope_divider = pulse_2_control & 0b1111;
			if (pulse_2_envelope_decay > 0)
			{
				pulse_2_envelope_decay--;
			}
			else if (pulse_2_envelope_loop)
			{
				pulse_2_envelope_decay = COARSE_MAX_VOLUME;
			}
		}
	}
	
	// Noise
	unsigned char noise_envelope_loop = (noise_control & 0b00100000) == 0b00100000;
	
	if (noise_envelope_start)
	{
		noise_envelope_decay = COARSE_MAX_VOLUME;
		noise_envelope_divider = noise_control & 0b1111;
		noise_envelope_start = 0;
	}
	else
	{
		if (noise_envelope_divider > 0)
		{
			noise_envelope_divider--;
		}
		else
		{
			noise_envelope_divider = noise_control & 0b1111;
			if (noise_envelope_decay > 0)
			{
				noise_envelope_decay--;
			}
			else if (noise_envelope_loop)
			{
				noise_envelope_decay = COARSE_MAX_VOLUME;
			}
		}
	}
}

// Generates a random stream of bits from a 15-bit register.
// Right shifts, filling in bit 14 with bit 0 XOR bit 6 or bit 0 XOR bit 1,
// depending on the loop mode.
void shift_noise_bit_stream()
{
	unsigned char loop_mode = (noise_period_control & 0b10000000) == 0b10000000;
	unsigned char feedback_bit;
	// bit 0 XOR bit 6 mode
	if (loop_mode)
	{
		feedback_bit = (noise_bit_stream & 0b1) ^ ((noise_bit_stream >> 6) & 0b1);
	}
	// bit 0 XOR bit 1 mode
	else
	{
		feedback_bit = (noise_bit_stream & 0b1) ^ ((noise_bit_stream >> 1) & 0b1);
	}
	
	noise_bit_stream = ((noise_bit_stream >> 1) & 0b11111111111111) | ((feedback_bit << 14) & 0b100000000000000);
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
	
	clock_envelope();
}

void half_frame_clock()
{
	if ((triangle_length_counter > 0) && ((triangle_linear_control & 0b10000000) == 0))
	{
		triangle_length_counter--;
	}
	
	if ((pulse_1_length_counter > 0) && ((pulse_1_control & 0b00100000) == 0))
	{
		pulse_1_length_counter--;
	}
	
	if ((pulse_2_length_counter > 0) && ((pulse_2_control & 0b00100000) == 0))
	{
		pulse_2_length_counter--;
	}
	
	if ((noise_length_counter > 0) && (noise_control & 0b00100000) == 0)
	{
		noise_length_counter--;
	}
	
	sweep_pulse();
}

void mix_audio()
{
	if (apu_buffer_length >= APU_BUFFER_MAX)
	{
		return;
	}
	
	unsigned char duty_select = (pulse_1_control >> 6) & 0b11;
	// 0 == use envelope decay, 1 == use constant volume
	unsigned char pulse_1_volume_flag = (pulse_1_control & 0b00010000) == 0b00010000;
	unsigned char pulse_1_high;
	if (pulse_1_volume_flag)
	{
		pulse_1_high = (pulse_1_control & 0b1111);
	}
	else
	{
		pulse_1_high = pulse_1_envelope_decay;
	}
	unsigned char pulse_1_volume = pulse_1_high * ((duty_sequence[pulse_1_duty_index] >> duty_select) & 0b1);
	unsigned int pulse_1_timer_value = pulse_1_timer_low + ((pulse_1_timer_high & 0b111) << 8);
	if ((pulse_1_timer_value < 8) || (pulse_1_length_counter == 0))
	{
		pulse_1_volume = 0;
	}
	
	duty_select = (pulse_2_control >> 6) & 0b11;
	// 0 == use envelope decay, 1 == use constant volume
	unsigned char pulse_2_volume_flag = (pulse_2_control & 0b00010000) == 0b00010000;
	unsigned char pulse_2_high;
	if (pulse_2_volume_flag)
	{
		pulse_2_high = (pulse_2_control & 0b1111);
	}
	else
	{
		pulse_2_high = pulse_2_envelope_decay;
	}
	unsigned char pulse_2_volume = pulse_2_high * ((duty_sequence[pulse_2_duty_index] >> duty_select) & 0b1);
	unsigned int pulse_2_timer_value = pulse_2_timer_low + ((pulse_2_timer_high & 0b111) << 8);
	if ((pulse_2_timer_value < 8) || (pulse_2_length_counter == 0))
	{
		pulse_2_volume = 0;
	}
	
	unsigned char noise_volume_flag = (noise_control & 0b00010000) == 0b00010000;
	unsigned char noise_high;
	if (noise_volume_flag)
	{
		noise_high = (noise_control & 0b1111);
	}
	else
	{
		noise_high = noise_envelope_decay;
	}
	unsigned char noise_volume = noise_high * !(noise_bit_stream & 0b1);
	if (noise_length_counter == 0)
	{
		noise_volume = 0;
	}
	
	float pulse_1_mix = ((0.00752 * pulse_1_volume) - (0.00752 * (pulse_1_high / 2.0))) * !pulse_1_silence;
	float pulse_2_mix = ((0.00752 * pulse_2_volume) - (0.00752 * (pulse_2_high / 2.0))) * !pulse_2_silence;
	float triangle_mix = ((0.00851  * sequencer[sequencer_index]) - (0.00851 * 7.5)) * !triangle_silence;
	float noise_mix = ((0.00494 * noise_volume) - (0.00494 * noise_high / 2.0)) * !noise_silence;
	float dmc_mix = ((0.00335 * dmc_output_level) - (0.00335 * FINE_MAX_VOLUME / 2.0)) * !sample_silence;
	
	float pulse_out = pulse_1_mix + pulse_2_mix;
	float tnd_out = triangle_mix + noise_mix + dmc_mix;
	mixer_buffer[apu_buffer_length] = pulse_out + tnd_out + 0.5;
	apu_buffer_length++;
}

// For convenience, each tick will represent a half clock for the APU.
// Period timers count down every clock, except triangle timer, which counts down every half clock.
// Linear counters count down every quarter frame. Length counters count down every half frame.
void apu_tick()
{
	if ((apu_status & 0b1) == 0)
	{
		pulse_1_length_counter = 0;
	}
	
	if ((apu_status & 0b10) == 0)
	{
		pulse_2_length_counter = 0;
	}
	
	if ((apu_status & 0b1000) == 0)
	{
		noise_length_counter = 0;
	}
	
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
		apu_half_clock_count = (apu_half_clock_count + 1) % FIVE_STEP_FRAME_LENGTH;
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
		apu_half_clock_count = (apu_half_clock_count + 1) % FOUR_STEP_FRAME_LENGTH;
	}
	
	unsigned char dmc_loop = (dmc_control >> 6) & 0b1;
	if (dmc_rate_count == 0)
	{
		if (dmc_bits_remaining == 0)
		{
			if (dmc_bytes_remaining > 0)
			{
				dmc_silence_flag = 0;
				dmc_current_address++;
				if (dmc_current_address > 0xFFFF)
				{
					dmc_current_address = 0x8000;
				}
				// TODO: This is a bit quick and dirty. To be more cycle-accurate, should halt
				// the CPU to allow it to do a read from memory. But it works for now.
				access_cpu_memory(&dmc_sample_buffer, dmc_current_address, READ);
				dmc_bytes_remaining--;
				dmc_bits_remaining = 8;
			}
			else if (dmc_loop)
			{
				dmc_silence_flag = 0;
				// Sample address = %11AAAAAA.AA000000
				dmc_current_address = 0xC000 | (dmc_sample_address << 6);
				// Sample length = %LLLL.LLLL0001
				dmc_bytes_remaining = (dmc_sample_length << 4) | 1;
				// TODO: This is a bit quick and dirty. To be more cycle-accurate, should halt
				// the CPU to allow it to do a read from memory. But it works for now.
				access_cpu_memory(&dmc_sample_buffer, dmc_current_address, READ);
				dmc_bytes_remaining--;
				dmc_bits_remaining = 8;
			}
			else
			{
				dmc_silence_flag = 1;
			}
		}
		
		if (!dmc_silence_flag)
		{
			// Each bit of a sample changes the level. A 1 adds 2 to the output level,
			// and a 0 subtracts 2 from the output level.
			// The output level is clamped between 0 and 127.
			unsigned char output_bit = dmc_sample_buffer & 0b1;
			if (output_bit && (dmc_output_level < 126))
			{
				dmc_output_level += 2;
			}
			else if (dmc_output_level > 1)
			{
				dmc_output_level -= 2;
			}
			dmc_bits_remaining--;
			dmc_sample_buffer = dmc_sample_buffer >> 1;
		}
		dmc_rate_count = dmc_rate_table[dmc_control & 0b1111];
	}
	else
	{
		dmc_rate_count--;
	}
	
	// Anything that should only occur on whole APU clocks goes here.
	if ((apu_half_clock_count % 2) == 0)
	{
		if (pulse_1_length_counter > 0)
		{
			if (pulse_1_timer_count == 0)
			{
				pulse_1_duty_index = (pulse_1_duty_index + 1) % duty_size;
				pulse_1_timer_count = pulse_1_timer_low + ((pulse_1_timer_high & 0b111) << 8);;
			}
			else
			{
				pulse_1_timer_count--;
			}
		}
		
		if (pulse_2_length_counter > 0)
		{
			if (pulse_2_timer_count == 0)
			{
				pulse_2_duty_index = (pulse_2_duty_index + 1) % duty_size;
				pulse_2_timer_count = pulse_2_timer_low + ((pulse_2_timer_high & 0b111) << 8);;
			}
			else
			{
				pulse_2_timer_count--;
			}
		}
		
		if (noise_length_counter > 0)
		{
			if (noise_timer_count == 0)
			{
				shift_noise_bit_stream();
				noise_timer_count = noise_period_table[noise_period_control & 0b1111];
			}
			else
			{
				noise_timer_count--;
			}
		}
		
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
	
	pulse_1_control = 0;
	pulse_1_sweep = 0;
	pulse_1_timer_low = 0;
	pulse_1_timer_high = 0;
	pulse_1_timer_count = 0;
	pulse_1_duty_index = 0;
	pulse_1_length_counter = 0;
	pulse_1_sweep_divider = 0;
	pulse_1_sweep_reload = 0;
	pulse_1_target_period = 0;
	pulse_1_envelope_start = 0;
	pulse_1_envelope_divider = 0;
	pulse_1_envelope_decay = 0;
	
	pulse_2_control = 0;
	pulse_2_sweep = 0;
	pulse_2_timer_low = 0;
	pulse_2_timer_high = 0;
	pulse_2_timer_count = 0;
	pulse_2_duty_index = 0;
	pulse_2_length_counter = 0;
	pulse_2_sweep_divider = 0;
	pulse_2_sweep_reload = 0;
	pulse_2_target_period = 0;
	pulse_2_envelope_start = 0;
	pulse_2_envelope_divider = 0;
	pulse_2_envelope_decay = 0;
	
	// Each duty byte consists of the four bits from each duty sequence.
	// Bit X selects the sample from duty sequence X.
	duty_sequence = malloc(sizeof(char) * duty_size);
	duty_sequence[0] = 0b1000;
	duty_sequence[1] = 0b1000;
	duty_sequence[2] = 0b1000;
	duty_sequence[3] = 0b1000;
	duty_sequence[4] = 0b1100;
	duty_sequence[5] = 0b1100;
	duty_sequence[6] = 0b0110;
	duty_sequence[7] = 0b0111;
	
	noise_control = 0;
	noise_period_control = 0;
	noise_length_counter_load = 0;
	noise_timer_count = 0;
	// Noise channel's randomized bit stream must be initialized to 1, because if
	// it is set to 0, it will only ever produce 0s.
	noise_bit_stream = 1;
	noise_length_counter = 0;
	noise_envelope_start = 0;
	noise_envelope_divider = 0;
	noise_envelope_decay = 0;
	
	noise_period_table = malloc(sizeof(int) * 16);
	noise_period_table[0] = 4;
	noise_period_table[1] = 8;
	noise_period_table[2] = 16;
	noise_period_table[3] = 32;
	noise_period_table[4] = 64;
	noise_period_table[5] = 96;
	noise_period_table[6] = 128;
	noise_period_table[7] = 160;
	noise_period_table[8] = 202;
	noise_period_table[9] = 254;
	noise_period_table[10] = 380;
	noise_period_table[11] = 508;
	noise_period_table[12] = 762;
	noise_period_table[13] = 1016;
	noise_period_table[14] = 2034;
	noise_period_table[15] = 4068;
	
	apu_buffer_length = 0;
	mixer_buffer = malloc(sizeof(int) * APU_BUFFER_MAX);
	
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
	
	dmc_rate_table = malloc(sizeof(int) * 16);
	dmc_rate_table[0] = 428;
	dmc_rate_table[1] = 380;
	dmc_rate_table[2] = 340;
	dmc_rate_table[3] = 320;
	dmc_rate_table[4] = 286;
	dmc_rate_table[5] = 254;
	dmc_rate_table[6] = 226;
	dmc_rate_table[7] = 214;
	dmc_rate_table[8] = 190;
	dmc_rate_table[9] = 160;
	dmc_rate_table[10] = 142;
	dmc_rate_table[11] = 128;
	dmc_rate_table[12] = 106;
	dmc_rate_table[13] = 84;
	dmc_rate_table[14] = 72;
	dmc_rate_table[15] = 54;
	
	triangle_timer_count = 0;
	
	pulse_1_silence = 0;
	pulse_2_silence = 0;
	triangle_silence = 0;
	noise_silence = 0;
	sample_silence = 0;
}