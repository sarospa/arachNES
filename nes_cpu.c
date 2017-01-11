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
unsigned const int FIRST_HALF_DECODE_LINES = 24;
unsigned const int SECOND_HALF_DECODE_LINES = 79;
unsigned const char NMI = 0;
unsigned const char IRQ = 1;

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
unsigned char oam_dma_write = 0;
unsigned char oam_dma_low = 0;

unsigned char timing_cycle = 0b000010;
unsigned char next_timing_cycle = 0;
unsigned int address_bus = 0;
unsigned char address_low_bus = 0;
unsigned char address_high_bus = 0;
unsigned char data_bus = 0;
unsigned char execute_bus = 0;
// The two inputs and one output for the ALU. Most of our calculations
// can be done directly, but certain internal calculations use these registers.
unsigned char alu_in_a = 0;
unsigned char alu_in_b = 0;
unsigned char alu_out = 0;
// Controls the CPU's reads and writes to memory. 1 == read, 0 == write.
unsigned char read_write = 1;

unsigned char pending_interrupt = 0;
unsigned char interrupt_cycle = 0;
unsigned char interrupt_type = 0;

// Represents lines for ops that do something in the first half of the cycle.
struct DecodeLine* decode_lines_first_half;
// Represents lines for ops that do something in the second half of the cycle.
struct DecodeLine* decode_lines_second_half;

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
	fwrite(&oam_dma_write, sizeof(char), 1, save_file);
	fwrite(&oam_dma_low, sizeof(char), 1, save_file);
	fwrite(&total_cycles, sizeof(int), 1, save_file);
	
	fwrite(&timing_cycle, sizeof(char), 1, save_file);
	fwrite(&next_timing_cycle, sizeof(char), 1, save_file);
	fwrite(&address_bus, sizeof(int), 1, save_file);
	fwrite(&address_low_bus, sizeof(char), 1, save_file);
	fwrite(&address_high_bus, sizeof(char), 1, save_file);
	fwrite(&data_bus, sizeof(char), 1, save_file);
	fwrite(&execute_bus, sizeof(char), 1, save_file);
	fwrite(&alu_in_a, sizeof(char), 1, save_file);
	fwrite(&alu_in_b, sizeof(char), 1, save_file);
	fwrite(&alu_out, sizeof(char), 1, save_file);
	fwrite(&read_write, sizeof(char), 1, save_file);
	
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
	fread(&oam_dma_write, sizeof(char), 1, save_file);
	fread(&oam_dma_low, sizeof(char), 1, save_file);
	fread(&total_cycles, sizeof(int), 1, save_file);
	
	fread(&timing_cycle, sizeof(char), 1, save_file);
	fread(&next_timing_cycle, sizeof(char), 1, save_file);
	fread(&address_bus, sizeof(int), 1, save_file);
	fread(&address_low_bus, sizeof(char), 1, save_file);
	fread(&address_high_bus, sizeof(char), 1, save_file);
	fread(&data_bus, sizeof(char), 1, save_file);
	fread(&execute_bus, sizeof(char), 1, save_file);
	fread(&alu_in_a, sizeof(char), 1, save_file);
	fread(&alu_in_b, sizeof(char), 1, save_file);
	fread(&alu_out, sizeof(char), 1, save_file);
	fread(&read_write, sizeof(char), 1, save_file);
	
	fread(cpu_ram, sizeof(char), RAM_SIZE, save_file);
}

// Should indicate true for ASL, ROL, LSR, ROR, INC, and DEX.
unsigned char is_rmw_instruction()
{
	unsigned char group = execute_bus & 0b00000011;
	unsigned char function_bits = (execute_bus & 0b11000000) >> 6;
	return (group == 0b10) && ((function_bits == 0b00) || (function_bits == 0b01) || (function_bits == 0b11));
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

void op_T0_ldx()
{
	load_register(&x_register, data_bus);
}

void op_T0_ldy()
{
	load_register(&y_register, data_bus);
}

void op_T0_and()
{
	bitwise_and(data_bus);
}

void op_T0_ora()
{
	bitwise_or(data_bus);
}

void op_T0_eor()
{
	bitwise_xor(data_bus);
}

void op_T0_adc()
{
	add_with_carry(data_bus);
}

void op_T0_sbc()
{
	subtract_with_carry(data_bus);
}

void op_T0_cmp()
{
	compare_register(accumulator, data_bus);
}

void op_T0_cpx()
{
	compare_register(x_register, data_bus);
}

void op_T0_cpy()
{
	compare_register(y_register, data_bus);
}

void op_T0_sta()
{
	// Bit of a hack to keep it from trying to do a write on immediate,
	// thus turning it into a NOP #i as it's supposed to be.
	if (timing_cycle == 0b000001)
	{
		data_bus = accumulator;
	}
}

void op_T0_stx()
{
	data_bus = x_register;
}

void op_T0_sty()
{
	data_bus = y_register;
}

void op_T0_inc()
{
	data_bus++;
	test_negative_flag(data_bus);
	test_zero_flag(data_bus);
}

void op_T0_dec()
{
	data_bus--;
	test_negative_flag(data_bus);
	test_zero_flag(data_bus);
}

void op_T0_asl()
{
	data_bus = arithmetic_shift_left(data_bus);
}

void op_T0_lsr()
{
	data_bus = logical_shift_right(data_bus);
}

void op_T0_rol()
{
	data_bus = rotate_left(data_bus);
}

void op_T0_ror()
{
	data_bus = rotate_right(data_bus);
}

// Bit of a hacky solution, although I believe it roughly imitates the CPU.
// Sets read/write mode to write on:
// T2 in zero page mode
// T3 in zero page,X and absolute mode
// T4 in absolute,X and absolute,Y mode
// T5 in indirect,X and indirect,Y mode
void op_sta()
{
	char addressing_mode = (execute_bus & 0b00011100) >> 2;
	if (((addressing_mode == 0b001) && (timing_cycle == 0b000100))
		|| (((addressing_mode == 0b101) || (addressing_mode == 0b011)) && (timing_cycle == 0b001000))
		|| (((addressing_mode == 0b111) || (addressing_mode == 0b110)) && (timing_cycle == 0b010000))
		|| (((addressing_mode == 0b100) || (addressing_mode == 0b000)) && (timing_cycle == 0b100000)))
	{
		read_write = 0;
	}
}

void op_rmw()
{
	if (read_write == 0)
	{
		next_timing_cycle = 0b000001;
	}
	
	char addressing_mode = (execute_bus & 0b00011100) >> 2;
	if (((addressing_mode == 0b001) && (timing_cycle == 0b001000))
		|| (((addressing_mode == 0b101) || (addressing_mode == 0b011)) && (timing_cycle == 0b010000))
		|| ((addressing_mode == 0b111) && (timing_cycle == 0b100000)))
	{
		read_write = 0;
	}
}

void op_lsr_ror_dec_inc()
{
	op_rmw();
}

void op_asl_rol()
{
	op_rmw();
}

// On T0, fetch next instruction, load program counter into ADL/ADH, and
// make sure the next cycle is T1 (to override T0+T2 rolling into T1+T3).
void op_T0()
{
	next_timing_cycle = 0b000010;
	address_low_bus = program_counter & 0x00FF;
	address_high_bus = (program_counter >> 8) & 0x00FF;
	read_write = 1;
}

void op_T1_asl_acc()
{
	accumulator = arithmetic_shift_left(accumulator);
}

void op_T1_lsr_acc()
{
	accumulator = logical_shift_right(accumulator);
}

void op_T1_rol_acc()
{
	accumulator = rotate_left(accumulator);
}

void op_T1_ror_acc()
{
	accumulator = rotate_right(accumulator);
}

// Artificial line. It looks like ALU instructions always increment the
// program counter on T2.
void op_T2_acc()
{
	program_counter++;
}

void op_T2_zp_abs()
{
	program_counter++;
}

// Need to read the next byte for address high, but address low needs
// to be stuffed somewhere to use later.
void op_T2_abs()
{
	address_low_bus = (program_counter + 1) & 0x00FF;
	address_high_bus = ((program_counter + 1) >> 8) & 0x00FF;
	alu_in_b = data_bus;
}

// Artificial line. Stores x in alua and the low address byte in
// alub to be added together next cycle.
void op_T2_abs_x()
{
	address_low_bus = (program_counter + 1) & 0x00FF;
	address_high_bus = ((program_counter + 1) >> 8) & 0x00FF;
	// LDX and STX need to use Y for indexing instead.
	if ((execute_bus & 0b11011110) == 0b10011110)
	{
		alu_in_a = y_register;
	}
	else
	{
		alu_in_a = x_register;
	}
	
	alu_in_b = data_bus;
}

// Stores y in alua and the low address byte in
// alub to be added together next cycle.
void op_T2_abs_y()
{
	address_low_bus = (program_counter + 1) & 0x00FF;
	address_high_bus = ((program_counter + 1) >> 8) & 0x00FF;
	alu_in_a = y_register;
	alu_in_b = data_bus;
}

// Zero page opcodes reset the timing cycle at T2.
void op_T2_mem_zp()
{
	address_low_bus = data_bus;
	address_high_bus = 0;
	if (!is_rmw_instruction())
	{
		next_timing_cycle = 0b000001;
	}
}

void op_T2_ind_y()
{
	address_low_bus = data_bus;
	address_high_bus = 0;
}

void op_T3_ind_y()
{
	address_low_bus++;
	alu_in_b = data_bus;
}

void op_T4_ind_y()
{
	address_low_bus = y_register + alu_in_b;
	address_high_bus = data_bus;
	// Add an extra cycle if the sum overflowed or in write instructions.
	if ((address_low_bus >= alu_in_b) && ((0b11100001 & execute_bus) != 0b10000001))
	{
		next_timing_cycle = 0b000001;
	}
}

void op_T5_ind_y()
{
	// Confirm that the sum overflowed, since the extra cycle also might be
	// for a write.
	if (address_low_bus < alu_in_b)
	{
		address_high_bus++;
	}
	next_timing_cycle = 0b000001;
}

void op_T2_ind_x()
{
	address_low_bus = data_bus;
	address_high_bus = 0;
	alu_in_b = data_bus;
	alu_in_a = x_register;
}

void op_T3_ind_x()
{
	address_low_bus = alu_in_a + alu_in_b;
}

void op_T4_ind_x()
{
	alu_in_b = data_bus;
	address_low_bus++;
}

void op_T5_ind_x()
{
	address_low_bus = alu_in_b;
	address_high_bus = data_bus;
	next_timing_cycle = 0b000001;
}

void op_T2_mem_zp_idx()
{
	address_low_bus = data_bus;
	address_high_bus = 0;
}

// Add X register to address low for indexed zero page. Timing cycle also
// resets here.
void op_T3_mem_zp_idx()
{
	// LDX and STX need to use Y for indexing instead.
	if ((execute_bus & 0b11011110) == 0b10010110)
	{
		address_low_bus += y_register;
	}
	else
	{
		address_low_bus += x_register;
	}
	if (!is_rmw_instruction())
	{
		next_timing_cycle = 0b000001;
	}
}

// Load ADL from where we stuffed it last cycle, and ADH from the data bus.
// Timing cycle also resets here.
void op_T3_mem_abs()
{
	address_low_bus = alu_in_b;
	address_high_bus = data_bus;
	// Continue to next cycle on RMW instructions or JMP indirect.
	if ((!is_rmw_instruction()) && (execute_bus != 0b01101100))
	{
		next_timing_cycle = 0b000001;
	}
	program_counter++;
}

// Sum the low address byte with X to get the indexed address. Check if
// we need to spend another cycle on page crossing.
void op_T3_abs_idx()
{
	address_low_bus = alu_in_a + alu_in_b;
	address_high_bus = data_bus;
	// Add an extra cycle if the sum overflowed or in STA instructions.
	if ((address_low_bus >= alu_in_b) && ((0b11100001 & execute_bus) != 0b10000001)
		&& (!is_rmw_instruction()))
	{
		next_timing_cycle = 0b000001;
	}
	program_counter++;
}

// Extra cycle for indexed absolute, for handling page crossing.
void op_T4_abs_idx()
{
	// Confirm that the sum overflowed, since the extra cycle also might be
	// for a write.
	if (address_low_bus < alu_in_b)
	{
		address_high_bus++;
	}
	if (!is_rmw_instruction())
	{
		next_timing_cycle = 0b000001;
	}
}

void op_T2_branch()
{
	address_low_bus = program_counter & 0x00FF;
	address_high_bus = (program_counter >> 8) & 0x00FF;
	
	unsigned char op_bit_7 = (execute_bus >> 7) & 0b1;
	unsigned char op_bit_6 = (execute_bus >> 6) & 0b1;
	unsigned char flag_compare = (execute_bus >> 5) & 0b1;
	// Determines which bit from the status flags to compare to.
	unsigned char flag_select = (0b111 ^ op_bit_6) ^ (0b111 * op_bit_7);
	unsigned char flag_bit = (status_flags >> flag_select) & 0b1;
	if (flag_compare != flag_bit)
	{
		next_timing_cycle = 0b000010;
	}
	program_counter++;
}

void op_T3_branch()
{
	alu_in_a = data_bus;
	alu_in_b = address_low_bus;
	alu_out = alu_in_a + alu_in_b;
	address_low_bus = alu_out;
	// Check overflow as a signed number. If it did overflow, need to add a cycle
	// for page crossing.
	if ((alu_out >= alu_in_a) == ((alu_in_a & 0b10000000) == 0b10000000))
	{
		next_timing_cycle = 0b00000001;
	}
	else
	{
		next_timing_cycle = 0b00000010;
	}
	program_counter = address_low_bus | (address_high_bus << 8);
	program_counter++;
}

void op_T0_branch()
{
	// Decrement adh if the branch amount was negative. Otherwise increment it.
	if ((alu_in_a & 0b10000000) == 0b10000000)
	{
		address_high_bus--;
	}
	else
	{
		address_high_bus++;
	}
	program_counter = address_low_bus | (address_high_bus << 8);
	program_counter++;
}

void op_T2_jmp_abs()
{
	next_timing_cycle = 0b000001;
	address_low_bus = (program_counter + 1) & 0x00FF;
	address_high_bus = ((program_counter + 1) >> 8) & 0x00FF;
}

void op_T0_jmp()
{
	address_low_bus = alu_in_b;
	address_high_bus = data_bus;
	address_bus = address_low_bus | (address_high_bus << 8);
	program_counter = address_bus;
}

void op_T3_jmp()
{
	address_low_bus = alu_in_b;
	address_high_bus = data_bus;
	address_bus = address_low_bus | (address_high_bus << 8);
}

void op_T4_jmp()
{
	alu_in_b = data_bus;
	address_low_bus++;
	next_timing_cycle = 0b000001;
}

void op_T0_bit()
{
	test_zero_flag(data_bus & accumulator);
	status_flags = (status_flags & 0b00111111) | (data_bus & 0b11000000);
}

void op_T2_imm()
{
	program_counter++;
}

void op_T2_php_pha()
{
	read_write = 0;
	next_timing_cycle = 0b000001;
	address_low_bus = stack_pointer;
	address_high_bus = 0x01;
}

void op_T0_pha()
{
	data_bus = accumulator;
	stack_pointer--;
}

void op_T0_php()
{
	data_bus = (status_flags | 0b00110000);
	stack_pointer--;
}

void op_T2_stack_pull()
{
	address_low_bus = stack_pointer;
	address_high_bus = 0x01;
}

void op_T3_pla_pha()
{
	stack_pointer++;
	address_low_bus = stack_pointer;
	next_timing_cycle = 0b000001;
}

void op_T0_plp()
{
	status_flags = data_bus;
}

void op_T0_pla()
{
	accumulator = data_bus;
	test_zero_flag(accumulator);
	test_negative_flag(accumulator);
}

void op_T3_rti_rts()
{
	stack_pointer++;
	address_low_bus = stack_pointer;
}

void op_T4_rti_rts()
{
	alu_in_b = data_bus;
	stack_pointer++;
	address_low_bus = stack_pointer;
}

void op_T5_rts()
{
	address_low_bus = alu_in_b;
	address_high_bus = data_bus;
	address_bus = address_low_bus | (address_high_bus << 8);
	program_counter = address_bus;
	next_timing_cycle = 0b000001;
}

void op_T0_rts()
{
	program_counter++;
}

void op_T5_rti()
{
	status_flags = alu_in_b;
	alu_in_b = data_bus;
	stack_pointer++;
	address_low_bus = stack_pointer;
	next_timing_cycle = 0b000001;
}

void op_T0_rti()
{
	address_low_bus = alu_in_b;
	address_high_bus = data_bus;
	address_bus = address_low_bus | (address_high_bus << 8);
	program_counter = address_bus;
}

void op_T2_jsr()
{
	alu_in_b = stack_pointer;
	stack_pointer = data_bus;
	address_low_bus = alu_in_b;
	address_high_bus = 0x01;
	program_counter++;
}

void op_T3_jsr()
{
	read_write = 0;
}

void op_T4_jsr()
{
	data_bus = (program_counter >> 8) & 0xFF;
	address_low_bus--;
}

void op_T5_jsr_first_half()
{
	data_bus = program_counter & 0xFF;
	alu_in_a = address_bus & 0xFF;
	alu_in_a--;
	next_timing_cycle = 0b000001;
}

void op_T5_jsr_second_half()
{
	address_low_bus = program_counter & 0xFF;;
	address_high_bus = (program_counter >> 8) & 0xFF;;
	address_bus = address_low_bus | (address_high_bus << 8);
	read_write = 1;
}

void op_T0_jsr()
{
	program_counter = (data_bus << 8) | stack_pointer;
	stack_pointer = alu_in_a;
}

void op_T0_txa()
{
	load_register(&accumulator, x_register);
}

void op_T0_tax()
{
	load_register(&x_register, accumulator);
}

void op_T0_txs()
{
	stack_pointer = x_register;
}

void op_T0_tsx()
{
	x_register = stack_pointer;
	test_negative_flag(x_register);
	test_zero_flag(x_register);
}

void op_T0_dex()
{
	x_register--;
	test_negative_flag(x_register);
	test_zero_flag(x_register);
}

void op_T0_inx()
{
	x_register++;
	test_negative_flag(x_register);
	test_zero_flag(x_register);
}

void op_T0_tya()
{
	load_register(&accumulator, y_register);
}

void op_T0_tay()
{
	load_register(&y_register, accumulator);
}

void op_T0_dey()
{
	y_register--;
	test_negative_flag(y_register);
	test_zero_flag(y_register);
}

void op_T0_iny()
{
	y_register++;
	test_negative_flag(y_register);
	test_zero_flag(y_register);
}

void op_brk_stack()
{
	address_low_bus = stack_pointer;
	address_high_bus = 0x01;
	stack_pointer--;
	read_write = 0;
}

void op_T3_brk()
{
	data_bus = ((program_counter + 1) >> 8) & 0xFF;
}

void op_T4_brk()
{
	data_bus = (program_counter + 1) & 0xFF;
}

void op_T5_brk()
{
	data_bus = (status_flags | 0b00110000);
	address_low_bus = 0xFE;
	address_high_bus = 0xFF;
	read_write = 1;
	status_flags = status_flags | 0b00000100;
}

void op_brk()
{
	if (timing_cycle == 0)
	{
		address_low_bus = 0xFF;
		address_high_bus = 0xFF;
		alu_in_b = data_bus;
		next_timing_cycle = 0b000001;
	}
}

void op_T0_brk()
{
	address_low_bus = alu_in_b;
	address_high_bus = data_bus;
	program_counter = address_low_bus | (address_high_bus << 8);
}

// Two-cycle instructions have an odd behavior where T0 and T2 run at the
// same time. The CPU has a special check for this that covers all two-cycle
// instructions, which is reproduced here.
void activate_T0_for_two_cycle_op()
{
	if (((execute_bus & 0b00011101) == 0b00001001)
		|| ((execute_bus & 0b10011101) == 0b10000000)
		|| (((execute_bus & 0b00001101) == 0b00001000) && !((execute_bus & 0b10010010) == 0b00000000)))
	{
		timing_cycle = timing_cycle | 0b000001;
	}
}

// Carries out the operations of the current opcode for this cycle.
void execute_opcode()
{
	if ((timing_cycle & 0b0000100) == 0b0000100)
	{
		access_cpu_memory(&execute_bus, program_counter, READ);
		program_counter++;
		address_bus = program_counter;
	}
	
	unsigned char prev_read_write = read_write;
	
	if (prev_read_write == 1)
	{
		access_cpu_memory(&data_bus, address_bus, READ);
	}
	
	//printf("%04X: OP:%02X A:%02X X:%02X Y:%02X D:%02X ADL:%02X ADH:%02X ADB:%04X RW:%02X T:%02X CYC:%d\n", program_counter - 1, execute_bus, accumulator, x_register, y_register, data_bus, address_low_bus, address_high_bus, address_bus, prev_read_write, timing_cycle, total_cycles);
	
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
			decode_lines_first_half[i].rom_op();
		}
	}
	
	if (prev_read_write == 0)
	{
		access_cpu_memory(&data_bus, address_bus, WRITE);
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
			decode_lines_second_half[i].rom_op();
		}
	}
	
	address_bus = address_low_bus | (address_high_bus << 8);
	
	timing_cycle = next_timing_cycle;
	
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
	// If an interrupt is pending, the CPU is set to react to it (IRQ enabled, or it's an NMI) and we aren't in the middle of
	// an interrupt already, begin the interrupt process.
	if (pending_interrupt && ((timing_cycle & 0b0000010) == 0b0000010) && ((interrupt_type == NMI) || ((status_flags &0b00000100) == 0)))
	{
		if (interrupt_cycle == 0)
		{
			interrupt_cycle = 1;
			if (interrupt_type == NMI)
			{
				pending_interrupt--;
			}
		}
	}
	
	if (oam_dma_active && ((timing_cycle & 0b0000100) == 0b0000100))
	{
		if (oam_dma_write)
		{
			access_cpu_memory(&data_bus, 0x2004, WRITE);
			if (oam_dma_low == 0)
			{
				oam_dma_active = 0;
			}
		}
		else
		{
			address_bus = oam_dma_low | (oam_dma_page << 8);
			access_cpu_memory(&data_bus, address_bus, READ);
			oam_dma_low++;
		}
		oam_dma_write = !oam_dma_write;
	}
	// A lot of this is covered by the existing ops, but I think for this,
	// it would be easier to simply keep it self-contained.
	else if (interrupt_cycle > 0)
	{
		switch (interrupt_cycle)
		{
			case 0b0000001:
			{
				address_low_bus = stack_pointer;
				address_high_bus = 0x01;
				break;
			}
			case 0b0000010:
			{
				data_bus = (program_counter >> 8) & 0xFF;
				access_cpu_memory(&data_bus, address_bus, WRITE);
				stack_pointer--;
				address_low_bus = stack_pointer;
				break;
			}
			case 0b0000100:
			{
				data_bus = program_counter & 0xFF;
				access_cpu_memory(&data_bus, address_bus, WRITE);
				stack_pointer--;
				address_low_bus = stack_pointer;
				break;
			}
			case 0b0001000:
			{
				data_bus = status_flags | 0b00100000;
				access_cpu_memory(&status_flags, address_bus, WRITE);
				stack_pointer--;
				break;
			}
			case 0b0010000:
			{
				if (interrupt_type == NMI)
				{
					address_low_bus = 0xFA;
				}
				else if (interrupt_type == IRQ)
				{
					address_low_bus = 0xFE;
				}
				address_high_bus = 0xFF;
				status_flags = status_flags | 0b00000100;
				break;
			}
			case 0b0100000:
			{
				access_cpu_memory(&alu_in_b, address_bus, READ);
				if (interrupt_type == NMI)
				{
					address_low_bus = 0xFB;
				}
				else if (interrupt_type == IRQ)
				{
					address_low_bus = 0xFF;
				}
				break;
			}
			case 0b1000000:
			{ 
				access_cpu_memory(&data_bus, address_bus, READ);
				program_counter = alu_in_b | (data_bus << 8);
				break;
			}
		}
		address_bus = address_low_bus | (address_high_bus << 8);
		interrupt_cycle = (interrupt_cycle << 1) & 0b1111111;
	}
	else
	{
		execute_opcode();
	}
	
	total_cycles++;
}

void reset_cpu()
{
	// JMP to the address at $FFFC.
	unsigned char low_byte;
	access_cpu_memory(&low_byte, 0xFFFC, READ);
	unsigned char high_byte;
	access_cpu_memory(&high_byte, 0xFFFD, READ);
	program_counter = low_byte | (high_byte << 8);
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

	decode_lines_first_half[0] = (struct DecodeLine) { .rom_op = op_T3_mem_zp_idx, .opcode_bits = 0b00010100, .opcode_mask = 0b00011100, .timing = 0b001000};
	decode_lines_first_half[1] = (struct DecodeLine) { .rom_op = op_T2_abs, .opcode_bits = 0b00001100, .opcode_mask = 0b00011100, .timing = 0b000100};
	decode_lines_first_half[2] = (struct DecodeLine) { .rom_op = op_T2_abs_x, .opcode_bits = 0b00011100, .opcode_mask = 0b00011100, .timing = 0b000100};
	decode_lines_first_half[3] = (struct DecodeLine) { .rom_op = op_T2_abs_y, .opcode_bits = 0b00011001, .opcode_mask = 0b00011101, .timing = 0b000100};
	decode_lines_first_half[4] = (struct DecodeLine) { .rom_op = op_T3_mem_abs, .opcode_bits = 0b00001100, .opcode_mask = 0b00011100, .timing = 0b001000};
	decode_lines_first_half[5] = (struct DecodeLine) { .rom_op = op_T0_sta, .opcode_bits = 0b10000001, .opcode_mask = 0b11100001, .timing = 0b000001};
	decode_lines_first_half[6] = (struct DecodeLine) { .rom_op = op_T0_inc, .opcode_bits = 0b11100110, .opcode_mask = 0b11100110, .timing = 0b000001};
	decode_lines_first_half[7] = (struct DecodeLine) { .rom_op = op_T0_dec, .opcode_bits = 0b11000110, .opcode_mask = 0b11100110, .timing = 0b000001};
	decode_lines_first_half[8] = (struct DecodeLine) { .rom_op = op_T0_asl, .opcode_bits = 0b00000110, .opcode_mask = 0b11100110, .timing = 0b000001};
	decode_lines_first_half[9] = (struct DecodeLine) { .rom_op = op_T0_lsr, .opcode_bits = 0b01000110, .opcode_mask = 0b11100110, .timing = 0b000001};
	decode_lines_first_half[10] = (struct DecodeLine) { .rom_op = op_T0_rol, .opcode_bits = 0b00100110, .opcode_mask = 0b11100110, .timing = 0b000001};
	decode_lines_first_half[11] = (struct DecodeLine) { .rom_op = op_T0_ror, .opcode_bits = 0b01100110, .opcode_mask = 0b11100110, .timing = 0b000001};
	decode_lines_first_half[12] = (struct DecodeLine) { .rom_op = op_T2_branch, .opcode_bits = 0b00010000, .opcode_mask = 0b00011111, .timing = 0b000100};
	decode_lines_first_half[13] = (struct DecodeLine) { .rom_op = op_T3_branch, .opcode_bits = 0b00010000, .opcode_mask = 0b00011111, .timing = 0b001000};
	decode_lines_first_half[14] = (struct DecodeLine) { .rom_op = op_T0_branch, .opcode_bits = 0b00010000, .opcode_mask = 0b00011111, .timing = 0b000001};
	decode_lines_first_half[15] = (struct DecodeLine) { .rom_op = op_T0_stx, .opcode_bits = 0b10000110, .opcode_mask = 0b11100110, .timing = 0b000001};
	decode_lines_first_half[16] = (struct DecodeLine) { .rom_op = op_T0_sty, .opcode_bits = 0b10000100, .opcode_mask = 0b11100111, .timing = 0b000001};
	decode_lines_first_half[17] = (struct DecodeLine) { .rom_op = op_T0_pha, .opcode_bits = 0b01001000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_first_half[18] = (struct DecodeLine) { .rom_op = op_T0_php, .opcode_bits = 0b00001000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_first_half[19] = (struct DecodeLine) { .rom_op = op_T4_jsr, .opcode_bits = 0b00100000, .opcode_mask = 0b11111111, .timing = 0b010000};
	decode_lines_first_half[20] = (struct DecodeLine) { .rom_op = op_T5_jsr_first_half, .opcode_bits = 0b00100000, .opcode_mask = 0b11111111, .timing = 0b100000};
	decode_lines_first_half[21] = (struct DecodeLine) { .rom_op = op_T3_brk, .opcode_bits = 0b00000000, .opcode_mask = 0b11111111, .timing = 0b001000};
	decode_lines_first_half[22] = (struct DecodeLine) { .rom_op = op_T4_brk, .opcode_bits = 0b00000000, .opcode_mask = 0b11111111, .timing = 0b010000};
	decode_lines_first_half[23] = (struct DecodeLine) { .rom_op = op_T5_brk, .opcode_bits = 0b00000000, .opcode_mask = 0b11111111, .timing = 0b100000};
	
	decode_lines_second_half[0] = (struct DecodeLine) { .rom_op = op_T2_jmp_abs, .opcode_bits = 0b01001100, .opcode_mask = 0b11111111, .timing = 0b000100};
	decode_lines_second_half[1] = (struct DecodeLine) { .rom_op = op_T0, .opcode_bits = 0b00000000, .opcode_mask = 0b00000000, .timing = 0b000001};
	decode_lines_second_half[2] = (struct DecodeLine) { .rom_op = op_clv, .opcode_bits = 0b10111000, .opcode_mask = 0b11111111, .timing = 0b000000};
	decode_lines_second_half[3] = (struct DecodeLine) { .rom_op = op_T0_clc_sec, .opcode_bits = 0b00011000, .opcode_mask = 0b11011111, .timing = 0b000001};
	decode_lines_second_half[4] = (struct DecodeLine) { .rom_op = op_T0_cli_sei, .opcode_bits = 0b01011000, .opcode_mask = 0b11011111, .timing = 0b000001};
	decode_lines_second_half[5] = (struct DecodeLine) { .rom_op = op_T0_cld_sed, .opcode_bits = 0b11011000, .opcode_mask = 0b11011111, .timing = 0b000001};
	decode_lines_second_half[6] = (struct DecodeLine) { .rom_op = op_T0_lda, .opcode_bits = 0b10100001, .opcode_mask = 0b11100001, .timing = 0b000001};
	decode_lines_second_half[7] = (struct DecodeLine) { .rom_op = op_T2_acc, .opcode_bits = 0b00000001, .opcode_mask = 0b00000001, .timing = 0b000100};
	decode_lines_second_half[8] = (struct DecodeLine) { .rom_op = op_T2_mem_zp, .opcode_bits = 0b00000100, .opcode_mask = 0b00011100, .timing = 0b000100};
	decode_lines_second_half[9] = (struct DecodeLine) { .rom_op = op_T0_jmp, .opcode_bits = 0b01001100, .opcode_mask = 0b11011111, .timing = 0b000001};
	decode_lines_second_half[10] = (struct DecodeLine) { .rom_op = op_T3_abs_idx, .opcode_bits = 0b00011000, .opcode_mask = 0b00011000, .timing = 0b001000};
	decode_lines_second_half[11] = (struct DecodeLine) { .rom_op = op_T4_abs_idx, .opcode_bits = 0b00011000, .opcode_mask = 0b00011000, .timing = 0b010000};
	decode_lines_second_half[12] = (struct DecodeLine) { .rom_op = op_T2_ind_y, .opcode_bits = 0b00010001, .opcode_mask = 0b00011101, .timing = 0b000100};
	decode_lines_second_half[13] = (struct DecodeLine) { .rom_op = op_T3_ind_y, .opcode_bits = 0b00010001, .opcode_mask = 0b00011101, .timing = 0b001000};
	decode_lines_second_half[14] = (struct DecodeLine) { .rom_op = op_T4_ind_y, .opcode_bits = 0b00010001, .opcode_mask = 0b00011101, .timing = 0b010000};
	decode_lines_second_half[15] = (struct DecodeLine) { .rom_op = op_T5_ind_y, .opcode_bits = 0b00010001, .opcode_mask = 0b00011101, .timing = 0b100000};
	decode_lines_second_half[16] = (struct DecodeLine) { .rom_op = op_T2_ind_x, .opcode_bits = 0b00000001, .opcode_mask = 0b00011101, .timing = 0b000100};
	decode_lines_second_half[17] = (struct DecodeLine) { .rom_op = op_T3_ind_x, .opcode_bits = 0b00000001, .opcode_mask = 0b00011101, .timing = 0b001000};
	decode_lines_second_half[18] = (struct DecodeLine) { .rom_op = op_T4_ind_x, .opcode_bits = 0b00000001, .opcode_mask = 0b00011101, .timing = 0b010000};
	decode_lines_second_half[19] = (struct DecodeLine) { .rom_op = op_T5_ind_x, .opcode_bits = 0b00000001, .opcode_mask = 0b00011101, .timing = 0b100000};
	decode_lines_second_half[20] = (struct DecodeLine) { .rom_op = op_T0_and, .opcode_bits = 0b00100001, .opcode_mask = 0b11100001, .timing = 0b000001};
	decode_lines_second_half[21] = (struct DecodeLine) { .rom_op = op_T0_ora, .opcode_bits = 0b00000001, .opcode_mask = 0b11100001, .timing = 0b000001};
	decode_lines_second_half[22] = (struct DecodeLine) { .rom_op = op_T0_eor, .opcode_bits = 0b01000001, .opcode_mask = 0b11100001, .timing = 0b000001};
	decode_lines_second_half[23] = (struct DecodeLine) { .rom_op = op_T0_adc, .opcode_bits = 0b01100001, .opcode_mask = 0b11100001, .timing = 0b000001};
	decode_lines_second_half[24] = (struct DecodeLine) { .rom_op = op_T0_sbc, .opcode_bits = 0b11100001, .opcode_mask = 0b11100001, .timing = 0b000001};
	decode_lines_second_half[25] = (struct DecodeLine) { .rom_op = op_T0_cmp, .opcode_bits = 0b11000001, .opcode_mask = 0b11100001, .timing = 0b000001};
	decode_lines_second_half[26] = (struct DecodeLine) { .rom_op = op_sta, .opcode_bits = 0b10000001, .opcode_mask = 0b11100001, .timing = 0b000000};
	decode_lines_second_half[27] = (struct DecodeLine) { .rom_op = op_lsr_ror_dec_inc, .opcode_bits = 0b01000010, .opcode_mask = 0b01000010, .timing = 0b000000};
	decode_lines_second_half[28] = (struct DecodeLine) { .rom_op = op_asl_rol, .opcode_bits = 0b00000010, .opcode_mask = 0b11000010, .timing = 0b000000};
	decode_lines_second_half[29] = (struct DecodeLine) { .rom_op = op_T1_asl_acc, .opcode_bits = 0b00001010, .opcode_mask = 0b11111110, .timing = 0b000010};
	decode_lines_second_half[30] = (struct DecodeLine) { .rom_op = op_T1_rol_acc, .opcode_bits = 0b00101010, .opcode_mask = 0b11111110, .timing = 0b000010};
	decode_lines_second_half[31] = (struct DecodeLine) { .rom_op = op_T1_lsr_acc, .opcode_bits = 0b01001010, .opcode_mask = 0b11111110, .timing = 0b000010};
	decode_lines_second_half[32] = (struct DecodeLine) { .rom_op = op_T1_ror_acc, .opcode_bits = 0b01101010, .opcode_mask = 0b11111110, .timing = 0b000010};
	decode_lines_second_half[33] = (struct DecodeLine) { .rom_op = op_T0_ldx, .opcode_bits = 0b10100110, .opcode_mask = 0b11100110, .timing = 0b000001};
	decode_lines_second_half[34] = (struct DecodeLine) { .rom_op = op_T0_ldy, .opcode_bits = 0b10100100, .opcode_mask = 0b11100111, .timing = 0b000001};
	decode_lines_second_half[35] = (struct DecodeLine) { .rom_op = op_T2_zp_abs, .opcode_bits = 0b00000100, .opcode_mask = 0b00000101, .timing = 0b000100};
	decode_lines_second_half[36] = (struct DecodeLine) { .rom_op = op_T2_mem_zp_idx, .opcode_bits = 0b00010100, .opcode_mask = 0b00011100, .timing = 0b000100};
	decode_lines_second_half[37] = (struct DecodeLine) { .rom_op = op_T3_jmp, .opcode_bits = 0b01001100, .opcode_mask = 0b11011111, .timing = 0b001000};
	decode_lines_second_half[38] = (struct DecodeLine) { .rom_op = op_T4_jmp, .opcode_bits = 0b01001100, .opcode_mask = 0b11011111, .timing = 0b010000};
	decode_lines_second_half[39] = (struct DecodeLine) { .rom_op = op_sta, .opcode_bits = 0b10000100, .opcode_mask = 0b11100101, .timing = 0b000000};
	decode_lines_second_half[40] = (struct DecodeLine) { .rom_op = op_T0_cpx, .opcode_bits = 0b11100100, .opcode_mask = 0b11110111, .timing = 0b000001};
	decode_lines_second_half[41] = (struct DecodeLine) { .rom_op = op_T0_cpy, .opcode_bits = 0b11000100, .opcode_mask = 0b11110111, .timing = 0b000001};
	decode_lines_second_half[42] = (struct DecodeLine) { .rom_op = op_T0_bit, .opcode_bits = 0b00100100, .opcode_mask = 0b11110111, .timing = 0b000001};
	decode_lines_second_half[43] = (struct DecodeLine) { .rom_op = op_T2_imm, .opcode_bits = 0b10000000, .opcode_mask = 0b10011101, .timing = 0b000100};
	decode_lines_second_half[44] = (struct DecodeLine) { .rom_op = op_T0_ldx, .opcode_bits = 0b10100010, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[45] = (struct DecodeLine) { .rom_op = op_T0_ldy, .opcode_bits = 0b10100000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[46] = (struct DecodeLine) { .rom_op = op_T0_cpy, .opcode_bits = 0b11000000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[47] = (struct DecodeLine) { .rom_op = op_T0_cpx, .opcode_bits = 0b11100000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[48] = (struct DecodeLine) { .rom_op = op_T2_php_pha, .opcode_bits = 0b00001000, .opcode_mask = 0b10111111, .timing = 0b000100};
	decode_lines_second_half[49] = (struct DecodeLine) { .rom_op = op_T2_stack_pull, .opcode_bits = 0b00101000, .opcode_mask = 0b10111111, .timing = 0b000100};
	decode_lines_second_half[50] = (struct DecodeLine) { .rom_op = op_T3_pla_pha, .opcode_bits = 0b00101000, .opcode_mask = 0b10111111, .timing = 0b001000};
	decode_lines_second_half[51] = (struct DecodeLine) { .rom_op = op_T0_plp, .opcode_bits = 0b00101000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[52] = (struct DecodeLine) { .rom_op = op_T0_pla, .opcode_bits = 0b01101000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[53] = (struct DecodeLine) { .rom_op = op_T2_stack_pull, .opcode_bits = 0b01000000, .opcode_mask = 0b11011111, .timing = 0b000100};
	decode_lines_second_half[54] = (struct DecodeLine) { .rom_op = op_T3_rti_rts, .opcode_bits = 0b01000000, .opcode_mask = 0b11011111, .timing = 0b001000};
	decode_lines_second_half[55] = (struct DecodeLine) { .rom_op = op_T4_rti_rts, .opcode_bits = 0b01000000, .opcode_mask = 0b11011111, .timing = 0b010000};
	decode_lines_second_half[56] = (struct DecodeLine) { .rom_op = op_T5_rts, .opcode_bits = 0b01100000, .opcode_mask = 0b11111111, .timing = 0b100000};
	decode_lines_second_half[57] = (struct DecodeLine) { .rom_op = op_T0_rts, .opcode_bits = 0b01100000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[58] = (struct DecodeLine) { .rom_op = op_T5_rti, .opcode_bits = 0b01000000, .opcode_mask = 0b11111111, .timing = 0b100000};
	decode_lines_second_half[59] = (struct DecodeLine) { .rom_op = op_T0_rti, .opcode_bits = 0b01000000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[60] = (struct DecodeLine) { .rom_op = op_T2_jsr, .opcode_bits = 0b00100000, .opcode_mask = 0b11111111, .timing = 0b000100};
	decode_lines_second_half[61] = (struct DecodeLine) { .rom_op = op_T3_jsr, .opcode_bits = 0b00100000, .opcode_mask = 0b11111111, .timing = 0b001000};
	decode_lines_second_half[62] = (struct DecodeLine) { .rom_op = op_T5_jsr_second_half, .opcode_bits = 0b00100000, .opcode_mask = 0b11111111, .timing = 0b100000};
	decode_lines_second_half[63] = (struct DecodeLine) { .rom_op = op_T0_jsr, .opcode_bits = 0b00100000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[64] = (struct DecodeLine) { .rom_op = op_T0_txa, .opcode_bits = 0b10001010, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[65] = (struct DecodeLine) { .rom_op = op_T0_tax, .opcode_bits = 0b10101010, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[66] = (struct DecodeLine) { .rom_op = op_T0_dex, .opcode_bits = 0b11001010, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[67] = (struct DecodeLine) { .rom_op = op_T0_inx, .opcode_bits = 0b11101000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[68] = (struct DecodeLine) { .rom_op = op_T0_txs, .opcode_bits = 0b10011010, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[69] = (struct DecodeLine) { .rom_op = op_T0_tsx, .opcode_bits = 0b10111010, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[70] = (struct DecodeLine) { .rom_op = op_T0_tya, .opcode_bits = 0b10011000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[71] = (struct DecodeLine) { .rom_op = op_T0_tay, .opcode_bits = 0b10101000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[72] = (struct DecodeLine) { .rom_op = op_T0_iny, .opcode_bits = 0b11001000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[73] = (struct DecodeLine) { .rom_op = op_T0_dey, .opcode_bits = 0b10001000, .opcode_mask = 0b11111111, .timing = 0b000001};
	decode_lines_second_half[74] = (struct DecodeLine) { .rom_op = op_brk_stack, .opcode_bits = 0b00000000, .opcode_mask = 0b11111111, .timing = 0b000100};
	decode_lines_second_half[75] = (struct DecodeLine) { .rom_op = op_brk_stack, .opcode_bits = 0b00000000, .opcode_mask = 0b11111111, .timing = 0b001000};
	decode_lines_second_half[76] = (struct DecodeLine) { .rom_op = op_brk_stack, .opcode_bits = 0b00000000, .opcode_mask = 0b11111111, .timing = 0b010000};
	decode_lines_second_half[77] = (struct DecodeLine) { .rom_op = op_brk, .opcode_bits = 0b00000000, .opcode_mask = 0b11111111, .timing = 0b000000};
	decode_lines_second_half[78] = (struct DecodeLine) { .rom_op = op_T0_brk, .opcode_bits = 0b00000000, .opcode_mask = 0b11111111, .timing = 0b000001};
	
	reset_cpu();
}