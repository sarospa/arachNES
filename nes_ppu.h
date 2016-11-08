#ifndef PPU_HEADER
#define PPU_HEADER

extern const int KB;

extern unsigned char* ppu_ram;
extern unsigned char* palette_ram;
extern unsigned char* oam;

extern unsigned char ppu_bus;

extern unsigned char pending_nmi;

void exit_emulator();

void ppu_init();
void notify_ppu(unsigned int ppu_register, unsigned char access_type);
void notify_ppu_write(unsigned int ppu_register);
unsigned char ppu_tick();

#endif