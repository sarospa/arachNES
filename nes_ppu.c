#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include "nes_ppu.h"
#include "nes_cpu.h"
#include "cartridge.h"

unsigned char* ppu_ram;
unsigned char* palette_ram;
unsigned char* oam;
unsigned char* secondary_oam;

unsigned char ppu_bus;

unsigned int vram_address;
unsigned int vram_temp;
unsigned char fine_x_scroll;

// PPU registers
unsigned char ppu_control;
unsigned char ppu_mask;
unsigned char ppu_status;
unsigned char oam_address;

unsigned int register_accessed;

// Controls the write mode of PPUSCROLL and PPUADDRESS
unsigned char write_toggle;
unsigned char odd_frame;

unsigned int scanline;
unsigned int scan_pixel;

unsigned int bitmap_register_low;
unsigned int bitmap_register_high;
unsigned char palette_register_low;
unsigned char palette_register_high;
unsigned char palette_latch;
unsigned char* sprite_bitmaps_low;
unsigned char* sprite_bitmaps_high;
unsigned char* sprite_attributes;
unsigned char* sprite_x_positions;

unsigned char pending_nmi;
// Treated as two-bit shift registers to detect edges.
unsigned char nmi_occurred;
unsigned char nmi_output;

// Credit to sth at Stack Overflow for this one.
unsigned char reverse_byte(unsigned char byte)
{
   byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
   byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
   byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
   return byte;
}

unsigned char* get_pointer_at_ppu_address(unsigned int address)
{
	if (address <= 0x1FFF)
	{
		return get_pointer_at_chr_address(address);
	}
	else if (address >= 0x2000 && address <= 0x3EFF)
	{
		// Pass the address to the cartridge to handle mirroring and such.
		return get_pointer_at_nametable_address(address);
	}
	else if (address >= 0x3F00 && address <= 0x3FFF)
	{
		// Handle mirroring of 0x3F00 through 0x3F1F.
		return &palette_ram[address & 0x1F];
	}
	else
	{
		printf("PPU address %04X out of range\n", address);
		exit_emulator();
		return NULL;
	}
}

unsigned char* get_pointer_at_attribute_table_address(int address)
{
	// This is a bit tricky. The VRAM address is structured like this:
	// yyy NN YYYYY XXXXX
	// y for fine Y-scroll, N for nametable select, Y for coarse Y-scroll, X for coarse X-scroll.
	// The attribute table address is structured like this:
	//     NN 1111 YYY XXX
	// N for nametable select, Y for the high 3 bits of the coarse Y-scroll, X for the high 3 bits of the coarse X-scroll.
	// We want to strip out yyy, keep NN, set the next 4 bits to 1, set the top 3 Y bits shifted down by 4, set the top 3 X bits shifted down by 2.
	return get_pointer_at_ppu_address(0x23C0 | (address & 0b110000000000) | ((address >> 4) & 0b111000) | ((address >> 2) & 0b111));
}

// Notifies the PPU that the CPU accessed the PPU bus.
// If the PPU needs to write, it should do it here.
// If the PPU needs to read, it should save the register address
// to register_accessed and look at it during ppu_tick so the CPU
// has time to write to the PPU bus.
void notify_ppu(unsigned int ppu_register, unsigned char access_type)
{
	switch(ppu_register)
	{
		// PPUSTATUS
		case 0x2002:
		{
			write_toggle = 0;
			ppu_bus = ppu_status;
			// Clear the vblank flag after PPUSTATUS read.
			ppu_status = ppu_status & 0b01111111;
			break;
		}
		// OAMADDR
		case 0x2003:
		{
			ppu_bus = oam_address;
			register_accessed = ppu_register;
			break;
		}
		// OAMDATA
		case 0x2004:
		{
			ppu_bus = oam[oam_address];
			if (access_type == WRITE)
			{
				register_accessed = ppu_register;
			}
			break;
		}
		// PPUDATA
		case 0x2007:
		{
			ppu_bus = *get_pointer_at_ppu_address(vram_address); 
			register_accessed = ppu_register;
			break;
		}
		default:
		{
			register_accessed = ppu_register;
			break;
		}
	}
}

void notify_ppu_write(unsigned int ppu_register)
{
	switch(ppu_register)
	{
		// PPUCTRL
		case 0x2000:
		{
			ppu_control = ppu_bus;
			// Track current state of nmi output on its low bit.
			if ((ppu_control & 0b10000000) == 0b10000000)
			{
				nmi_output = nmi_output | 0b01;
			}
			else
			{
				nmi_output = nmi_output & 0b10;
			}
			break;
		}
		// PPUMASK
		case 0x2001:
		{
			ppu_mask = ppu_bus;
			break;
		}
		// OAMADDR
		case 0x2003:
		{
			oam_address = ppu_bus;
			break;
		}
		// OAMDATA
		case 0x2004:
		{
			oam[oam_address] = ppu_bus;
			oam_address++;
			break;
		}
		// PPUSCROLL
		case 0x2005:
		{
			if (write_toggle) // == 1
			{
				// Write to the Y-scroll.
				vram_temp = (vram_temp & 0b000110000011111) | ((ppu_bus & 0b111) << 12) | ((ppu_bus & 0b11111000) << 2);
				write_toggle = 0;
			}
			else
			{
				// Write to the X-scroll.
				fine_x_scroll = ppu_bus & 0b111;
				vram_temp = (vram_temp & 0b1111111111100000) | ((ppu_bus >> 3) & 0b11111);
				write_toggle = 1;
			}
			break;
		}
		// PPUADDR
		case 0x2006:
		{
			if (write_toggle) // == 1
			{
				// Write the ppu bus data to the low byte.
				vram_temp = (vram_temp & 0xFF00) | ppu_bus;
				vram_address = vram_temp;
				write_toggle = 0;
			}
			else
			{
				// Write the ppu bus data to the high byte.
				vram_temp = (vram_temp & 0x00FF) | (ppu_bus * 0x100);
				write_toggle = 1;
			}
			break;
		}
		// PPUDATA
		case 0x2007:
		{
			unsigned char* address_byte = get_pointer_at_ppu_address(vram_address);
			*address_byte = ppu_bus;
			// Check VRAM address increment flag
			if ((ppu_control & 0b00000100) == 0b00000100)
			{
				vram_address += 32;
			}
			else
			{
				vram_address++;
			}
			break;
		}
	}
	register_accessed = 0;
}

void ppu_init()
{
	vram_address = 0;
	vram_temp = 0;
	fine_x_scroll = 0;
	
	ppu_control = 0;
	ppu_mask = 0;
	ppu_status = 0;
	oam_address = 0;
	register_accessed = 0;
	
	write_toggle = 0;
	odd_frame = 0;
	
	scanline = 261;
	scan_pixel = 0;
	
	nmi_occurred = 0;
	
	ppu_ram = malloc(sizeof(char) * KB * 2);
	for (int i = 0; i < (KB * 2); i++)
	{
		ppu_ram[i] = 0;
	}
	palette_ram = malloc(sizeof(char) * 0x20);
	for (int i = 0; i < 0x20; i++)
	{
		palette_ram[i] = 0;
	}
	oam = malloc(sizeof(char) * 0x100);
	secondary_oam = malloc(sizeof(char) * 0x40);
	sprite_bitmaps_low = malloc(sizeof(char) * 0x8);
	sprite_bitmaps_high = malloc(sizeof(char) * 0x8);
	sprite_attributes = malloc(sizeof(char) * 0x8);
	sprite_x_positions = malloc(sizeof(char) * 0x8);
}

// Increments the coarse X-scroll in VRAM.
void increment_vram_horz()
{
	unsigned char tile_x = ((vram_address + 1) & 0b11111);
	vram_address = (vram_address & 0b111001111100000) | ((ppu_control & 0b11) << 10) | tile_x;
}

// Overwrite VRAM's X-scroll bits with the ones from temp VRAM.
void reset_vram_horz()
{
	vram_address = (vram_address & 0b111001111100000) | (vram_temp & 0b11111) | ((ppu_control & 0b11) << 10);
}

// Increments the fine Y-scroll in VRAM.
void increment_vram_vert()
{
	unsigned int scroll_y = (vram_address >> 12) + ((vram_address >> 2) & 0b11111000) + 1;
	vram_address = (vram_address & 0b110000011111) | ((scroll_y << 12) & 0b111000000000000) | ((scroll_y << 2) & 0b1111100000); 
}

// Overwrite VRAM's Y-scroll bits with the ones from temp VRAM.
void reset_vram_vert()
{
	vram_address = (vram_address & 0b110000011111) | (vram_temp & 0b111001111100000);
}

void load_render_registers()
{
	unsigned fine_y_scroll = (vram_address >> 12) & 0b111;
	unsigned char pattern_byte = *get_pointer_at_nametable_address(vram_address & 0b111111111111);
	// An address in the pattern table is encoded thus:
	// 0HRRRR CCCCPTTT
	// H is which half of the sprite table.
	// P is which bit plane we're in. We need both the low and the high one.
	// TTT is the fine y offset. We get that from which scanline is being drawn.
	// It seems that the part stored in the nametable is RRRR CCCC, indicating the tile row and column.
	unsigned int pattern_address = ((ppu_control & 0b10000) << 8) | (pattern_byte * 0b10000) | fine_y_scroll;
	// Write the low byte from the pattern table to the low byte of the low bitmap register.
	bitmap_register_low = (bitmap_register_low & 0xFF00) | *get_pointer_at_ppu_address(pattern_address);
	// Write the high byte from the pattern table to the low byte of the high bitmap register.
	bitmap_register_high = (bitmap_register_high & 0xFF00) | *get_pointer_at_ppu_address(pattern_address | 0b1000);
	unsigned char attribute_byte = *get_pointer_at_attribute_table_address(vram_address & 0b111111111111);
	unsigned char attribute_tile_select = ((vram_address >> 1) & 0b1) + ((vram_address >> 5) & 0b10);
	palette_latch = (attribute_byte >> (attribute_tile_select * 2)) & 0b11;
	
	increment_vram_horz();
}

// Checks which sprites should be loaded into secondary OAM for rendering.
// Right now I'm going to evaluate sprites all in one cycle.
// This isn't realistic to how the NES really does it, but it's easier, and mostly shouldn't have
// any noticable effects (this might affect some games, though).
void evaluate_sprites()
{
	for (int i = 0; i < 0x40; i++)
	{
		secondary_oam[i] = 0xFF;
	}
	unsigned char sec_oam_count = 0;
	for (int i = 0; (i < 0x40) && (sec_oam_count < 0x8); i++)
	{
		secondary_oam[sec_oam_count * 4] = oam[i * 4];
		// If the sprite falls on the scanline, load it into the secondary OAM for rendering.
		if ((secondary_oam[sec_oam_count * 4] <= scanline) && ((secondary_oam[sec_oam_count * 4] + 8U) > scanline))
		{
			secondary_oam[(sec_oam_count * 4) + 1] = oam[(i * 4) + 1];
			secondary_oam[(sec_oam_count * 4) + 2] = oam[(i * 4) + 2];
			secondary_oam[(sec_oam_count * 4) + 3] = oam[(i * 4) + 3];
			sec_oam_count++;
		}
	}
}

// Loads sprites from secondary OAM into rendering buffers.
void load_sprites()
{
	for (int i = 0; i < 0x8; i++)
	{
		unsigned char fine_y = scanline - (secondary_oam[i * 4] + 1);
		unsigned char sprite_attribute_byte = secondary_oam[(i * 4) + 2];
		// Flip vertical
		if ((sprite_attribute_byte & 0b10000000) == 0b10000000)
		{
			fine_y = fine_y ^ 0b111;
		}
		unsigned char tile_number = secondary_oam[(i * 4) + 1];
		// An address in the pattern table is encoded thus:
		// 0HRRRR CCCCPTTT
		// H is which half of the sprite table.
		// P is which bit plane we're in. We need both the low and the high one.
		// TTT is the fine y offset. We get that from which scanline is being drawn.
		// RRRR indicates row, and CCCC indicates column.
		unsigned int pattern_address = ((ppu_control & 0b1000) << 9) | (tile_number << 4) | fine_y;
		unsigned char sprite_bitmap_low = *get_pointer_at_ppu_address(pattern_address);
		unsigned char sprite_bitmap_high = *get_pointer_at_ppu_address(pattern_address | 0b1000);
		
		if ((sprite_attribute_byte & 0b1000000) == 0b1000000)
		{
			sprite_bitmap_low = reverse_byte(sprite_bitmap_low);
			sprite_bitmap_high = reverse_byte(sprite_bitmap_high);
		}
		sprite_bitmaps_low[i] = sprite_bitmap_low;
		sprite_bitmaps_high[i] = sprite_bitmap_high;
		sprite_attributes[i] = sprite_attribute_byte;
		sprite_x_positions[i] = secondary_oam[(i * 4) + 3];
	}
}

// Returns the pixel data to be rendered. 255 indicates no render.
// This will probably have to be made a bit more complex as more parts of the PPU are implemented.
unsigned char ppu_tick()
{
	unsigned pixel_data = 255;
	
	notify_ppu_write(register_accessed);
	
	unsigned char render_disable = (ppu_mask & 0b00011000) == 0;
	
	// pre-render scanline
	if (scanline == 261)
	{
		if (scan_pixel == 1)
		{
			// Clear the vblank flag.
			ppu_status = ppu_status & 0b01111111;
			nmi_occurred = nmi_occurred & 0b10;
		}
		else if (scan_pixel == 257 && (!render_disable))
		{
			reset_vram_horz();
			increment_vram_vert();
		}
		else if (scan_pixel == 279 && (!render_disable))
		{
			reset_vram_vert();
		}
		// Load the first two tiles of the next scanline.
		else if ((scan_pixel >= 320) && (scan_pixel <= 336) && ((scan_pixel % 8) == 1) && (!render_disable))
		{
			load_render_registers();
		}
	}
	// visible scanline
	else if (scanline < 240)
	{
		// Send up a black pixel during visible pixels to keep rendering aligned properly.
		// Should probably find a better way to make sure that can't actually happen.
		if (render_disable)
		{
			if ((scan_pixel > 0) && (scan_pixel <= 256))
			{
				pixel_data = 0x0F;
			}
		}
		else
		{
			// Load sprites into buffers here.
			if (scan_pixel == 0)
			{
				load_sprites();
			}
			// Rendering cycles
			else if ((scan_pixel > 0) && (scan_pixel <= 256))
			{
				// Load the next tile every 8 cycles.
				if (((scan_pixel % 8) == 1) && (scan_pixel > 1))
				{
					load_render_registers();
				}
				// Eventually we'll have to implement fine scrolling to select which bit we want to use.
				// Right now, always selecting bit 15 for no scrolling.
				unsigned char palette_address = ((bitmap_register_low >> 15) & 0b1) | ((bitmap_register_high >> 14) & 0b10) | ((palette_register_low >> 5) & 0b100) | ((palette_register_high >> 4) & 0b1000);
				pixel_data = *get_pointer_at_ppu_address(0x3F00 + palette_address);
				bitmap_register_low = (bitmap_register_low << 1) & 0xFFFF;
				bitmap_register_high = (bitmap_register_high << 1) & 0xFFFF;
				palette_register_low = ((palette_register_low << 1) & 0xFF) | (palette_latch & 0b1);
				palette_register_high = ((palette_register_high << 1) & 0xFF) | ((palette_latch & 0b10) >> 1);
				
				// Evaluate in reverse order to make sure lower index sprites are drawn on top.
				for (int i = 7; i >= 0; i--)
				{
					if (sprite_x_positions[i] > 0)
					{
						sprite_x_positions[i]--;
					}
					else
					{
						unsigned char sprite_palette = ((sprite_bitmaps_low[i] >> 7) & 0b1) | ((sprite_bitmaps_high[i] >> 6) & 0b10);
						// Only load in sprite pixel if it's non-transparent.
						if (sprite_palette > 0)
						{
							sprite_palette = sprite_palette | ((sprite_attributes[i] & 0b11) << 2);
							pixel_data = *get_pointer_at_ppu_address(0x3F10 + sprite_palette);
						}
						sprite_bitmaps_low[i] = (sprite_bitmaps_low[i] << 1) & 0xFF;
						sprite_bitmaps_high[i] = (sprite_bitmaps_high[i] << 1) & 0xFF;
					}
				}
			}
			else if (scan_pixel == 257)
			{
				reset_vram_horz();
				increment_vram_vert();
				// Evaluating sprites here for now, in preparation for the next scanline.
				evaluate_sprites();
			}
			else if ((scan_pixel >= 320) && (scan_pixel <= 336))
			{
				// Load the first two tiles of the next scanline.
				if ((scan_pixel % 8) == 1)
				{
					load_render_registers();
				}
			}
		}
	}
	// Start of vblank.
	else if (scanline == 241)
	{
		if (scan_pixel == 1)
		{
			// Set vblank
			ppu_status = ppu_status | 0b10000000;
			nmi_occurred = nmi_occurred | 0b01;
		}
	}
	
	scan_pixel++;
	// Jump to the next scanline once we hit the end. The dummy scanline ends one frame early on odd frames.
	if (scan_pixel > 340 || ((scanline == 261) && (scan_pixel == 339) && odd_frame))
	{
		scanline++;
		scan_pixel = 0;
	}
	
	if (scanline > 261)
	{
		scanline = 0;
		odd_frame = !odd_frame;
	}
	
	if (((nmi_occurred & 0b1) == 1) && ((nmi_output & 0b1) == 1) && ((nmi_occurred == 0b01) || (nmi_output == 0b01)))
	{
		pending_nmi = 1;
	}
	
	// Shift the NMI bits into the 'previous frame' bits.
	nmi_occurred = ((nmi_occurred & 0b1) << 1) | (nmi_occurred & 0b1);
	nmi_output = ((nmi_output & 0b1) << 1) | (nmi_output & 0b1);
	
	return pixel_data;
}