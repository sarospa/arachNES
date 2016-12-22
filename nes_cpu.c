#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include "emu_nes.h"
#include "nes_cpu.h"
#include "nes_apu.h"
#include "nes_ppu.h"
#include "controller.h"
#include "cartridge.h"

unsigned const char WRITE = 1;
unsigned const char READ = 0;
unsigned const int RAM_SIZE = 2048;
unsigned const int FIRST_HALF_DECODE_LINES = 65;
unsigned const int SECOND_HALF_DECODE_LINES = 66;

// Struct for holding data on a decode ROM line.
// Each line has an opcode condition for turning on, in which each bit
// may be 0, 1, or either. To represent either, we set the bit to 0, then
// use a mask to ignore the bit in the opcode. Each line also has a timing
// condition, in which we must be in one of T0 through T5, or any. The six
// bits of the timing char each represent one timing cycle, and 000000
// represents any cycle.
struct DecodeLine
{
	unsigned char opcode_bits;
	unsigned char opcode_mask;
	unsigned char timing;
	// Flag to indicate that this line won't turn on if push/pull is on.
	unsigned char push_pull_negate;
	void (*rom_op)(void);
};

unsigned char accumulator = 0;
unsigned char x_register = 0;
unsigned char y_register = 0;
unsigned int program_counter = 0;
unsigned char stack_pointer = 0xFF;
unsigned char status_flags = 0;

unsigned char oam_dma_active = 0;
unsigned char oam_dma_page = 0;

unsigned char timing_cycle = 0b000100;
unsigned char next_timing_cycle = 0;
unsigned int address_bus = 0;
unsigned char address_low_bus = 0;
unsigned char address_high_bus = 0;
unsigned char data_bus = 0;
unsigned char predecode_bus = 0;
unsigned char execute_bus = 0xEA; // For now, start off execute bus on NOP
// Misc storage value. In the 6502, this might be represented by various
// different internal buses, but since I don't know what a lot of them
// mean, I'm not going to pretend that I do.
unsigned char internal_storage = 0;

// Represents lines for ops that do something in the first half of the cycle.
struct DecodeLine* decode_lines_first_half;
// Represents lines for ops that do something in the second half of the cycle.
struct DecodeLine* decode_lines_second_half;
// The push/pull op negates certain other ops, so there needs to be special
// case handling for that.
unsigned char PUSH_PULL_OP_INDEX = 0;

unsigned int total_cycles = 0;

unsigned char* cpu_ram;

// Maps CPU addresses to memory pointers. 'access_type' is a flag to indicate whether it is a read or write access.
void access_cpu_memory(unsigned char* data, unsigned int address, unsigned char access_type)
{
	if (access_type == READ)
	{
		// First 2KB is the NES's own CPU RAM.
		// 0x0800 through 0x1FFF mirrors CPU RAM three times.
		if (address <= 0x1FFF)
		{
			unsigned int cpu_ram_address = address % 0x800;
			*data = cpu_ram[cpu_ram_address];
		}
		// PPU registers. Found in 0x2000 through 0x2007, but they're mirrored every 8 bytes.
		else if (address >= 0x2000 && address <= 0x3FFF)
		{
			int ppu_register = 0x2000 + (address % 8);
			access_ppu_register(data, ppu_register, access_type);
		}
		else if (address == 0x4014)
		{
			oam_dma_active = 1;
			*data = oam_dma_page;
		}
		// Player 1 controller port.
		// Will have to figure that out eventually.
		else if (address == 0x4016)
		{
			read_controller_state(data, address);
		}
		// 0x4017 is weird because it's partly controller port 2 and partly APU frame counter.
		// It appears that reads only get the controller data, though.
		else if (address == 0x4017)
		{
			read_controller_state(data, address);
		}
		else if (address >= 0x4000 && address <= 0x4017)
		{
			apu_read(data, address);
		}
		else if (address >= 0x6000 && address <= 0xFFFF)
		{
			get_pointer_at_prg_address(data, address, access_type);
		}
		else
		{
			printf("Unhandled CPU address %04X\n", address);
			exit_emulator();
		}
	}
	else // access_type == WRITE
	{
		// First 2KB is the NES's own CPU RAM.
		// 0x0800 through 0x1FFF mirrors CPU RAM three times.
		if (address <= 0x1FFF)
		{
			unsigned int cpu_ram_address = address % 0x800;
			cpu_ram[cpu_ram_address] = *data;
		}
		// PPU registers. Found in 0x2000 through 0x2007, but they're mirrored every 8 bytes.
		else if (address >= 0x2000 && address <= 0x3FFF)
		{
			int ppu_register = 0x2000 + (address % 8);
			access_ppu_register(data, ppu_register, access_type);
		}
		else if (address == 0x4014)
		{
			oam_dma_active = 1;
			oam_dma_page = *data;
		}
		// Player 1 controller port.
		else if (address == 0x4016)
		{
			write_controller_state(data, address);
		}
		// 0x4017 is weird because it's partly controller port 2 and partly APU frame counter.
		// It appears that writes only go to the APU, though.
		else if (address >= 0x4000 && address <= 0x4017)
		{
			apu_write(data, address);
		}
		else if (address >= 0x6000 && address <= 0xFFFF)
		{
			get_pointer_at_prg_address(data, address, access_type);
		}
		else
		{
			printf("Unhandled CPU address %04X\n", address);
			exit_emulator();
		}
	}
}

unsigned int immediate_address()
{
	return program_counter;
}

unsigned int zero_page_address()
{
	unsigned char data;
	access_cpu_memory(&data, program_counter, READ);
	return data;
}

// Should wrap around if it goes past the zero page.
unsigned int zero_page_indexed_address(unsigned char offset)
{
	unsigned char data;
	access_cpu_memory(&data, program_counter, READ);
	return (data + offset) % 0x100;
}

unsigned int absolute_address()
{
	unsigned char low_byte;
	access_cpu_memory(&low_byte, program_counter, READ);
	unsigned char high_byte;
	access_cpu_memory(&high_byte, program_counter + 1, READ);
	return low_byte + (high_byte * 0x100);
}

unsigned int absolute_indexed_address(unsigned char offset, unsigned int* cycles)
{
	unsigned char low_byte;
	access_cpu_memory(&low_byte, program_counter, READ);
	unsigned char high_byte;
	access_cpu_memory(&high_byte, program_counter + 1, READ);
	unsigned int address = low_byte + (high_byte * 0x100);
	unsigned int indexed_address = (address + offset) % 0x10000;
	if ((address & 0xFF00) != (indexed_address & 0xFF00))
	{
		*cycles += 1;
	}
	return indexed_address;
}

unsigned int zero_page_indirect_address()
{
	unsigned char indirect_address_byte;
	access_cpu_memory(&indirect_address_byte, program_counter, READ);
	unsigned char low_byte;
	access_cpu_memory(&low_byte, indirect_address_byte, READ);
	unsigned char high_byte;
	access_cpu_memory(&high_byte, (indirect_address_byte + 1) % 0x100, READ);
	return low_byte + (high_byte * 0x100);
}

// Adds the index before looking up the indirect address. Always uses the x register as the index.
unsigned int preindexed_indirect_address()
{
	unsigned char indirect_address_byte;
	access_cpu_memory(&indirect_address_byte, program_counter, READ);
	indirect_address_byte += x_register;
	unsigned char low_byte;
	access_cpu_memory(&low_byte, indirect_address_byte, READ);
	unsigned char high_byte;
	access_cpu_memory(&high_byte, (indirect_address_byte + 1) % 0x100, READ);
	unsigned int address = low_byte + (high_byte * 0x100);
	return address;
}

// Adds the index after looking up the indirect address. Always uses the y register as the index.
unsigned int postindexed_indirect_address(unsigned int* cycles)
{
	unsigned char indirect_address_byte;
	access_cpu_memory(&indirect_address_byte, program_counter, READ);
	unsigned char low_byte;
	access_cpu_memory(&low_byte, indirect_address_byte, READ);
	unsigned char high_byte;
	access_cpu_memory(&high_byte, (indirect_address_byte + 1) % 0x100, READ);
	unsigned int address = low_byte + (high_byte * 0x100);
	unsigned int indexed_address = (address + y_register) % 0x10000;
	if ((address & 0xFF00) != (indexed_address & 0xFF00))
	{
		*cycles += 1;
	}
	return indexed_address;
}

// Sets the negative flag if the high bit of the input is set, clears it otherwise.
void test_negative_flag(unsigned char byte)
{
	if ((byte & 0b10000000) == 0b10000000)
	{
		status_flags = status_flags | 0b10000000;
	}
	else
	{
		status_flags = status_flags & 0b01111111;
	}
}

// Sets the zero flag if the byte is zero, clears it otherwise.
void test_zero_flag(unsigned char byte)
{
	if (byte == 0)
	{
		status_flags = status_flags | 0b00000010;
	}
	else
	{
		status_flags = status_flags & 0b11111101;
	}
}

// Sets the carry flag if a + b would carry, clears it otherwise.
void test_carry_addition(unsigned char operand_a, unsigned char operand_b, unsigned char carry_bit)
{
	if ((operand_a + operand_b + carry_bit) > 0xFF)
	{
		status_flags = status_flags | 0b00000001;
	}
	else
	{
		status_flags = status_flags & 0b11111110;
	}
}

// Sets the carry flag if a - b wouldn't require a borrow, clears it otherwise.
void test_carry_subtraction(unsigned char operand_a, unsigned char operand_b)
{
	if (operand_a >= operand_b)
	{
		status_flags = status_flags | 0b00000001;
	}
	else
	{
		status_flags = status_flags & 0b11111110;
	}
}

// Sets the overflow flag if the operands, treated as signed bytes, will overflow when added. Clears it otherwise.
void test_overflow_addition(unsigned char operand_a, unsigned char operand_b, unsigned char carry_bit)
{
	// If the operands have opposite signs, they can't overflow.
	if ((operand_a >= 0x80) != (operand_b >= 0x80))
	{
		status_flags = status_flags & 0b10111111;
	}
	else
	{
		unsigned char result = operand_a + operand_b + carry_bit;
		// If A and B have the same sign, there's an overflow if the sum has a different sign.
		if ((result >= 0x80) == (operand_a >= 0x80))
		{
			status_flags = status_flags & 0b10111111;
		}
		else
		{
			status_flags = status_flags | 0b01000000;
		}
	}
}

// Sets the carry flag to bit 0.
void test_carry_right_shift(unsigned char byte)
{
	if ((byte & 0b1) == 0b1)
	{
		status_flags = status_flags | 0b00000001;
	}
	else
	{
		status_flags = status_flags & 0b11111110;
	}
}

// Sets the carry flag to bit 7.
void test_carry_left_shift(unsigned char byte)
{
	if ((byte & 0b10000000) == 0b10000000)
	{
		status_flags = status_flags | 0b00000001;
	}
	else
	{
		status_flags = status_flags & 0b11111110;
	}
}

// Returns the number of cycles spent.
unsigned int branch_on_status_flags(unsigned char mask, unsigned char value)
{
	unsigned int cycles = 2;
	if ((status_flags & mask) == value)
	{
		cycles++;
		unsigned char branch_value;
		access_cpu_memory(&branch_value, immediate_address(), READ);
		unsigned char adjusted_branch_value = branch_value;
		unsigned int skip_compare = program_counter + 1;
		// Branch values are signed, so values with the high bit set are negative.
		if (branch_value > 127)
		{
			adjusted_branch_value = (~branch_value) + 1;
			program_counter -= adjusted_branch_value;
		}
		else
		{
			program_counter += adjusted_branch_value;
		}
		// Add a cycle if page boundary was crossed, compared to the address of the next instruction.
		if ((skip_compare & 0xFF00) != (program_counter & 0xFF00))
		{
			cycles++;
		}
	}
	program_counter += 1;
	return cycles;
}

/* Arithmetic with carry is weird. These two functions might still have issues, because how
   the 6502 does arithmetic is pretty hard to understand. Keep an eye on them. */
   
// Adds the data byte to the accumulator. Works as both signed or unsigned addition.
void add_with_carry(unsigned char data)
{
	unsigned char carry_bit = status_flags & 0b1;
	// Carry bit is for unsigned addition, so we should test signed overflow before adding it.
	test_overflow_addition(accumulator, data, carry_bit);
	test_carry_addition(accumulator, data, carry_bit);
	// The carry bit is added to the data byte. Normally it should start cleared for addition.
	accumulator += data + carry_bit;
	test_negative_flag(accumulator);
	test_zero_flag(accumulator);
}

// Subtracts the data byte from the accumulator. Works by taking the two's complement of the data byte and adding it.
void subtract_with_carry(unsigned char data)
{
	unsigned char carry_bit = status_flags & 0b1;
	test_overflow_addition(accumulator, ~data, carry_bit);
	test_carry_addition(accumulator, ~data, carry_bit);
	accumulator += (~data) + carry_bit;
	test_negative_flag(accumulator);
	test_zero_flag(accumulator);
}

void bitwise_and(unsigned char data)
{
	accumulator = data & accumulator;
	test_negative_flag(accumulator);
	test_zero_flag(accumulator);
}

void bitwise_or(unsigned char data)
{
	accumulator = data | accumulator;
	test_negative_flag(accumulator);
	test_zero_flag(accumulator);
}

void bitwise_xor(unsigned char data)
{
	accumulator = data ^ accumulator;
	test_negative_flag(accumulator);
	test_zero_flag(accumulator);
}

void compare_register(unsigned char nes_register, unsigned char data)
{
	test_negative_flag(nes_register - data);
	test_zero_flag(nes_register - data);
	test_carry_subtraction(nes_register, data);
}

void load_register(unsigned char* nes_register, unsigned char data)
{
	*nes_register = data;
	test_negative_flag(data);
	test_zero_flag(data);
}

unsigned char arithmetic_shift_left(unsigned char data)
{
	test_carry_left_shift(data);
	data = data << 1;
	test_negative_flag(data);
	test_zero_flag(data);
	return data;
}

unsigned char logical_shift_right(unsigned char data)
{
	test_carry_right_shift(data);
	data = data >> 1;
	test_negative_flag(data);
	test_zero_flag(data);
	return data;
}

unsigned char rotate_left(unsigned char data)
{
	unsigned char carry_bit = status_flags & 0b1;
	test_carry_left_shift(data);
	data = (data << 1) | carry_bit;
	test_negative_flag(data);
	test_zero_flag(data);
	return data;
}

unsigned char rotate_right(unsigned char data)
{
	unsigned char carry_bit = (status_flags & 0b1) << 7;
	test_carry_right_shift(data);
	data = (data >> 1) | carry_bit;
	test_negative_flag(data);
	test_zero_flag(data);
	return data;
}

// Pushes a byte to the stack. The stack starts at $01FF and grows downward.
void push_to_stack(unsigned char byte)
{
	access_cpu_memory(&byte, STACK_PAGE + stack_pointer, WRITE);
	stack_pointer--;
}

// Pulls a byte off the stack.
unsigned char pull_from_stack()
{
	stack_pointer++;
	unsigned char stack_top_byte;
	access_cpu_memory(&stack_top_byte, STACK_PAGE + stack_pointer, READ);
	return stack_top_byte;
}

void stack_dump()
{
	printf("STACK DUMP\n");
	for (unsigned int i = 0x01FF; i > (STACK_PAGE + stack_pointer); i--)
	{
		printf("%04X: %02X\n", i, cpu_ram[i]);
	}
}

void cpu_save_state(FILE* save_file)
{
	fwrite(&accumulator, sizeof(char), 1, save_file);
	fwrite(&x_register, sizeof(char), 1, save_file);
	fwrite(&y_register, sizeof(char), 1, save_file);
	fwrite(&program_counter, sizeof(int), 1, save_file);
	fwrite(&stack_pointer, sizeof(char), 1, save_file);
	fwrite(&status_flags, sizeof(char), 1, save_file);
	fwrite(&oam_dma_active, sizeof(char), 1, save_file);
	fwrite(&oam_dma_page, sizeof(char), 1, save_file);
	fwrite(&total_cycles, sizeof(int), 1, save_file);
	fwrite(cpu_ram, sizeof(char), RAM_SIZE, save_file);
}

void cpu_load_state(FILE* save_file)
{
	fread(&accumulator, sizeof(char), 1, save_file);
	fread(&x_register, sizeof(char), 1, save_file);
	fread(&y_register, sizeof(char), 1, save_file);
	fread(&program_counter, sizeof(int), 1, save_file);
	fread(&stack_pointer, sizeof(char), 1, save_file);
	fread(&status_flags, sizeof(char), 1, save_file);
	fread(&oam_dma_active, sizeof(char), 1, save_file);
	fread(&oam_dma_page, sizeof(char), 1, save_file);
	fread(&total_cycles, sizeof(int), 1, save_file);
	fread(cpu_ram, sizeof(char), RAM_SIZE, save_file);
}

void do_nothing()
{
	
}

// Clears the overflow flag. There is no corresponding set opcode.
void op_clv()
{
	status_flags = status_flags & 0b10111111;
}

// Sets the carry flag to bit 5 of the opcode.
void op_T0_clc_sec()
{
	if ((execute_bus & 0b00100000) == 0b00100000)
	{
		status_flags = status_flags | 0b00000001;
	}
	else
	{
		status_flags = status_flags & 0b11111110;
	}
}

// Sets the interrupt flag to bit 5 of the opcode.
void op_T0_cli_sei()
{
	if ((execute_bus & 0b00100000) == 0b00100000)
	{
		status_flags = status_flags | 0b00000100;
	}
	else
	{
		status_flags = status_flags & 0b11111011;
	}
}

// Sets the decimal flag to bit 5 of the opcode.
void op_T0_cld_sed()
{
	if ((execute_bus & 0b00100000) == 0b00100000)
	{
		status_flags = status_flags | 0b00001000;
	}
	else
	{
		status_flags = status_flags & 0b11110111;
	}
}

void op_T0_lda()
{
	load_register(&accumulator, data_bus);
}

// Artificial line. It looks like ALU instructions always increment the
// program counter on T2.
void op_T2_acc()
{
	program_counter++;
}

// On T0, fetch next instruction, load program counter into ADL/ADH, and
// make sure the next cycle is T1 (to override T0+T2 rolling into T1+T3).
void op_T0()
{
	next_timing_cycle = 0b000010;
	predecode_bus = data_bus;
	address_low_bus = program_counter & 0x00FF;
	address_high_bus = (program_counter >> 8) & 0x00FF;
}

// Need to read the next byte for address high, but address low needs
// to be stuffed somewhere to use later.
void op_T2_abs()
{
	address_low_bus = (program_counter + 1) & 0x00FF;
	address_high_bus = ((program_counter + 1) >> 8) & 0x00FF;
	internal_storage = data_bus;
}

// Zero page opcodes reset the timing cycle at T2.
void op_T2_mem_zp()
{
	next_timing_cycle = 0b000001;
}

// Sets the adl/adh buses to the address on the data bus, at the zero page.
void op_T2_ADL_ADD()
{
	address_low_bus = data_bus;
	address_high_bus = 0x00;
}

// Add X register to address low for indexed zero page. Timing cycle also
// resets here.
void op_T3_mem_zp_idx()
{
	address_low_bus += x_register;
	next_timing_cycle = 0b000001;
}

// Load ADL from where we stuffed it last cycle, and ADH from the data bus.
// Timing cycle also resets here.
void op_T3_mem_abs()
{
	address_low_bus = internal_storage;
	address_high_bus = data_bus;
	next_timing_cycle = 0b000001;
	program_counter++;
}

// Two-cycle instructions have an odd behavior where T0 and T2 run at the
// same time. The CPU has a special check for this that covers all two-cycle
// instructions, which is reproduced here.
void activate_T0_for_two_cycle_op()
{
	if (((predecode_bus & 0b00011101) == 0b00001001)
		|| ((predecode_bus & 0b10011101) == 0b10000000)
		|| (((predecode_bus & 0b00001101) == 0b00001000) && !((predecode_bus & 0b10010010) == 0b00000000)))
	{
		timing_cycle = timing_cycle | 0b000001;
	}
}

// Runs a single cycle of the CPU.
// This means that, unlike run_opcode, it's necessary to break down what each
// opcode does on each cycle, and perform only those operations on the
// appropriate cycle. This is vital to getting the CPU as accurate as
// possible, in particular as far as syncing up the CPU with the PPU. Some
// games and certain particular behavior quirks absolutely rely on the CPU
// behaving correctly on every cycle.
void cpu_tick()
{
	access_cpu_memory(&data_bus, address_bus, READ);
	
	//printf("%04X: OP:%02X A:%02X D:%02X ADL:%02X ADH:%02X ADB:%04X T:%02X\n", program_counter, execute_bus, accumulator, data_bus, address_low_bus, address_high_bus, address_bus, timing_cycle);
	
	// Assuming the default behavior is to shift to the next timing cycle.
	next_timing_cycle = (timing_cycle << 1) & 0b111111;
	
	// This should go earlier, but this is kinda necessary for now.
	if ((timing_cycle & 0b000100) == 0b000100)
	{
		activate_T0_for_two_cycle_op();
	}
	
	for (unsigned int i = 0; i < FIRST_HALF_DECODE_LINES; i++)
	{
		// Activate this op if we're on the right timing cycle, and the current opcode matches the pattern.
		if (((execute_bus & decode_lines_first_half[i].opcode_mask) == decode_lines_first_half[i].opcode_bits) && ((timing_cycle & decode_lines_first_half[i].timing) == decode_lines_first_half[i].timing))
		{
			// Skip this op if it's negated by op-push/pull, and op-push/pull is on.
			if (!decode_lines_first_half[i].push_pull_negate || ((execute_bus & decode_lines_second_half[PUSH_PULL_OP_INDEX].opcode_mask) != decode_lines_second_half[PUSH_PULL_OP_INDEX].opcode_bits))
			{
				decode_lines_first_half[i].rom_op();
			}
		}
	}
	
	if ((timing_cycle & 0b000010) == 0b000010)
	{
		address_low_bus = program_counter & 0x00FF;
		address_high_bus = (program_counter >> 8) & 0x00FF;
	}
	
	for (unsigned int i = 0; i < SECOND_HALF_DECODE_LINES; i++)
	{
		// Activate this op if we're on the right timing cycle, and the current opcode matches the pattern.
		if (((execute_bus & decode_lines_second_half[i].opcode_mask) == decode_lines_second_half[i].opcode_bits) && ((timing_cycle & decode_lines_second_half[i].timing) == decode_lines_second_half[i].timing))
		{
			// Skip this op if it's negated by op-push/pull, and op-push/pull is on.
			if (!decode_lines_second_half[i].push_pull_negate || ((execute_bus & decode_lines_second_half[PUSH_PULL_OP_INDEX].opcode_mask) != decode_lines_second_half[PUSH_PULL_OP_INDEX].opcode_bits))
			{
				decode_lines_second_half[i].rom_op();
			}
		}
	}
	
	address_bus = address_low_bus | (address_high_bus << 8);
	
	if ((timing_cycle & 0b000010) == 0b000010)
	{
		execute_bus = predecode_bus;
	}
	
	timing_cycle = next_timing_cycle;
}

// Carries out whatever instruction the opcode represents.
// Returns the number of cycles spent on the instruction.
// Addressing mode cheat sheet:
// Immediate: Passes a constant to the instruction.
// Implied: The instruction has no operand, and its address, if any, is fixed.
// Absolute: Directly passes a two-byte address to the instruction.
// Zero page: Directly passes a one-byte address to the instruction. The high byte is implicitly $00.
// Absolute,X: Same as absolute mode, but with the value of the x register added. There is a corresponding Y mode.
// Zero page,X: Same as zero page mode, but with the value of the x register added. Wraps around rather than crossing page boundary. There is a corresponding Y mode.
// Indirect,X: Written as ($00,X). Works like a zero page indirect, but it adds the x register to the pointer before accessing the address.
// Indirect,Y: Written as ($00),Y. Works like a zero page indirect, but after it accesses the address at the pointer, it adds the y register.
unsigned int run_opcode()
{
	unsigned int cycles = 0;
	
	// Bit of a hack to run the single cycle function if we're mid-instruction.
	if ((timing_cycle & 0b0000100) == 0)
	{
		cpu_tick();
		cycles++;
	}
	else
	{
		switch(execute_bus)
		{
			// NOP: Does nothing.
			// Implied NOP. Hex: $EA  Len: 1  Time: 2
			case 0xEA:
			// Unofficial NOPs.
			case 0x1A:
			case 0x3A:
			case 0x5A:
			case 0x7A:
			case 0xDA:
			case 0xFA:
			{
				cycles = 2;
				break;
			}
			// Unofficial NOPs that skip over the next address, incrementing the program counter by 2.
			case 0x80:
			case 0x82:
			case 0x89:
			case 0xC2:
			case 0xE2:
			{
				program_counter++;
				cycles = 2;
				break;
			}
			// JMP: Jumps to an address specified by the operand.
			// Absolute JMP. Hex: $4C  Len: 3  Time: 3 
			case 0x4C:
			{
				unsigned int address = absolute_address();
				program_counter = address;
				cycles = 3;
				break;
			}
			// Indirect JMP. Hex: $6C  Len: 3  Time: 5
			// Unlike other forms of indirect addressing modes, an absolute address is used instead of a zero page address.
			case 0x6C:
			{
				unsigned char low_byte;
				access_cpu_memory(&low_byte, program_counter, READ);
				unsigned char high_byte;
				access_cpu_memory(&high_byte, program_counter + 1, READ);
				unsigned char indirect_low_byte;
				access_cpu_memory(&indirect_low_byte, low_byte + (high_byte * 0x100), READ);
				low_byte++; // Intended to overflow to 0x00 if it's at 0xFF.
				unsigned char indirect_high_byte;
				access_cpu_memory(&indirect_high_byte, low_byte + (high_byte * 0x100), READ);
				program_counter = indirect_low_byte + (indirect_high_byte * 0x100);
				cycles = 5;
				break;
			}
			// JSR: Jump to subroutine. Pushes the next instruction onto the stack and jumps to an address specified by the operand.
			// Absolute JSR. Hex: $20  Len: 3  Time: 6
			case 0x20:
			{
				// Take the program counter + 2 and push the high byte to the stack, then the low byte.
				unsigned int push_value = program_counter + 1;
				push_to_stack(push_value / 0x100);
				push_to_stack(push_value % 0x100);
				program_counter = absolute_address();
				cycles = 6;
				break;
			}
			// RTS: Return from subroutine. Pops the stack for the return address and jumps back to it.
			// Implied RTS. Hex: $60  Len: 1  Time: 6
			case 0x60:
			{
				unsigned int return_address = 0;
				return_address += pull_from_stack();
				return_address += pull_from_stack() * 0x100;
				// Return address is the next instruction - 1, so we need to add 1 to it.
				program_counter = return_address + 1;
				cycles = 6;
				break;
			}
			// RTI: Return from interrupt. Pops the stack for the status flags, then pops the stack for the return address and jumps back to it.
			// Implied RTS. Hex: $40  Len: 1  Time:  6
			// Affects all flags.
			case 0x40:
			{
				status_flags = pull_from_stack();
				program_counter = pull_from_stack();
				program_counter += pull_from_stack() * 0x100;
				cycles = 6;
				break;
			}
			// BPL: Branches on sign clear.
			// Relative BPL. Hex: $10  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
			case 0x10:
			{
				cycles = branch_on_status_flags(0b10000000, 0b00000000);
				break;
			}
			// BMI: Branches on sign set.
			// Relative BMI. Hex: $30  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
			case 0x30:
			{
				cycles = branch_on_status_flags(0b10000000, 0b10000000);
				break;
			}
			// BVC: Branches on overflow clear.
			// Relative BVC. Hex: $50  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
			case 0x50:
			{
				cycles = branch_on_status_flags(0b01000000, 0b00000000);
				break;
			}
			// BVS: Branches on overflow set.
			// Relative BVS. Hex: $70  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
			case 0x70:
			{
				cycles = branch_on_status_flags(0b01000000, 0b01000000);
				break;
			}
			// BCC: Branches on carry clear.
			// Relative BCC. Hex: $90  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
			case 0x90:
			{
				cycles = branch_on_status_flags(0b00000001, 0b00000000);
				break;
			}
			// BCS: Branches on carry set.
			// Relative BCS. Hex: $B0  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
			case 0xB0:
			{
				cycles = branch_on_status_flags(0b00000001, 0b00000001);
				break;
			}
			// BNE: Branches on zero clear.
			// Relative BNE. Hex: $D0  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
			case 0xD0:
			{
				cycles = branch_on_status_flags(0b00000010, 0b00000000);
				break;
			}
			// BEQ: Branches on zero set.
			// Relative BEQ. Hex: $F0  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
			case 0xF0:
			{
				cycles = branch_on_status_flags(0b00000010, 0b00000010);
				break;
			}
			// TAX: Transfers A to X.
			// Implied TAX. Hex: $AA  Len: 1  Time: 2
			// Affects flags S and Z.
			case 0xAA:
			{
				load_register(&x_register, accumulator);
				cycles = 2;
				break;
			}
			// TXA: Transfers X to A.
			// Implied TXA. Hex: $8A  Len: 1  Time: 2
			// Affects flags S and Z.
			case 0x8A:
			{
				load_register(&accumulator, x_register);
				cycles = 2;
				break;
			}
			// DEX: Decrements the X register.
			// Implied DEX. Hex: $CA  Len: 1  Time: 2
			// Affects flags S and Z.
			case 0xCA:
			{
				x_register--;
				test_negative_flag(x_register);
				test_zero_flag(x_register);
				cycles = 2;
				break;
			}
			// INX: Increments the X register.
			// Implied INX. Hex: $E8  Len: 1  Time: 2
			// Affects flags S and Z.
			case 0xE8:
			{
				x_register++;
				test_negative_flag(x_register);
				test_zero_flag(x_register);
				cycles = 2;
				break;
			}
			// TAY: Transfers A to Y.
			// Implied TAY. Hex: $A8  Len: 1  Time: 2
			// Affects flags S and Z.
			case 0xA8:
			{
				load_register(&y_register, accumulator);
				cycles = 2;
				break;
			}
			// TYA: Transfers Y to A.
			// Implied TYA. Hex: $98  Len: 1  Time: 2
			case 0x98:
			{
				load_register(&accumulator, y_register);
				cycles = 2;
				break;
			}
			// DEY: Decrements the Y register.
			// Implied DEY. Hex: $88  Len: 1  Time: 2
			// Affects flags S and Z.
			case 0x88:
			{
				y_register--;
				test_negative_flag(y_register);
				test_zero_flag(y_register);
				cycles = 2;
				break;
			}
			// INY: Increments the Y register.
			// Implied INY. Hex: $C8  Len: 1  Time: 2
			// Affects flags S and Z.
			case 0xC8:
			{
				y_register++;
				test_negative_flag(y_register);
				test_zero_flag(y_register);
				cycles = 2;
				break;
			}
			// INC: Increment memory byte.
			// Zero page INC. Hex: $E6  Len: 2  Time: 5
			// Affects flags S and Z.
			case 0xE6:
			{
				unsigned int address = zero_page_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target += 1;
				access_cpu_memory(&target, address, WRITE);
				test_negative_flag(target);
				test_zero_flag(target);
				program_counter++;
				cycles = 5;
				break;
			}
			// Zero page,X INC. Hex: $F6  Len: 2  Time: 6
			// Affects flags S and Z.
			case 0xF6:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target += 1;
				access_cpu_memory(&target, address, WRITE);
				test_negative_flag(target);
				test_zero_flag(target);
				program_counter++;
				cycles = 6;
				break;
			}
			// Absolute INC. Hex: $EE  Len: 3  Time: 6
			// Affects flags S and Z.
			case 0xEE:
			{
				unsigned int address = absolute_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target += 1;
				access_cpu_memory(&target, address, WRITE);
				test_negative_flag(target);
				test_zero_flag(target);
				program_counter += 2;
				cycles = 6;
				break;
			}
			// Absolute,X INC. Hex: $FE  Len: 3  Time: 7
			// Affects flags S and Z.
			case 0xFE:
			{
				unsigned int address = absolute_address() + x_register;
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target += 1;
				access_cpu_memory(&target, address, WRITE);
				test_negative_flag(target);
				test_zero_flag(target);
				program_counter += 2;
				cycles = 7;
				break;
			}
			// DEC: Decrement memory byte.
			// Zero page DEC. Hex: $C6  Len: 2  Time: 5
			// Affects flags S and Z.
			case 0xC6:
			{
				unsigned int address = zero_page_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target -= 1;
				access_cpu_memory(&target, address, WRITE);
				test_negative_flag(target);
				test_zero_flag(target);
				program_counter++;
				cycles = 5;
				break;
			}
			// Zero page,X DEC. Hex: $D6  Len: 2  Time: 6
			// Affects flags S and Z.
			case 0xD6:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target -= 1;
				access_cpu_memory(&target, address, WRITE);
				test_negative_flag(target);
				test_zero_flag(target);
				program_counter++;
				cycles = 6;
				break;
			}
			// Absolute DEC. Hex: $CE  Len: 3  Time: 6
			// Affects flags S and Z.
			case 0xCE:
			{
				unsigned int address = absolute_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target -= 1;
				access_cpu_memory(&target, address, WRITE);
				test_negative_flag(target);
				test_zero_flag(target);
				program_counter += 2;
				cycles = 6;
				break;
			}
			// Absolute,X DEC. Hex: $DE  Len: 3  Time: 7
			// Affects flags S and Z.
			case 0xDE:
			{
				unsigned int address = absolute_address() + x_register;
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target -= 1;
				access_cpu_memory(&target, address, WRITE);
				test_negative_flag(target);
				test_zero_flag(target);
				program_counter += 2;
				cycles = 7;
				break;
			}
			// ADC: Adds a value to the accumulator.
			// Immediate ADC. Hex: $69  Len: 2  Time: 2
			// Affects flags S, V, Z, and C.
			case 0x69:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				add_with_carry(load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page ADC. Hex: $65  Len: 2  Time: 3
			// Affects flags S, V, Z, and C.
			case 0x65:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				add_with_carry(load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,X ADC. Hex: $75  Len: 2  Time: 4
			// Affects flags S, V, Z, and C.
			case 0x75:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				add_with_carry(load_byte);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute ADC. Hex: $6D  Len: 3  Time: 4
			// Affects flags S, V, Z, and C.
			case 0x6D:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				add_with_carry(load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,X ADC. Hex: $7D  Len: 3  Time: 4 + 1 [if page boundary crossed]
			// Affects flags S, V, Z, and C.
			case 0x7D:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(x_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				add_with_carry(load_byte);
				program_counter += 2;
				break;
			}
			// Absolute,Y ADC. Hex: $79  Len: 3  Time: 4 + 1 [if page boundary crossed]
			// Affects flags S, V, Z, and C.
			case 0x79:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(y_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				add_with_carry(load_byte);
				program_counter += 2;
				break;
			}
			// Indirect,X ADC. Hex: $61  Len: 2  Time: 6
			// Affects flags S, V, Z, and C.
			case 0x61:
			{
				unsigned int address = preindexed_indirect_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				add_with_carry(load_byte);
				program_counter++;
				cycles = 6;
				break;
			}
			// Indirect,Y ADC. Hex: $71  Len: 2  Time: 5 + 1 [if page boundary crossed]
			// Affects flags S, V, Z, and C.
			case 0x71:
			{
				cycles = 5;
				unsigned int address = postindexed_indirect_address(&cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				add_with_carry(load_byte);
				program_counter++;
				break;
			}
			// SBC: Subtracts a value from the accumulator.
			// Immediate SBC. Hex: $E9  Len: 2  Time: 2
			// Affects flags S, V, Z, and C.
			case 0xE9:
			// Unofficial SBC.
			case 0xEB:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				subtract_with_carry(load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page SBC. Hex: $E5  Len: 2  Time: 3
			// Affects flags S, V, Z, and C.
			case 0xE5:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				subtract_with_carry(load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,X SBC. Hex: $F5  Len: 2  Time: 4
			// Affects flags S, V, Z, and C.
			case 0xF5:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				subtract_with_carry(load_byte);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute SBC. Hex: $ED  Len: 3  Time: 4
			// Affects flags S, V, Z, and C.
			case 0xED:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				subtract_with_carry(load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,X SBC. Hex: $FD  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S, V, Z, and C.
			case 0xFD:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(x_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				subtract_with_carry(load_byte);
				program_counter += 2;
				break;
			}
			// Absolute,Y SBC. Hex: $F9  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S, V, Z, and C.
			case 0xF9:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(y_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				subtract_with_carry(load_byte);
				program_counter += 2;
				break;
			}
			// Indirect,X SBC. Hex: $E1  Len: 2  Time: 6
			// Affects flags S, V, Z, and C.
			case 0xE1:
			{
				unsigned int address = preindexed_indirect_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				subtract_with_carry(load_byte);
				program_counter++;
				cycles = 6;
				break;
			}
			// Indirect,Y SBC. Hex: $F1  Len: 2  Time: 5 + 1 [if crossed page boundary]
			// Affects flags S, V, Z, and C.
			case 0xF1:
			{
				cycles = 5;
				unsigned int address = postindexed_indirect_address(&cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				subtract_with_carry(load_byte);
				program_counter++;
				break;
			}
			// AND: Performs bitwise AND with the accumulator.
			// Immediate AND. Hex: $29  Len: 2  Time: 2
			// Affects flags S and Z.
			case 0x29:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page AND. Hex: $25  Len: 2  Time: 3
			// Affects flags S and Z.
			case 0x25:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,X AND. Hex: $35  Len: 2  Time: 4
			// Affects flags S and Z.
			case 0x35:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute AND. Hex: $2D  Len: 3  Time: 4
			// Affects flags S and Z.
			case 0x2D:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,X AND. Hex: $3D  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0x3D:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(x_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				program_counter += 2;
				break;
			}
			// Absolute,Y AND. Hex: $39  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0x39:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(y_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				program_counter += 2;
				break;
			}
			// Indirect,X AND. Hex: $21  Len: 2  Time: 6
			// Affects flags S and Z.
			case 0x21:
			{
				unsigned int address = preindexed_indirect_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				program_counter++;
				cycles = 6;
				break;
			}
			// Indirect,Y AND. Hex: $31  Len: 2  Time: 5 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0x31:
			{
				cycles = 5;
				unsigned int address = postindexed_indirect_address(&cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				program_counter++;
				break;
			}
			// Unofficial opcode ANC. Performs an immediate AND, then copies flag S into flag C.
			// Hex: $0B or $2B  Len: 2  Time: 2
			// Affects flags S, Z, and C.
			case 0x0B:
			case 0x2B:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				if ((status_flags & 0b10000000) == 0b10000000)
				{
					status_flags = status_flags | 0b00000001;
				}
				else
				{
					status_flags = status_flags & 0b11111110;
				}
				program_counter++;
				cycles = 2;
				break;
			}
			// Unofficial opcode ALR. Performs an immediate AND, then an accumulator logical shift right.
			// Hex: $4B  Len: 2  Time: 2
			// Affects flags S, Z, and C.
			case 0x4B:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_and(load_byte);
				accumulator = logical_shift_right(accumulator);
				program_counter++;
				cycles = 2;
				break;
			}
			// Unofficial opcode ARR. Performs an immediate AND, then an accumulator roll right.
			// Sets flag C to bit 6, sets flag V to bit 6 xor bit 5.
			// Hex: $6B  Len: 2  Time: 2
			case 0x6B:
			{
				// TODO
				program_counter++;
				cycles = 2;
				break;
			}
			// Unofficial opcode AXS.
			// Hex: $CB  Len: 2  Time: 2
			case 0xCB:
			{
				// TODO
				program_counter++;
				cycles = 2;
				break;
			}
			// ORA: Performs bitwise OR with the accumulator.
			// Immediate ORA. Hex: $09  Len: 2  Time: 2
			// Affects flags S and Z.
			case 0x09:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_or(load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page ORA. Hex: $05  Len: 2  Time: 3
			// Affects flags S and Z.
			case 0x05:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_or(load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,X ORA. Hex: $15  Len: 2  Time: 4
			// Affects flags S and Z.
			case 0x15:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_or(load_byte);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute ORA. Hex: $0D  Len: 3  Time: 4
			// Affects flags S and Z.
			case 0x0D:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_or(load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,X ORA. Hex: $1D  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0x1D:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(x_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_or(load_byte);
				program_counter += 2;
				break;
			}
			// Absolute,Y ORA. Hex: $19  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0x19:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(y_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_or(load_byte);
				program_counter += 2;
				break;
			}
			// Indirect,X ORA. Hex: $01  Len: 2  Time: 6
			// Affects flags S and Z.
			case 0x01:
			{
				unsigned int address = preindexed_indirect_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_or(load_byte);
				program_counter++;
				cycles = 6;
				break;
			}
			// Indirect,Y ORA. Hex: $11  Len: 2  Time: 5 + 1 [if crossed page boundary]
			case 0x11:
			{
				cycles = 5;
				unsigned int address = postindexed_indirect_address(&cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_or(load_byte);
				program_counter++;
				break;
			}
			// EOR: Perform bitwise XOR with the accumulator.
			// Immediate EOR. Hex: $49  Len: 2  Time: 2
			// Affect S and Z.
			case 0x49:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_xor(load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page EOR. Hex: $45  Len: 2  Time: 3
			// Affects S and Z.
			case 0x45:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_xor(load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,X EOR. Hex: $55  Len: 2  Time: 4
			// Affects S and Z.
			case 0x55:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_xor(load_byte);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute EOR. Hex: $4D  Len: 3  Time: 4
			// Affects flags S and Z.
			case 0x4D:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_xor(load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,X EOR. Hex: $5D  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0x5D:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(x_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_xor(load_byte);
				program_counter += 2;
				break;
			}
			// Absolute,Y EOR. Hex: $59  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0x59:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(y_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_xor(load_byte);
				program_counter += 2;
				break;
			}
			// Indirect,X EOR. Hex: $41  Len: 2  Time: 6
			// Affects flags S and Z.
			case 0x41:
			{
				unsigned int address = preindexed_indirect_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_xor(load_byte);
				program_counter += 2;
				cycles = 6;
				break;
			}
			// Indirect,Y EOR. Hex: $51  Len: 2  Time: 5 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0x51:
			{
				cycles = 5;
				unsigned int address = postindexed_indirect_address(&cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				bitwise_xor(load_byte);
				program_counter++;
				break;
			}
			// LSR: Logical shift right.
			// Accumulator LSR. Hex: $4A  Len: 1  Time: 2
			// Affects flags S, Z, and C.
			case 0x4A:
			{
				accumulator = logical_shift_right(accumulator);
				cycles = 2;
				break;
			}
			// Zero page LSR. Hex: $46  Len: 2  Time: 5
			// Affects flags S, Z, and C.
			case 0x46:
			{
				unsigned int address = zero_page_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = logical_shift_right(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter++;
				cycles = 5;
				break;
			}
			// Zero page,X LSR. Hex: $56  Len: 2  Time: 6
			// Affects flags S, Z, and C.
			case 0x56:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = logical_shift_right(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter++;
				cycles = 6;
				break;
			}
			// Absolute LSR. Hex: $4E  Len: 3  Time: 6
			// Affects flags S, Z, and C.
			case 0x4E:
			{
				unsigned int address = absolute_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = logical_shift_right(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter += 2;
				cycles = 6;
				break;
			}
			// Absolute,X LSR. Hex: $5E  Len: 3  Time: 7
			// Affects flags S, Z, and C.
			case 0x5E:
			{
				unsigned int address = absolute_address() + x_register;
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = logical_shift_right(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter += 2;
				cycles = 7;
				break;
			}
			// ASL: Arithmetic shift left.
			// Accumulator ASL. Hex: $0A  Len: 1  Time: 2
			// Affects flags S, Z, and C.
			case 0x0A:
			{
				accumulator = arithmetic_shift_left(accumulator);
				cycles = 2;
				break;
			}
			// Zero page ASL. Hex: $06  Len: 2  Time: 5
			// Affects flags S, Z, and C.
			case 0x06:
			{
				unsigned int address = zero_page_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = arithmetic_shift_left(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter++;
				cycles = 5;
				break;
			}
			// Zero page,X ASL. Hex: $16  Len: 2  Time: 6
			case 0x16:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = arithmetic_shift_left(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter++;
				cycles = 6;
				break;
			}
			// Absolute ASL. Hex: $0E  Len: 3  Time: 6
			// Affects flags S, Z, and C.
			case 0x0E:
			{
				unsigned int address = absolute_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = arithmetic_shift_left(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter += 2;
				cycles = 6;
				break;
			}
			// Absolute,X ASL. Hex: $1E  Len: 3  Time: 7
			// Affects flags S, Z, and C.
			case 0x1E:
			{
				unsigned int address = absolute_address() + x_register;
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = arithmetic_shift_left(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter += 2;
				cycles = 7;
				break;
			}
			// ROL: Rotate left. The byte that falls off the edge goes into carry, and the carry bit goes in the other side.
			// Accumulator ROL. Hex: $2A  Len: 1  Time: 2
			// Affects flags S, Z, and C.
			case 0x2A:
			{
				accumulator = rotate_left(accumulator);
				cycles = 2;
				break;
			}
			// Zero page ROL. Hex: $26  Len: 2  Time: 5
			// Affects flags S, Z, and C.
			case 0x26:
			{
				unsigned int address = zero_page_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = rotate_left(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter++;
				cycles = 5;
				break;
			}
			// Zero page,X ROL. Hex: $36  Len: 2  Time: 6
			// Affects flags S, Z, and C.
			case 0x36:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = rotate_left(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter++;
				cycles = 6;
				break;
			}
			// Absolute ROL. Hex: $2E  Len: 3  Time: 6
			// Affects flags S, Z, and C.
			case 0x2E:
			{
				unsigned int address = absolute_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = rotate_left(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter += 2;
				cycles = 6;
				break;
			}
			// Absolute,X ROL. Hex: $3E  Len: 3  Time: 7
			// Affects flags S, Z, and C.
			case 0x3E:
			{
				unsigned int address = absolute_address() + x_register;
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = rotate_left(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter += 2;
				cycles = 7;
				break;
			}
			// ROR: Rotate right. The byte that falls off the edge goes into carry, and the carry bit goes in the other side.
			// Accumulator ROR. Hex: $6A  Len: 1  Time: 2
			// Affects flags S, Z, and C.
			case 0x6A:
			{
				accumulator = rotate_right(accumulator);
				cycles = 2;
				break;
			}
			// Zero page ROR. Hex: $66  Len: 2  Time: 5
			// Affects flags S, Z, and C.
			case 0x66:
			{
				unsigned int address = zero_page_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = rotate_right(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter++;
				cycles = 5;
				break;
			}
			// Zero page,X ROR. Hex: $76  Len: 2  Time: 6
			// Affects flags S, Z, and C.
			case 0x76:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = rotate_right(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter++;
				cycles = 6;
				break;
			}
			// Absolute ROR. Hex: $6E  Len: 3  Time: 6
			// Affects flags S, Z, and C.
			case 0x6E:
			{
				unsigned int address = absolute_address();
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = rotate_right(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter += 2;
				cycles = 6;
				break;
			}
			// Absolute,X ROR. Hex: $7E  Len: 3  Time: 7
			// Affects flags S, Z, and C.
			case 0x7E:
			{
				unsigned int address = absolute_address() + x_register;
				unsigned char target;
				access_cpu_memory(&target, address, READ);
				target = rotate_right(target);
				access_cpu_memory(&target, address, WRITE);
				program_counter += 2;
				cycles = 7;
				break;
			}
			// CMP: Compares the accumulator to a value, and sets the sign, carry, and zero flags accordingly.
			// Immediate CMP. Hex: $C9  Len: 2  Time: 2
			// Affects flags S, Z, and C.
			case 0xC9:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(accumulator, load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page CMP. Hex: $C5  Len: 2  Time: 3
			// Affects flags S, Z, and C.
			case 0xC5:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(accumulator, load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,X CMP. Hex: $D5  Len: 2  Time: 4
			// Affects flags S, Z, and C.
			case 0xD5:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(accumulator, load_byte);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute CMP. Hex: $CD  Len: 3  Time: 4
			// Affects flags S, Z, and C.
			case 0xCD:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(accumulator, load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,X CMP. Hex: $DD  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S, Z, and C.
			case 0xDD:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(x_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(accumulator, load_byte);
				program_counter += 2;
				break;
			}
			// Absolute,Y CMP. Hex: $D9  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S, Z, and C.
			case 0xD9:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(y_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(accumulator, load_byte);
				program_counter += 2;
				break;
			}
			// Indirect,X CMP. Hex: $C1  Len: 2  Time: 6
			// Affects flags S, Z, and C.
			case 0xC1:
			{
				unsigned int address = preindexed_indirect_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(accumulator, load_byte);
				program_counter++;
				cycles = 6;
				break;
			}
			// Indirect,Y CMP. Hex: $D1  Len: 2  Time: 5 + 1 [if crossed page boundary]
			// Affects flags S, Z, and C.
			case 0xD1:
			{
				cycles = 5;
				unsigned int address = postindexed_indirect_address(&cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(accumulator, load_byte);
				program_counter++;
				break;
			}
			// CPX: Compares the X register to a value, and sets the sign, carry, and zero flags accordingly.
			// Immediate CPX. Hex: $E0  Len: 2  Time: 2
			// Affects flags S, Z, and C.
			case 0xE0:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(x_register, load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page CPX. Hex: $E4  Len: 2  Time: 3
			// Affects flags S, Z, and C.
			case 0xE4:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(x_register, load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Absolute CPX. Hex: $EC  Len: 3  Time: 4
			// Affects flags S, Z, and C.
			case 0xEC:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(x_register, load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// CPY: Compares the Y register to a value, and sets the sign, carry, and zero flags accordingly.
			// Immediate CPY. Hex: $C0  Len: 2  Time: 2
			// Affects flags S, Z, and C.
			case 0xC0:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(y_register, load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page CPY. Hex: $C4  Len: 2  Time: 3
			// Affects flags S, Z, and C.
			case 0xC4:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(y_register, load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Absolute CPY. Hex: $CC  Len: 3  Time: 4
			// Affects flags S, Z, and C.
			case 0xCC:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				compare_register(y_register, load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// BIT: Sets the zero flag to the result of ANDing a memory byte with accumulator, transfers bit 7 and 6 of the memory byte into flags S and V.
			// Zero page BIT. Hex: $24  Len: 2  Time: 3
			// Affects flags N, V, and Z.
			case 0x24:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				test_zero_flag(load_byte & accumulator);
				status_flags = (status_flags & 0b00111111) | (load_byte & 0b11000000);
				program_counter++;
				cycles = 3;
				break;
			}
			// Absolute BIT. Hex: $2C  Len: 3  Time: 4
			// Affects flags N, V, and Z.
			case 0x2C:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				test_zero_flag(load_byte & accumulator);
				status_flags = (status_flags & 0b00111111) | (load_byte & 0b11000000);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,X LDA. Hex: $BD  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0xBD:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(x_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&accumulator, load_byte);
				program_counter += 2;
				break;
			}
			// Absolute,Y LDA. Hex: $B9  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0xB9:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(y_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&accumulator, load_byte);
				program_counter += 2;
				break;
			}
			// Indirect,X LDA. Hex: $A1  Len: 2  Time: 6
			case 0xA1:
			{
				unsigned int address = preindexed_indirect_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&accumulator, load_byte);
				program_counter++;
				cycles = 6;
				break;
			}
			// Indirect,Y LDA. Hex: $B1  Len: 2  Time: 5 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0xB1:
			{
				cycles = 5;
				unsigned int address = postindexed_indirect_address(&cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&accumulator, load_byte);
				program_counter++;
				break;
			}
			// Unofficial opcode LAX.
			// Immediate LAX. Hex: $AB  Len: 2  Time: Unknown, guessing 2?
			case 0xAB:
			{
				// TODO
				program_counter++;
				cycles = 2;
				break;
			}
			// LDX: Loads a byte into the X register.
			// Immediate LDX. Hex: $A2  Len: 2  Time: 2
			// Affects flags S and Z.
			case 0xA2:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&x_register, load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page LDX. Hex $A6  Len: 2  Time: 3
			// Affects flags S and Z.
			case 0xA6:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&x_register, load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,Y LDX. Hex: $B6  Len: 2  Time: 4
			// Affects flags S and Z.
			case 0xB6:
			{
				unsigned int address = zero_page_indexed_address(y_register);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&x_register, load_byte);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute LDX. Hex $AE  Len: 3  Time: 4
			// Affects flags S and Z.
			case 0xAE:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&x_register, load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,Y LDX. Hex: $BE  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0xBE:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(y_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&x_register, load_byte);
				program_counter += 2;
				break;
			}
			// LDY: Loads a byte into the Y register.
			// Immediate LDY. Hex: $A0  Len: 2  Time: 2
			// Affects flags S and Z.
			case 0xA0:
			{
				unsigned int address = immediate_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&y_register, load_byte);
				program_counter++;
				cycles = 2;
				break;
			}
			// Zero page LDY. Hex: $A4  Len: 2  Time: 3
			// Affects flags S and Z.
			case 0xA4:
			{
				unsigned int address = zero_page_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&y_register, load_byte);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,X LDY. Hex: $B4  Len: 2  Time: 4
			// Affects flags S and Z.
			case 0xB4:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&y_register, load_byte);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute LDY. Hex: $AC  Len: 3  Time: 4
			// Affects flags S and Z.
			case 0xAC:
			{
				unsigned int address = absolute_address();
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&y_register, load_byte);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,X LDY. Hex: $BC  Len: 3  Time: 4 + 1 [if crossed page boundary]
			// Affects flags S and Z.
			case 0xBC:
			{
				cycles = 4;
				unsigned int address = absolute_indexed_address(x_register, &cycles);
				unsigned char load_byte;
				access_cpu_memory(&load_byte, address, READ);
				load_register(&y_register, load_byte);
				program_counter += 2;
				break;
			}
			// STA: Stores the accumulator in the address specified by the operand.
			// Zero page STA. Hex: $85  Len: 2  Time: 3
			case 0x85:
			{
				unsigned int address = zero_page_address();
				unsigned char data = accumulator;
				access_cpu_memory(&data, address, WRITE);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,X STA. Hex: $95  Len: 2  Time: 4
			case 0x95:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char data = accumulator;
				access_cpu_memory(&data, address, WRITE);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute STA. Hex: $8D  Len: 3  Time: 4
			case 0x8D:
			{
				unsigned int address = absolute_address();
				unsigned char data = accumulator;
				access_cpu_memory(&data, address, WRITE);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// Absolute,X STA. Hex: $9D  Len: 3  Time: 5
			case 0x9D:
			{
				unsigned int address = absolute_address() + x_register;
				unsigned char data = accumulator;
				access_cpu_memory(&data, address, WRITE);
				program_counter += 2;
				cycles = 5;
				break;
			}
			// Absolute,Y STA. Hex: $99  Len: 3  Time: 5
			case 0x99:
			{
				unsigned int address = absolute_address() + y_register;
				unsigned char data = accumulator;
				access_cpu_memory(&data, address, WRITE);
				program_counter += 2;
				cycles = 5;
				break;
			}
			// Indirect,X STA. Hex: $81  Len: 2  Time: 6
			case 0x81:
			{
				unsigned int address = preindexed_indirect_address();
				unsigned char data = accumulator;
				access_cpu_memory(&data, address, WRITE);
				program_counter++;
				cycles = 6;
				break;
			}
			// Indirect,Y STA. Hex: $91  Len: 2  Time: 6
			case 0x91:
			{
				unsigned int address = zero_page_indirect_address() + y_register;
				unsigned char data = accumulator;
				access_cpu_memory(&data, address, WRITE);
				program_counter++;
				cycles = 6;
				break;
			}
			// STX: Stores the X register in the address specified by the operand.
			// Zero Page STX. Hex: $86  Len: 2  Time: 3
			case 0x86:
			{
				unsigned address = zero_page_address();
				unsigned char data = x_register;
				access_cpu_memory(&data, address, WRITE);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,Y STX. Hex: $96  Len: 2  Time: 4
			case 0x96:
			{
				unsigned address = zero_page_indexed_address(y_register);
				unsigned char data = x_register;
				access_cpu_memory(&data, address, WRITE);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute STX. Hex: $8E  Len: 3  Time: 4
			case 0x8E:
			{
				unsigned int address = absolute_address();
				unsigned char data = x_register;
				access_cpu_memory(&data, address, WRITE);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// STY: Stores the Y register in the address specified by the operand.
			// Zero page STY. Hex: $84  Len: 2  Time: 3
			case 0x84:
			{
				unsigned int address = zero_page_address();
				unsigned char data = y_register;
				access_cpu_memory(&data, address, WRITE);
				program_counter++;
				cycles = 3;
				break;
			}
			// Zero page,X STY. Hex: $94  Len: 2  Time: 4
			case 0x94:
			{
				unsigned int address = zero_page_indexed_address(x_register);
				unsigned char data = y_register;
				access_cpu_memory(&data, address, WRITE);
				program_counter++;
				cycles = 4;
				break;
			}
			// Absolute STY. Hex: $8C  Len: 3  Time: 4
			case 0x8C:
			{
				unsigned int address = absolute_address();
				unsigned char data = y_register;
				access_cpu_memory(&data, address, WRITE);
				program_counter += 2;
				cycles = 4;
				break;
			}
			// TXS: Loads the value of the x register into the stack pointer.
			// Implied TXS. Hex: $9A  Len: 1  Time: 2
			case 0x9A:
			{
				stack_pointer = x_register;
				cycles = 2;
				break;
			}
			// TSX: Loads the value of the stack pointer into the x register.
			// Implied TSX. Hex: $BA  Len: 1  Time: 2
			// Affects flags S and Z.
			case 0xBA:
			{
				x_register = stack_pointer;
				test_negative_flag(x_register);
				test_zero_flag(x_register);
				cycles = 2;
				break;
			}
			// PHA: Push accumulator to the stack.
			// Implied PHA. Hex: $48  Len: 1  Time: 3
			case 0x48:
			{
				push_to_stack(accumulator);
				cycles = 3;
				break;
			}
			// PLA: Pull accumulator from the stack.
			// Implied PLA. Hex: $68  Len: 1  Time: 3
			// Affects flags S and Z.
			case 0x68:
			{
				accumulator = pull_from_stack();
				test_zero_flag(accumulator);
				test_negative_flag(accumulator);
				cycles = 4;
				break;
			}
			// PHP: Push status flags to the stack.
			// Implied PHP. Hex: $08  Len: 1  Time: 3
			case 0x08:
			{
				push_to_stack(status_flags | 0b00110000);
				cycles = 3;
				break;
			}
			// PLP: Pull status flags from the stack.
			// Implied PLP. Hex: $28  Len: 1  Time: 4
			case 0x28:
			{
				status_flags = pull_from_stack();
				cycles = 4;
				break;
			}
			// BRK: Triggers an interrupt.
			// Implied BRK. Hex: $00  Len: 1  Time: 7
			case 0x00:
			{
				program_counter++;
				push_to_stack(program_counter / 0x100);
				push_to_stack(program_counter % 0x100);
				push_to_stack((status_flags | 0b00100000) & 0b11101111);
				unsigned char low_address_byte;
				access_cpu_memory(&low_address_byte, 0xFFFE, READ);
				unsigned char high_address_byte;
				access_cpu_memory(&high_address_byte, 0xFFFF, READ);
				program_counter = low_address_byte + (high_address_byte * 0x100);
				cycles = 7;
				break;
			}
			default:
			{
				cpu_tick();
				cycles++;
				break;
			}
		}
	}
	
	if (pending_nmi && ((timing_cycle & 0b0000100) == 0b0000100))
	{
		push_to_stack(program_counter / 0x100);
		push_to_stack(program_counter % 0x100);
		push_to_stack((status_flags | 0b00100000) & 0b11101111);
		unsigned char low_address_byte;
		access_cpu_memory(&low_address_byte, 0xFFFA, READ);
		unsigned char high_address_byte;
		access_cpu_memory(&high_address_byte, 0xFFFB, READ);
		program_counter = low_address_byte + (high_address_byte * 0x100);
		cycles += 7;
		pending_nmi = 0;
	}
	
	// Writes 256 bytes to the OAM, filling it up.
	if (oam_dma_active && ((timing_cycle & 0b0000100) == 0b0000100))
	{
		for (int i = 0; i < 0x100; i++)
		{
			unsigned char load_byte;
			access_cpu_memory(&load_byte, (oam_dma_page * 0x100) | i, READ);
			access_cpu_memory(&load_byte, 0x2004, WRITE);
		}
		// OAM DMA takes 513 cycles, +1 on odd cycles. I don't have any detection for odd cycles right now, so I won't worry about that.
		cycles += 513;
		oam_dma_active = 0;
	}
	
	if ((timing_cycle & 0b0000100) == 0b0000100)
	{
		// Bit of a cheat to make sure it's ready for T2.
		access_cpu_memory(&predecode_bus, program_counter, READ);
		execute_bus = predecode_bus;
		program_counter++;
		address_bus = program_counter;
	}
	
	total_cycles += cycles;
	
	return cycles;
}

void reset_cpu()
{
	// JMP to the address at $FFFC.
	program_counter = 0xFFFC;
	program_counter = absolute_address();
}

void cpu_init()
{
	cpu_ram = malloc(sizeof(char) * RAM_SIZE);
	for (unsigned int i = 0; i < RAM_SIZE; i++)
	{
		cpu_ram[i] = 0;
	}
	
	// Two lists of decode lines, containing all the information needed to
	// determine when it fires. The first list is for half cycle 1, the second
	// list is for half cycle 2.
	// I'm putting the rom op at the start of the struct init, so it
	// should be clear enough what each one is for.
	decode_lines_first_half = malloc(sizeof(struct DecodeLine) * FIRST_HALF_DECODE_LINES);
	decode_lines_second_half = malloc(sizeof(struct DecodeLine) * SECOND_HALF_DECODE_LINES);
	for (unsigned int i = 0; i < FIRST_HALF_DECODE_LINES; i++)
	{
		decode_lines_first_half[i] = (struct DecodeLine) { .opcode_bits = 0b00000000, .opcode_mask = 0b00000000, .timing = 0b000000, .push_pull_negate = 0, .rom_op = do_nothing };
	}
	for (unsigned int i = 0; i < SECOND_HALF_DECODE_LINES; i++)
	{
		decode_lines_second_half[i] = (struct DecodeLine) { .opcode_bits = 0b00000000, .opcode_mask = 0b00000000, .timing = 0b000000, .push_pull_negate = 0, .rom_op = do_nothing };
	}
	
	decode_lines_first_half[0] = (struct DecodeLine) { .rom_op = op_T3_mem_zp_idx, .opcode_bits = 0b00010100, .opcode_mask = 0b00011100, .timing = 0b001000, .push_pull_negate = 0 };
	decode_lines_first_half[1] = (struct DecodeLine) { .rom_op = op_T2_abs, .opcode_bits = 0b00001100, .opcode_mask = 0b00011100, .timing = 0b000100, .push_pull_negate = 0 };
	decode_lines_first_half[2] = (struct DecodeLine) { .rom_op = op_T3_mem_abs, .opcode_bits = 0b00001100, .opcode_mask = 0b00011100, .timing = 0b001000, .push_pull_negate = 0 };
	
	// op-push/pull
	// TODO: fill in the function for this
	decode_lines_second_half[0] = (struct DecodeLine) { .rom_op = do_nothing, .opcode_bits = 0b00001000, .opcode_mask = 0b10011111, .timing = 0, .push_pull_negate = 0 };
	decode_lines_second_half[1] = (struct DecodeLine) { .rom_op = op_T0, .opcode_bits = 0b00000000, .opcode_mask = 0b00000000, .timing = 0b000001, .push_pull_negate = 0 };
	decode_lines_second_half[2] = (struct DecodeLine) { .rom_op = op_clv, .opcode_bits = 0b10111000, .opcode_mask = 0b11111111, .timing = 0, .push_pull_negate = 0 };
	decode_lines_second_half[3] = (struct DecodeLine) { .rom_op = op_T0_clc_sec, .opcode_bits = 0b00011000, .opcode_mask = 0b11011111, .timing = 0b000001, .push_pull_negate = 0 };
	decode_lines_second_half[4] = (struct DecodeLine) { .rom_op = op_T0_cli_sei, .opcode_bits = 0b01011000, .opcode_mask = 0b11011111, .timing = 0b000001, .push_pull_negate = 0 };
	decode_lines_second_half[5] = (struct DecodeLine) { .rom_op = op_T0_cld_sed, .opcode_bits = 0b11011000, .opcode_mask = 0b11011111, .timing = 0b000001, .push_pull_negate = 0 };
	decode_lines_second_half[6] = (struct DecodeLine) { .rom_op = op_T0_lda, .opcode_bits = 0b10100001, .opcode_mask = 0b11100001, .timing = 0b000001, .push_pull_negate = 0 };
	decode_lines_second_half[7] = (struct DecodeLine) { .rom_op = op_T2_acc, .opcode_bits = 0b00000001, .opcode_mask = 0b00000001, .timing = 0b000100, .push_pull_negate = 0 };
	decode_lines_second_half[8] = (struct DecodeLine) { .rom_op = op_T2_mem_zp, .opcode_bits = 0b00000100, .opcode_mask = 0b00011100, .timing = 0b000100, .push_pull_negate = 0 };
	decode_lines_second_half[9] = (struct DecodeLine) { .rom_op = op_T2_ADL_ADD, .opcode_bits = 0b00000000, .opcode_mask = 0b00001000, .timing = 0b000100, .push_pull_negate = 0 };
	
	reset_cpu();
}