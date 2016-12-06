#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nes_cpu.h"
#include "controller.h"

unsigned char controller_1_bus;
unsigned char controller_2_bus;

unsigned char controller_1_shift;
unsigned char controller_2_shift;

unsigned char controller_1_data;
unsigned char controller_2_data;

unsigned char controller_1_pending_write;
unsigned char controller_2_pending_write;

void controller_tick()
{
	
}

void write_controller_state(unsigned char* data, unsigned int address)
{
	if (address == 0x4016)
	{
		controller_1_bus = *data;
		if ((controller_1_bus & 0b1) == 0b1)
		{
			controller_1_shift = 0;
		}
	}
	else if (address == 0x4017)
	{
		controller_2_bus = *data;
		if ((controller_2_bus & 0b1) == 0b1)
		{
			controller_2_shift = 0;
		}
	}
	else
	{
		printf("Controller access error: attempted to access nonexistent controller register %04X\n", address);
		exit_emulator();
	}
}

void read_controller_state(unsigned char* data, unsigned int address)
{
	if (address == 0x4016)
	{
		if (controller_1_shift > 7)
		{
			controller_1_bus = 0;
		}
		else
		{
			controller_1_bus = (controller_1_data >> controller_1_shift) & 0b1;
			controller_1_shift++;
		}
		*data = controller_1_bus;
	}
	else if (address == 0x4017)
	{
		if (controller_2_shift > 7)
		{
			controller_2_bus = 0;
		}
		else
		{
			controller_2_bus = (controller_2_data >> controller_2_shift) & 0b1;
			controller_2_shift++;
		}
		*data = controller_2_bus;
	}
	else
	{
		printf("Controller access error: attempted to access nonexistent controller register %04X\n", address);
		exit_emulator();
	}
}

void controller_init()
{
	controller_1_bus = 0;
	controller_2_bus = 0;
	
	controller_1_shift = 0;
	controller_2_shift = 0;
	
	controller_1_data = 0;
	controller_2_data = 0;
	
	controller_1_pending_write = 0;
	controller_2_pending_write = 0;
}