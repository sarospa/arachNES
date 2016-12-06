#ifndef CPU_HEADER
#define CPU_HEADER

#ifdef DEBUG
extern int debug_counter;
extern const int DEBUG_LIMIT;
#endif

extern const unsigned int KB;
extern const unsigned int STACK_PAGE;

extern unsigned const char WRITE;
extern unsigned const char READ;

extern unsigned int mapper_type;
extern unsigned char accumulator;
extern unsigned char x_register;
extern unsigned char y_register;
extern unsigned int program_counter;
extern unsigned char stack_pointer;
// Bit 7: S - Sign Flag - Set if result is negative, cleared if positive. A number is considered 'negative' if its high bit is set.
// Bit 6: V - Overflow Flag - Set if result overflows.
// Bit 5: Not used. Should be 1 when saved to memory.
// Bit 4: Weird. When saved to memory, varies depending on what triggered the interrupt.
// Bit 3: D - Decimal Flag - Used for decimal mode. Not supported in the NES.
// Bit 2: I - Interrupt Disable Flag - Disables interrupts if set.
// Bit 1: Z - Zero Flag - Set if result is zero, cleared otherwise.
// Bit 0: C - Carry Flag - Set if arithmetic carry is required, or arithmetic borrow is not required. Cleared if not.
extern unsigned char status_flags;

// Status for the audio processing unit.
extern unsigned char apu_status;

extern unsigned char controller_1_port;
extern unsigned char controller_2_port;

extern unsigned char pending_nmi;

extern unsigned char* cpu_ram;
extern unsigned char* prg_rom;

void exit_emulator();

void reset_cpu();
void cpu_init();
unsigned int run_opcode(unsigned char opcode);
void get_pointer_at_cpu_address(unsigned char* data, unsigned int address, unsigned char write);

void stack_dump();

#endif