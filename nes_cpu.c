#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include "nes_cpu.h"
#include "nes_ppu.h"

unsigned int mapper_type;
unsigned char accumulator;
unsigned char x_register;
unsigned char y_register;
unsigned int program_counter;
unsigned char stack_pointer;
unsigned char status_flags;

unsigned char apu_status;

unsigned char controller_1_port;
unsigned char controller_2_port;

unsigned char* cpu_ram;

unsigned char* get_pointer_at_cpu_address(unsigned int address)
{
	// First 2KB is the NES's own CPU RAM.
	if (address <= 0x07FF)
	{
		return &cpu_ram[address];
	}
	// 0x0800 through 0x1FFF mirrors CPU RAM three times.
	else if (address >= 0x0800 && address <= 0x0FFF)
	{
		return &cpu_ram[address - 0x0800];
	}
	else if (address >= 0x1000 && address <= 0x17FF)
	{
		return &cpu_ram[address - 0x1000];
	}
	else if (address >= 0x1800 && address <= 0x1FFF)
	{
		return &cpu_ram[address - 0x1800];
	}
	// PPU registers. Found in 0x2000 through 0x2007, but they're mirrored every 8 bytes.
	else if (address >= 0x2000 && address <= 0x3FFF)
	{
		int ppu_register = 0x2000 + (address % 8);
		notify_ppu(ppu_register);
		return &ppu_bus;
	}
	// APU status
	else if (address == 0x4015)
	{
		return &apu_status;
	}
	// Controller port 1
	else if (address == 0x4016)
	{
		// TODO: Always returning 0 here for now. Eventually will need to handle unusual behavior of read/write, plus actual input.
		return &controller_1_port;
	}
	// Controller port 2 / APU frame counter???
	else if (address == 0x4017)
	{
		// TODO: Ditto for port 2.
		return &controller_2_port;
	}
	// Assuming for now that we're using mapper type 0.
	// 0x8000 through 0xBFFF addresses the first 16KB bytes of ROM.
	else if (address >= 0x8000 && address <= 0xBFFF)
	{
		return &prg_rom[address - 0x8000];
	}
	// 0xC000 through 0xFFFF mirrors the first 16KB bytes of ROM.
	// Note: If there are 32KB bytes of ROM, they would be found here instead.
	// I'll worry about that later.
	else if (address >= 0xC000 && address < 0xFFFF)
	{
		return &prg_rom[address - 0xC000];
	}
	else
	{
		printf("Unhandled CPU address %04X\n", address);
		exit_emulator();
		return NULL;
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

// Sets the carry flag if a + b would carry, clears it otherwise.
void test_carry_addition(unsigned char operand_a, unsigned char operand_b)
{
	if ((0xFF - operand_a) < operand_b)
	{
		status_flags = status_flags | 0b00000001;
	}
	else
	{
		status_flags = status_flags & 0b11111110;
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

// Sets the overflow flag if the operands, treated as signed bytes, will overflow when added. Clears it otherwise.
void test_overflow_addition(unsigned char operand_a, unsigned char operand_b)
{
	if (operand_a >= 0x80)
	{
		operand_a = (~operand_a) + 1;
	}
	
	if (operand_b >= 0x80)
	{
		operand_b = (~operand_b) + 1;
	}
	
	if ((operand_a + operand_b) >= 0x80)
	{
		status_flags = status_flags | 0b01000000;
	}
	else
	{
		status_flags = status_flags & 0b10111111;
	}
}

// Returns the number of cycles spent.
unsigned int branch_on_status_flags(char mask, char value, char debug_branch[], char debug_skip[])
{
	unsigned int cycles = 2;
	if ((status_flags & mask) == value)
	{
		cycles++;
		unsigned char branch_value = *get_pointer_at_cpu_address(program_counter + 1);
		unsigned char adjusted_branch_value = branch_value;
		unsigned int skip_compare = program_counter + 2;
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
		if ((skip_compare & 0x00FF) != (program_counter & 0x00FF))
		{
			cycles++;
		}
		#ifdef DEBUG
		if (debug_counter < DEBUG_LIMIT) printf(debug_branch, branch_value, program_counter);
		#endif
	}
	#ifdef DEBUG
	else
	{
		debug_counter = 0;
		if (debug_counter < DEBUG_LIMIT) printf(debug_skip);
	}
	#endif
	program_counter += 2;
	return cycles;
}

// Pushes a byte to the stack. The stack starts at $01FF and grows downward.
void push_to_stack(unsigned char byte)
{
	unsigned char* stack_top = get_pointer_at_cpu_address(STACK_PAGE + stack_pointer);
	*stack_top = byte;
	stack_pointer--;
}

// Pulls a byte off the stack.
unsigned char pull_from_stack()
{
	stack_pointer++;
	unsigned char stack_top_byte = *get_pointer_at_cpu_address(STACK_PAGE + stack_pointer);
	return stack_top_byte;
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
unsigned int run_opcode(unsigned char opcode)
{
	#ifdef DEBUG
	//debug_counter++;
	if (debug_counter < DEBUG_LIMIT) printf("%04X: ", program_counter);
	#endif
	
	unsigned int cycles = 0;
	
	switch(opcode)
	{
		// NOP: Does nothing.
		// Implied NOP. Hex: $EA  Len: 1  Time: 2
		case 0xEA:
		{
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("No operation\n");
			#endif
			break;
		}
		// CLC: Clears the carry flag.
		// Implied CLC. Hex: $18  Len: 1  Time: 2
		case 0x18:
		{
			status_flags = status_flags & 0b11111110;
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Clear the carry flag\n");
			#endif
			break;
		}
		// SEC: Sets the carry flag.
		// Implied SEC. Hex: $38  Len: 1  Time: 2
		case 0x38:
		{
			status_flags = status_flags | 0b00000001;
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Set the carry flag\n");
			#endif
			break;
		}
		// CLI: Clears the interrupt disable flag.
		// Implied CLI. Hex: $58  Len: 1  Time: 2
		case 0x58:
		{
			status_flags = status_flags & 0b11111011;
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Clear the interrupt disable flag.\n");
			#endif
			break;
		}
		// SEI: Sets the interrupt disable flag.
		// Implied SEI. Hex: $78  Len: 1  Time: 2
		case 0x78:
		{
			status_flags = status_flags | 0b00000100;
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Set the interrupt disable flag.\n");
			#endif
			break;
		}
		// CLV: Clears the overflow flag.
		// Implied CLV. Hex: $B8  Len: 1  Time: 2
		case 0xB8:
		{
			status_flags = status_flags & 0b10111111;
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Clear the overflow flag.\n");
			#endif
			break;
		}
		// CLD: Clears the decimal flag.
		// Implied CLD. Hex: $D8  Len: 1  Time: 2
		case 0xD8:
		{
			status_flags = status_flags & 0b11110111;
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Clear the decimal flag.\n");
			#endif
			break;
		}
		// SED: Sets the decimal flag.
		// Implied SEI. Hex: $F8  Len: 1  Time: 2
		case 0xF8:
		{
			status_flags = status_flags | 0b00001000;
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Set the decimal flag.\n");
			#endif
			break;
		}
		// JMP: Jumps to an address specified by the operand.
		// Absolute JMP. Hex: $4C  Len: 3  Time: 3 
		case 0x4C:
		{
			unsigned char low_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char high_byte = *get_pointer_at_cpu_address(program_counter + 2);
			program_counter = low_byte + (high_byte * 0x100);
			cycles = 3;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Absolute jump to %04X\n", low_byte + (high_byte * 0x100));
			#endif
			break;
		}
		// Indirect JMP. Hex: $6C  Len: 3  Time: 5
		case 0x6C:
		{
			unsigned char low_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char high_byte = *get_pointer_at_cpu_address(program_counter + 2);
			unsigned char indirect_low_byte = *get_pointer_at_cpu_address(low_byte + (high_byte * 0x100));
			low_byte++; // Intended to overflow to 0x00 if it's at 0xFF.
			unsigned char indirect_high_byte = *get_pointer_at_cpu_address(low_byte + (high_byte * 0x100));
			program_counter = indirect_low_byte + (indirect_high_byte * 0x100);
			cycles = 5;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Indirect jump to %04X found in address %04X\n", indirect_low_byte + (indirect_high_byte * 0x100), low_byte + (high_byte * 0x100));
			#endif
			break;
		}
		// JSR: Jump to subroutine. Pushes the next instruction onto the stack and jumps to an address specified by the operand.
		// Absolute JSR. Hex: $20  Len: 3  Time: 6
		case 0x20:
		{
			// Take the program counter + 2 and push the high byte to the stack, then the low byte.
			unsigned int push_value = program_counter + 2;
			push_to_stack(push_value / 0x100);
			push_to_stack(push_value % 0x100);
			unsigned char low_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char high_byte = *get_pointer_at_cpu_address(program_counter + 2);
			program_counter = low_byte + (high_byte * 0x100);
			cycles = 6;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Jump to subroutine at %04X\n", low_byte + (high_byte * 0x100));
			#endif
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
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Return from subroutine to %04X\n", program_counter);
			#endif
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
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Return from interrupt to %04X\n", program_counter);
			#endif
			break;
		}
		// BPL: Branches on sign clear.
		// Relative BPL. Hex: $10  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
		case 0x10:
		{
			cycles = branch_on_status_flags(0b10000000, 0b00000000, "Branch %02X bytes on sign clear to address %04X\n", "Skip sign clear branch\n");
			break;
		}
		// BMI: Branches on sign set.
		// Relative BMI. Hex: $30  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
		case 0x30:
		{
			cycles = branch_on_status_flags(0b10000000, 0b10000000, "Branch %02X bytes on sign set to address %04X\n", "Skip sign set branch\n");
			break;
		}
		// BVC: Branches on overflow clear.
		// Relative BVC. Hex: $50  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
		case 0x50:
		{
			cycles = branch_on_status_flags(0b01000000, 0b00000000, "Branch %02X bytes on overflow clear to address %04X\n", "Skip overflow clear branch\n");
			break;
		}
		// BVS: Branches on overflow set.
		// Relative BVS. Hex: $70  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
		case 0x70:
		{
			cycles = branch_on_status_flags(0b01000000, 0b01000000, "Branch %02X bytes on overflow set to address %04X\n", "Skip overflow set branch\n");
			break;
		}
		// BCC: Branches on carry clear.
		// Relative BCC. Hex: $90  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
		case 0x90:
		{
			cycles = branch_on_status_flags(0b00000001, 0b00000000, "Branch %02X bytes on carry clear to address %04X\n", "Skip carry clear branch\n");
			break;
		}
		// BCS: Branches on carry set.
		// Relative BCS. Hex: $B0  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
		case 0xB0:
		{
			cycles = branch_on_status_flags(0b00000001, 0b00000001, "Branch %02X bytes on carry set to address %04X\n", "Skip carry set branch\n");
			break;
		}
		// BNE: Branches on zero clear.
		// Relative BNE. Hex: $D0  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
		case 0xD0:
		{
			cycles = branch_on_status_flags(0b00000010, 0b00000000, "Branch %02X bytes on zero clear to address %04X\n", "Skip zero clear branch\n");
			break;
		}
		// BEQ: Branches on zero set.
		// Relative BEQ. Hex: $F0  Len: 2  Time: 2 + 1 [if branch taken] + 1 [if branch crosses page boundary]
		case 0xF0:
		{
			cycles = branch_on_status_flags(0b00000010, 0b00000010, "Branch %02X bytes on zero set to address %04X\n", "Skip zero set branch\n");
			break;
		}
		// TAX: Transfers A to X.
		// Implied TAX. Hex: $AA  Len: 1  Time: 2
		// Affects flags S and Z.
		case 0xAA:
		{
			x_register = accumulator;
			test_negative_flag(x_register);
			test_zero_flag(x_register);
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Transfer A to X.\n");
			#endif
			break;
		}
		// TXA: Transfers X to A.
		// Implied TXA. Hex: $8A  Len: 1  Time: 2
		// Affects flags S and Z.
		case 0x8A:
		{
			accumulator = x_register;
			test_negative_flag(accumulator);
			test_zero_flag(accumulator);
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Transfer X to A.\n");
			#endif
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
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Decrement X register to %02X\n", x_register);
			#endif
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
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Increment X register to %02X\n", x_register);
			#endif
			break;
		}
		// TAY: Transfers A to Y.
		// Implied TAY. Hex: $A8  Len: 1  Time: 2
		// Affects flags S and Z.
		case 0xA8:
		{
			y_register = accumulator;
			test_negative_flag(y_register);
			test_zero_flag(y_register);
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Transfer A to Y.\n");
			#endif
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
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Decrement Y register to %02X\n", y_register);
			#endif
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
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Increment Y register to %02X\n", y_register);
			#endif
			break;
		}
		// INC: Increment memory byte.
		// Zero page INC. Hex: $E6  Len: 2  Time: 5
		// Affects flags S and Z.
		case 0xE6:
		{
			unsigned char address_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char* target = get_pointer_at_cpu_address(address_byte);
			*target += 1;
			test_negative_flag(*target);
			test_zero_flag(*target);
			program_counter += 2;
			cycles = 5;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Increment value at zero page address %02X to %02X\n", address_byte, *target);
			#endif
			break;
		}
		// ADC: Adds a value to the accumulator.
		// Immediate ADC. Hex: $69  Len: 2  Time: 2
		// Affects flags S, V, Z, and C.
		case 0x69:
		{
			unsigned char load_byte = *get_pointer_at_cpu_address(program_counter + 1);
			test_carry_addition(load_byte, accumulator);
			test_overflow_addition(load_byte, accumulator);
			accumulator += load_byte;
			test_negative_flag(accumulator);
			test_zero_flag(accumulator);
			program_counter += 2;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Add immediate value %02X to accumulator\n", load_byte);
			#endif
			break;
		}
		// AND: Performs bitwise AND with the accumulator.
		// Zero page AND. Hex: $25  Len: 2  Time: 3
		// Affects flags S and Z.
		case 0x25:
		{
			unsigned char address_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char load_byte = *get_pointer_at_cpu_address(address_byte);
			accumulator = load_byte & accumulator;
			test_negative_flag(accumulator);
			test_zero_flag(accumulator);
			program_counter += 2;
			cycles = 3;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("AND the accumulator with value at zero page address %02X to %02X\n", address_byte, accumulator);
			#endif
			break;
		}
		// EOR: Perform bitwise XOR with the accumulator.
		// Zero page EOR. Hex: $45  Len: 2  Time: 3
		// Affects S and Z.
		case 0x45:
		{
			unsigned char address_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char load_byte = *get_pointer_at_cpu_address(address_byte);
			accumulator = load_byte ^ accumulator;
			test_negative_flag(accumulator);
			test_zero_flag(accumulator);
			program_counter += 2;
			cycles = 3;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("XOR the accumulator with value at zero page address %02X to %02X\n", address_byte, accumulator);
			#endif
			break;
		}
		// LSR: Logical shift right.
		// Accumulator LSR. Hex: $4A  Len: 1  Time: 2
		// Affects flags S, Z, and C.
		case 0x4A:
		{
			test_carry_right_shift(accumulator);
			accumulator = accumulator >> 1;
			test_negative_flag(accumulator);
			test_zero_flag(accumulator);
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Right shift the accumulator to %02X\n", accumulator);
			#endif
			break;
		}
		// ROL: Rotate left. The byte that falls off the edge goes into carry, and the carry bit goes in the other side.
		// Zero page ROL. Hex: $26  Len: 2  Time: 5
		// Affects flags S, Z, and C.
		case 0x26:
		{
			unsigned char address_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char* target = get_pointer_at_cpu_address(address_byte);
			unsigned char carry_bit = status_flags & 0b1;
			test_carry_left_shift(*target);
			*target = (*target << 1) | carry_bit;
			test_negative_flag(*target);
			test_zero_flag(*target);
			program_counter += 2;
			cycles = 5;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Right roll the zero page address %02X to %02X\n", address_byte, *target);
			#endif
			break;
		}
		// CMP: Compares the accumulator to a value, and sets the sign, carry, and zero flags accordingly.
		// Immediate CMP. Hex: $C9  Len: 2  Time: 2
		// Affects flags S, C, and Z.
		case 0xC9:
		{
			unsigned char load_byte = *get_pointer_at_cpu_address(program_counter + 1);
			test_negative_flag(accumulator - load_byte);
			test_zero_flag(accumulator - load_byte);
			test_carry_subtraction(accumulator, load_byte);
			program_counter += 2;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Compare accumulator to %02X\n", load_byte);
			#endif
			break;
		}
		// Zero page CMP. Hex: $C5  Len: 2  Time: 3
		case 0xC5:
		{
			unsigned char address_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char load_byte = *get_pointer_at_cpu_address(address_byte);
			test_negative_flag(accumulator - load_byte);
			test_zero_flag(accumulator - load_byte);
			test_carry_subtraction(accumulator, load_byte);
			program_counter += 2;
			cycles = 3;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Compare accumulator to value %02X found at zero page address %02X\n", load_byte, address_byte);
			#endif
			break;
		}
		// CPX: Compares the X register to a value, and sets the sign, carry, and zero flags accordingly.
		// Immediate CPX. Hex: $E0  Len: 2  Time: 2
		// Affects flags S, C, and Z.
		case 0xE0:
		{
			unsigned char load_byte = *get_pointer_at_cpu_address(program_counter + 1);
			test_negative_flag(x_register - load_byte);
			test_zero_flag(x_register - load_byte);
			test_carry_subtraction(x_register, load_byte);
			program_counter += 2;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Compare X register to %02X\n", load_byte);
			#endif
			break;
		}
		// LDA: Loads a byte into the accumulator.
		// Immediate LDA. Hex: $A9  Len: 2  Time: 2
		// Affects flags S and Z.
		case 0xA9:
		{
			unsigned char load_byte = *get_pointer_at_cpu_address(program_counter + 1);
			accumulator = load_byte;
			test_negative_flag(load_byte);
			test_zero_flag(load_byte);
			program_counter += 2;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Load accumulator with immediate value %02X\n", load_byte);
			#endif
			break;
		}
		// Zero page LDA. Hex: $A5  Len: 2  Time: 3
		case 0xA5:
		{
			unsigned char address_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char load_byte = *get_pointer_at_cpu_address(address_byte);
			accumulator = load_byte;
			test_negative_flag(load_byte);
			test_zero_flag(load_byte);
			program_counter += 2;
			cycles = 3;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Load accumulator with value %02X found at zero page address %02X\n", load_byte, address_byte);
			#endif
			break;
		}
		// Absolute LDA. Hex: $AD  Len: 3  Time: 4
		// Affects flags S and Z.
		case 0xAD:
		{
			unsigned char low_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char high_byte = *get_pointer_at_cpu_address(program_counter + 2);
			unsigned char load_byte = *get_pointer_at_cpu_address(low_byte + (high_byte * 0x100));
			accumulator = load_byte;
			test_negative_flag(load_byte);
			test_zero_flag(load_byte);
			program_counter += 3;
			cycles = 4;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Load accumulator with value %02X found at absolute address %04X\n", load_byte, low_byte + (high_byte * 0x100));
			#endif
			break;
		}
		// Absolute,X LDA. Hex: $BD  Len: 3  Time: 4 + 1 [if crossed page boundary]
		// Affects flags S and Z.
		case 0xBD:
		{
			unsigned char low_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char high_byte = *get_pointer_at_cpu_address(program_counter + 2);
			unsigned int address = low_byte + (high_byte * 0x100);
			unsigned char load_byte = *get_pointer_at_cpu_address(address + x_register);
			accumulator = load_byte;
			test_negative_flag(load_byte);
			test_zero_flag(load_byte);
			program_counter += 3;
			cycles = 4;
			// Add cycle if page boundary crossed.
			if ((address & 0xFF00) != ((address + x_register) & 0xFF00))
			{
				cycles++;
			}
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Load accumulator with value %02X found at absolute address %04X plus X register index %02X\n", load_byte, address, x_register);
			#endif
			break;
		}
		// Indirect,Y LDA. Hex: $B1  Len: 2  Time: 5 + 1 [if crossed page boundary]
		// Affects flags S and Z.
		case 0xB1:
		{
			unsigned char indirect_address_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char low_byte = *get_pointer_at_cpu_address(indirect_address_byte);
			unsigned char high_byte = *get_pointer_at_cpu_address(indirect_address_byte + 1);
			unsigned int address = low_byte + (high_byte * 0x100);
			unsigned char load_byte = *get_pointer_at_cpu_address(address + y_register);
			accumulator = load_byte;
			test_negative_flag(load_byte);
			test_zero_flag(load_byte);
			program_counter += 2;
			cycles = 5;
			// Add a cycle if page boundary crossed.
			if ((address & 0xFF00) != ((address + y_register) & 0xFF00))
			{
				cycles++;
			}
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Load accumulator with value %02X found at indirect address %04X plus Y register index %02X\n", load_byte, address, y_register);
			#endif
			break;
		}
		// LDX: Loads a byte into the X register.
		// Immediate LDX. Hex: $A2  Len: 2  Time: 2
		// Affects flags S and Z.
		case 0xA2:
		{
			unsigned char load_byte = *get_pointer_at_cpu_address(program_counter + 1);
			x_register = load_byte;
			test_negative_flag(load_byte);
			test_zero_flag(load_byte);
			program_counter += 2;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Load X with immediate value %02X\n", load_byte);
			#endif
			break;
		}
		// LDY: Loads a byte into the Y register.
		// Immediate LDY. Hex: $A0  Len: 2  Time: 2
		case 0xA0:
		{
			unsigned char load_byte = *get_pointer_at_cpu_address(program_counter + 1);
			y_register = load_byte;
			test_negative_flag(load_byte);
			test_zero_flag(load_byte);
			program_counter += 2;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Load Y with immediate value %02X\n", load_byte);
			#endif
			break;
		}
		// STA: Stores the accumulator in the address specified by the operand.
		// Zero page STA. Hex: $85  Len: 2  Time: 3
		case 0x85:
		{
			unsigned char address_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char* target = get_pointer_at_cpu_address(address_byte);
			*target = accumulator;
			program_counter += 2;
			cycles = 3;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Store value %02X from accumulator in the zero page address %02X\n", accumulator, address_byte);
			#endif
			break;
		}
		// Absolute STA. Hex: $8D  Len: 3  Time: 4
		case 0x8D:
		{
			unsigned char low_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char high_byte = *get_pointer_at_cpu_address(program_counter + 2);
			unsigned char* target = get_pointer_at_cpu_address(low_byte + (high_byte * 0x100));
			*target = accumulator;
			program_counter += 3;
			cycles = 4;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Store value %02X from accumulator in the address %04X\n", accumulator, low_byte + (high_byte * 0x100));
			#endif
			break;
		}
		// STX: Stores the X register in the address specified by the operand.
		// Zero Page STX. Hex: $86  Len: 2  Time: 3
		case 0x86:
		{
			unsigned char operand = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char* target = get_pointer_at_cpu_address(operand);
			*target = x_register;
			program_counter += 2;
			cycles = 3;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Store value %02X from X in the address %04X\n", x_register, operand);
			#endif
			break;
		}
		// Absolute STX. Hex: $8E  Len: 3  Time: 4
		case 0x8E:
		{
			unsigned char low_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char high_byte = *get_pointer_at_cpu_address(program_counter + 2);
			unsigned char* target = get_pointer_at_cpu_address(low_byte + (high_byte * 0x100));
			*target = x_register;
			program_counter += 3;
			cycles = 4;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Store value %02X from X in the address %04X\n", x_register, low_byte + (high_byte * 0x100));
			#endif
			break;
		}
		// STY: Stores the Y register in the address specified by the operand.
		// Absolute STY. Hex: $8C  Len: 3  Time: 4
		case 0x8C:
		{
			unsigned char low_byte = *get_pointer_at_cpu_address(program_counter + 1);
			unsigned char high_byte = *get_pointer_at_cpu_address(program_counter + 2);
			unsigned char* target = get_pointer_at_cpu_address(low_byte + (high_byte * 0x100));
			*target = y_register;
			program_counter += 3;
			cycles = 4;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Store value %02X from X in the address %04X\n", x_register, low_byte + (high_byte * 0x100));
			#endif
			break;
		}
		// TXS: Loads the value of the X register into the stack pointer.
		// Implied TXS. Hex: $9A  Len: 1  Time: 2
		case 0x9A:
		{
			stack_pointer = x_register;
			program_counter++;
			cycles = 2;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Transfer X register to stack pointer\n");
			#endif
			break;
		}
		// PHA: Push accumulator to the stack.
		// Implied PHA. Hex: $48  Len: 1  Time: 3
		case 0x48:
		{
			push_to_stack(accumulator);
			program_counter++;
			cycles = 3;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Push accumulator to the stack\n");
			#endif
			break;
		}
		case 0x68:
		{
			accumulator = pull_from_stack();
			program_counter++;
			cycles = 4;
			#ifdef DEBUG
			if (debug_counter < DEBUG_LIMIT) printf("Pull stack value to accumulator\n");
			#endif
			break;
		}
		default:
		{
			printf("%04X: Stopping execution on opcode %02X\n", program_counter, opcode);
			exit_emulator();
			break;
		}
	}
	
	if (pending_nmi)
	{
		push_to_stack(program_counter / 0x100);
		push_to_stack(program_counter % 0x100);
		push_to_stack((status_flags | 0b00100000) & 0b11101111);
		unsigned char low_address_byte = *get_pointer_at_cpu_address(0xFFFA);
		unsigned char high_address_byte = *get_pointer_at_cpu_address(0xFFFB);
		program_counter = low_address_byte + (high_address_byte * 0x100);
		cycles += 7;
		pending_nmi = 0;
	}
	
	return cycles;
}

void cpu_init()
{
	mapper_type = 0;
	accumulator = 0;
	x_register = 0;
	y_register = 0;
	stack_pointer = 0xFF;
	status_flags = 0;
	
	cpu_ram = malloc(sizeof(char) * KB * 2);
	
	// On init, JMP to the address at $FFFC.
	program_counter = 0xFFFB;
	run_opcode(0x4C);
}