#ifndef PPU_HEADER
#define PPU_HEADER

extern const unsigned int KB;

extern unsigned char* ppu_ram;
extern unsigned char* palette_ram;
extern unsigned char* oam;

extern unsigned char ppu_bus;

extern unsigned char pending_nmi;

void exit_emulator();

void ppu_init();
void access_ppu_register(unsigned char* data, unsigned int ppu_register, unsigned char access_type);
unsigned char ppu_tick();

void ppu_save_state(FILE* save_file);
void ppu_load_state(FILE* save_file);

#endif