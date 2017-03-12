#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include "nes_ppu.h"
#include "nes_cpu.h"
#include "emu_nes.h"
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
// When the CPU reads PPUDATA, what it gets
// is the contents of this buffer, which is only updated
// after the read. Okay???
unsigned char ppu_data_buffer;

unsigned int register_accessed;
// Flag to indicate whether PPUSTATUS was just read. Due to
// a quirk of the PPU, if the vblank flag would be set during
// this period, it is immediately cleared.
unsigned char status_read = 0;
unsigned char reset_cycle = 1;
// FCEUX seems to skip the first vblank, so I should too in order to be
// compatible with FCEUX movies.
unsigned char vblank_skip = 1;

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
unsigned char sprite_count;
unsigned char sprite_0_selected;

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

void get_pointer_at_ppu_address(unsigned char* data, unsigned int address, unsigned char access_type)
{
	address = address & 0x3FFF;
	
	if (address <= 0x1FFF)
	{
		get_pointer_at_chr_address(data, address, access_type);
	}
	else if (address >= 0x2000 && address <= 0x3EFF)
	{
		// Pass the address to the cartridge to handle mirroring and such.
		get_pointer_at_nametable_address(data, address, access_type);
	}
	else if (address >= 0x3F00 && address <= 0x3FFF)
	{
		// Sprite background colors are mirrors of tile background colors.
		if ((address & 0b11) == 0)
		{
			address = address & 0b1111111111101111;
		}
		// Handle mirroring of 0x3F00 through 0x3F1F.
		if (access_type == READ)
		{
			*data = palette_ram[address & 0x1F];
		}
		else // access_type == WRITE
		{
			palette_ram[address & 0x1F] = *data;
		}
	}
	else
	{
		printf("PPU address %04X out of range\n", address);
		exit_emulator();
	}
}

void get_pointer_at_attribute_table_address(unsigned char* data, int address, unsigned char access_type)
{
	// This is a bit tricky. The VRAM address is structured like this:
	// yyy NN YYYYY XXXXX
	// y for fine Y-scroll, N for nametable select, Y for coarse Y-scroll, X for coarse X-scroll.
	// The attribute table address is structured like this:
	//     NN 1111 YYY XXX
	// N for nametable select, Y for the high 3 bits of the coarse Y-scroll, X for the high 3 bits of the coarse X-scroll.
	// We want to strip out yyy, keep NN, set the next 4 bits to 1, set the top 3 Y bits shifted down by 4, set the top 3 X bits shifted down by 2.
	get_pointer_at_ppu_address(data, 0x23C0 | (address & 0b110000000000) | ((address >> 4) & 0b111000) | ((address >> 2) & 0b111), access_type);
}

// Increments the coarse X-scroll in VRAM.
void increment_vram_horz()
{
	unsigned char tile_x = ((vram_address + 1) & 0b11111);
	// Move to the next nametable when the scroll value wraps.
	if (tile_x == 0)
	{
		vram_address = vram_address ^ 0b10000000000;
	}
	vram_address = (vram_address & 0b111111111100000) | tile_x;
}

// Overwrite VRAM's X-scroll bits with the ones from temp VRAM.
void reset_vram_horz()
{
	vram_address = (vram_address & 0b111101111100000) | (vram_temp & 0b10000011111);
}

// Increments the fine Y-scroll in VRAM.
void increment_vram_vert()
{
	unsigned int scroll_y = ((vram_address >> 12) + ((vram_address >> 2) & 0b11111000) + 1) & 0b11111111;
	// Move down one nametable when we hit the bottom of the screen.
	// Note that this is not the bottom of the nametable, row 31, but row 29. Thus, we should check
	// if the row hits 30 when wrapping, and set it to the top of the next nametable.
	// Wrapping inside 'out of bounds' rows DOES NOT move to the next nametable.
	if (scroll_y == 0b11110000)
	{
		vram_address = vram_address ^ 0b100000000000;
		scroll_y = 0;
	}
	vram_address = (vram_address & 0b110000011111) | ((scroll_y << 12) & 0b111000000000000) | ((scroll_y << 2) & 0b1111100000);
}

// Overwrite VRAM's Y-scroll bits with the ones from temp VRAM.
void reset_vram_vert()
{
	vram_address = (vram_address & 0b10000011111) | (vram_temp & 0b111101111100000);
}

// Notifies the PPU that the CPU accessed the PPU bus.
// If the PPU needs to write, it should do it here.
// If the PPU needs to read, it should save the register address
// to register_accessed and look at it during ppu_tick so the CPU
// has time to write to the PPU bus.
void access_ppu_register(unsigned char* data, unsigned int ppu_register, unsigned char access_type)
{
	if (access_type == READ)
	{
		switch(ppu_register)
		{
			// PPUSTATUS
			case 0x2002:
			{
				if (access_type == READ)
				{
					write_toggle = 0;
					ppu_bus = ppu_status;
					// Clear the vblank flag after PPUSTATUS read.
					ppu_status = ppu_status & 0b01111111;
					nmi_occurred = nmi_occurred & 0b10;
					// Right now I'm assuming that the vblank can be cleared prematurely as long as
					// the CPU is 'pointing at' PPUSTATUS. It doesn't appear to work otherwise.
					// We'll see in due time if I'm proven wrong.
					status_read = 3;
				}
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
				if (access_type == READ)
				{
					// Reading PPUDATA works via an utterly bizarre scheme in which the data
					// actually read is from a buffer that is updated with the latest value only
					// after the read. Except for palettes, which are read directly, except
					// the buffer is still updated, but with the nametable byte that /would/
					// be there if the address wasn't a palette.
					if (vram_address <= 0x3EFF)
					{
						ppu_bus = ppu_data_buffer;
					}
					else
					{
						get_pointer_at_ppu_address(&ppu_bus, vram_address, READ);
					}
					get_pointer_at_ppu_address(&ppu_data_buffer, vram_address, READ);
					
					// Increment the vram address.
					unsigned char render_disable = (ppu_mask & 0b00011000) == 0;
					if ((!render_disable) && ((scanline < 240) || (scanline == 261)))
					{
						// During rendering, the coarse X and fine Y are both incremented.
						increment_vram_horz();
						increment_vram_vert();
					}
					else
					{
						// Check VRAM address increment flag
						if ((ppu_control & 0b00000100) == 0b00000100)
						{
							vram_address = (vram_address + 32) & 0x3FFF;
						}
						else
						{
							vram_address = (vram_address + 1) & 0x3FFF;
						}
					}
				}
				else
				{
					register_accessed = ppu_register;
				}
				break;
			}
			default:
			{
				register_accessed = ppu_register;
				break;
			}
		}
		*data = ppu_bus;
	}
	else // access_type == WRITE
	{
		ppu_bus = *data;
		switch(ppu_register)
		{
			// PPUCTRL
			case 0x2000:
			{
				ppu_control = ppu_bus;
				// Set the nametable select bits to the temp vram address.
				vram_temp = (vram_temp & 0b1111001111111111) | ((ppu_control & 0b11) << 10);
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
					vram_temp = (vram_temp & 0x00FF) | ((ppu_bus & 0x3F) << 8);
					write_toggle = 1;
				}
				break;
			}
			// PPUDATA
			case 0x2007:
			{
				get_pointer_at_ppu_address(&ppu_bus, vram_address, WRITE);
				
				// Increment the vram address.
				unsigned char render_disable = (ppu_mask & 0b00011000) == 0;
				if ((!render_disable) && ((scanline < 240) || (scanline == 261)))
				{
					// During rendering, the coarse X and fine Y are both incremented.
					increment_vram_horz();
					increment_vram_vert();
				}
				else
				{
					// Check VRAM address increment flag
					if ((ppu_control & 0b00000100) == 0b00000100)
					{
						vram_address = (vram_address + 32) & 0x3FFF;
					}
					else
					{
						vram_address = (vram_address + 1) & 0x3FFF;
					}
				}
				break;
			}
		}
		register_accessed = 0;
	}
}

void load_render_registers()
{
	unsigned fine_y_scroll = (vram_address >> 12) & 0b111;
	unsigned char pattern_byte;
	get_pointer_at_nametable_address(&pattern_byte, vram_address & 0b111111111111, READ);
	// An address in the pattern table is encoded thus:
	// 0HRRRR CCCCPTTT
	// H is which half of the sprite table.
	// P is which bit plane we're in. We need both the low and the high one.
	// TTT is the fine y offset. We get that from which scanline is being drawn.
	// It seems that the part stored in the nametable is RRRR CCCC, indicating the tile row and column.
	unsigned int pattern_address = ((ppu_control & 0b10000) << 8) | (pattern_byte * 0b10000) | fine_y_scroll;
	// Write the low byte from the pattern table to the low byte of the low bitmap register.
	unsigned char low_pattern_data;
	get_pointer_at_ppu_address(&low_pattern_data, pattern_address, READ);
	bitmap_register_low = (bitmap_register_low & 0xFF00) | low_pattern_data;
	// Write the high byte from the pattern table to the low byte of the high bitmap register.
	unsigned char high_pattern_data;
	get_pointer_at_ppu_address(&high_pattern_data, pattern_address | 0b1000, READ);
	bitmap_register_high = (bitmap_register_high & 0xFF00) | high_pattern_data;
	unsigned char attribute_byte;
	get_pointer_at_attribute_table_address(&attribute_byte, vram_address & 0b111111111111, READ);
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
	sprite_0_selected = 0;
	
	for (int i = 0; i < 0x40; i++)
	{
		secondary_oam[i] = 0xFF;
	}
	sprite_count = 0;
	for (int i = 0; (i < 0x40) && (sprite_count < 0x8); i++)
	{
		unsigned char sprite_height;
		// Check for 8x16 sprite mode
		if ((ppu_control & 0b00100000) == 0b00100000)
		{
			sprite_height = 16;
		}
		else
		{
			sprite_height = 8;
		}
		
		secondary_oam[sprite_count * 4] = oam[i * 4];
		// If the sprite falls on the scanline, load it into the secondary OAM for rendering.
		if ((secondary_oam[sprite_count * 4] <= scanline) && ((secondary_oam[sprite_count * 4] + sprite_height) > scanline))
		{
			if (i == 0)
			{
				sprite_0_selected = 1;
			}
			secondary_oam[(sprite_count * 4) + 1] = oam[(i * 4) + 1];
			secondary_oam[(sprite_count * 4) + 2] = oam[(i * 4) + 2];
			secondary_oam[(sprite_count * 4) + 3] = oam[(i * 4) + 3];
			sprite_count++;
		}
	}
}

// Loads sprites from secondary OAM into rendering buffers.
void load_sprites()
{
	// 8x16 sprite flag.
	// Tall sprites use the top 7 bits for the top tile number of their sprite,
	// and bit 0 for their pattern table select. PPUCTRL's table select is ignored.
	unsigned char tall_sprites = (ppu_control & 0b00100000) == 0b00100000;
	
	for (int i = 0; i < 0x8; i++)
	{
		unsigned char fine_y = (scanline + 1) - (secondary_oam[i * 4] + 1);
		unsigned char sprite_attribute_byte = secondary_oam[(i * 4) + 2];
		unsigned char tile_number = secondary_oam[(i * 4) + 1];
		unsigned char flip_vertical = (sprite_attribute_byte & 0b10000000) == 0b10000000;
		unsigned char pattern_table_select = (ppu_control & 0b1000) == 0b1000;
		if (tall_sprites)
		{
			pattern_table_select = tile_number & 0b1;
			tile_number = (tile_number & 0b11111110) | ((fine_y >> 3) & 0b1);
			fine_y = fine_y & 0b111;
		}
		
		if (flip_vertical)
		{
			fine_y = fine_y ^ 0b111;
			if (tall_sprites)
			{
				tile_number = tile_number ^ 0b1;
			}
		}
		// An address in the pattern table is encoded thus:
		// 0HRRRR CCCCPTTT
		// H is which half of the sprite table.
		// P is which bit plane we're in. We need both the low and the high one.
		// TTT is the fine y offset. We get that from which scanline is being drawn.
		// RRRR indicates row, and CCCC indicates column.
		unsigned int pattern_address = (pattern_table_select << 12) | (tile_number << 4) | fine_y;
		unsigned char sprite_bitmap_low;
		get_pointer_at_ppu_address(&sprite_bitmap_low, pattern_address, READ);
		unsigned char sprite_bitmap_high;
		get_pointer_at_ppu_address(&sprite_bitmap_high, pattern_address | 0b1000, READ);
		
		// Check for horizontal flip attribute.
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

void ppu_save_state(FILE* save_file)
{
	fwrite(&ppu_bus, sizeof(char), 1, save_file);
	fwrite(&vram_address, sizeof(int), 1, save_file);
	fwrite(&vram_temp, sizeof(int), 1, save_file);
	fwrite(&fine_x_scroll, sizeof(char), 1, save_file);
	fwrite(&ppu_control, sizeof(char), 1, save_file);
	fwrite(&ppu_mask, sizeof(char), 1, save_file);
	fwrite(&ppu_status, sizeof(char), 1, save_file);
	fwrite(&oam_address, sizeof(char), 1, save_file);
	fwrite(&ppu_data_buffer, sizeof(char), 1, save_file);
	fwrite(&write_toggle, sizeof(char), 1, save_file);
	fwrite(&odd_frame, sizeof(char), 1, save_file);
	fwrite(&scanline, sizeof(int), 1, save_file);
	fwrite(&scan_pixel, sizeof(int), 1, save_file);
	fwrite(&bitmap_register_low, sizeof(int), 1, save_file);
	fwrite(&bitmap_register_high, sizeof(int), 1, save_file);
	fwrite(&palette_register_low, sizeof(char), 1, save_file);
	fwrite(&palette_register_high, sizeof(char), 1, save_file);
	fwrite(&palette_latch, sizeof(char), 1, save_file);
	fwrite(&sprite_count, sizeof(char), 1, save_file);
	fwrite(&sprite_0_selected, sizeof(char), 1, save_file);
	fwrite(&pending_interrupt, sizeof(char), 1, save_file);
	fwrite(&nmi_occurred, sizeof(char), 1, save_file);
	fwrite(&nmi_output, sizeof(char), 1, save_file);
	fwrite(&status_read, sizeof(char), 1, save_file);
	fwrite(&reset_cycle, sizeof(char), 1, save_file);
	
	fwrite(ppu_ram, sizeof(char), 0x800, save_file);
	fwrite(palette_ram, sizeof(char), 0x20, save_file);
	fwrite(oam, sizeof(char), 0x100, save_file);
	fwrite(secondary_oam, sizeof(char), 0x40, save_file);
	fwrite(sprite_bitmaps_low, sizeof(char), 0x8, save_file);
	fwrite(sprite_bitmaps_high, sizeof(char), 0x8, save_file);
	fwrite(sprite_attributes, sizeof(char), 0x8, save_file);
	fwrite(sprite_x_positions, sizeof(char), 0x8, save_file);
}

void ppu_load_state(FILE* save_file)
{
	fread(&ppu_bus, sizeof(char), 1, save_file);
	fread(&vram_address, sizeof(int), 1, save_file);
	fread(&vram_temp, sizeof(int), 1, save_file);
	fread(&fine_x_scroll, sizeof(char), 1, save_file);
	fread(&ppu_control, sizeof(char), 1, save_file);
	fread(&ppu_mask, sizeof(char), 1, save_file);
	fread(&ppu_status, sizeof(char), 1, save_file);
	fread(&oam_address, sizeof(char), 1, save_file);
	fread(&ppu_data_buffer, sizeof(char), 1, save_file);
	fread(&write_toggle, sizeof(char), 1, save_file);
	fread(&odd_frame, sizeof(char), 1, save_file);
	fread(&scanline, sizeof(int), 1, save_file);
	fread(&scan_pixel, sizeof(int), 1, save_file);
	fread(&bitmap_register_low, sizeof(int), 1, save_file);
	fread(&bitmap_register_high, sizeof(int), 1, save_file);
	fread(&palette_register_low, sizeof(char), 1, save_file);
	fread(&palette_register_high, sizeof(char), 1, save_file);
	fread(&palette_latch, sizeof(char), 1, save_file);
	fread(&sprite_count, sizeof(char), 1, save_file);
	fread(&sprite_0_selected, sizeof(char), 1, save_file);
	fread(&pending_interrupt, sizeof(char), 1, save_file);
	fread(&nmi_occurred, sizeof(char), 1, save_file);
	fread(&nmi_output, sizeof(char), 1, save_file);
	fread(&status_read, sizeof(char), 1, save_file);
	fread(&reset_cycle, sizeof(char), 1, save_file);
	
	fread(ppu_ram, sizeof(char), 0x800, save_file);
	fread(palette_ram, sizeof(char), 0x20, save_file);
	fread(oam, sizeof(char), 0x100, save_file);
	fread(secondary_oam, sizeof(char), 0x40, save_file);
	fread(sprite_bitmaps_low, sizeof(char), 0x8, save_file);
	fread(sprite_bitmaps_high, sizeof(char), 0x8, save_file);
	fread(sprite_attributes, sizeof(char), 0x8, save_file);
	fread(sprite_x_positions, sizeof(char), 0x8, save_file);
}

// Returns the pixel data to be rendered. 255 indicates no render.
// This will probably have to be made a bit more complex as more parts of the PPU are implemented.
unsigned char ppu_tick()
{
	unsigned char pixel_data = 255;
	
	unsigned char render_disable = (ppu_mask & 0b00011000) == 0;
	unsigned char background_enable = (ppu_mask & 0b1000) == 0b1000;
	unsigned char sprite_enable = (ppu_mask & 0b10000) == 0b10000;
	
	// pre-render scanline
	if (scanline == 261)
	{
		if (scan_pixel == 1)
		{
			// Clear the vblank and sprite 0 hit flags.
			ppu_status = ppu_status & 0b00111111;
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
				get_pointer_at_ppu_address(&pixel_data, 0x3F00, READ);
			}
		}
		else
		{
			// Rendering cycles
			if ((scan_pixel > 0) && (scan_pixel <= 256))
			{
				unsigned char show_left_sprites = 1;
				if (scan_pixel <= 8)
				{
					background_enable = background_enable & ((ppu_mask >> 1) & 0b1);
					show_left_sprites = (ppu_mask >> 2) & 0b1;
				}
				// Default to backdrop color if nothing else gets rendered
				get_pointer_at_ppu_address(&pixel_data, 0x3F00, READ);
				// Load the next tile every 8 cycles.
				if (((scan_pixel % 8) == 1) && (scan_pixel > 1))
				{
					load_render_registers();
				}
				// It seems that fine x scroll selects bits in the opposite order of my implementation.
				// I still don't understand exactly how the rendering pipeline works. But this should work fine.
				unsigned char fixed_fine_x_scroll = 7 - fine_x_scroll;
				unsigned char background_bitmap_palette =
					  ((bitmap_register_low >> (8 + fixed_fine_x_scroll)) & 0b1)
					| ((bitmap_register_high >> (7 + fixed_fine_x_scroll)) & 0b10);
				unsigned char palette_address = 0;
				// All palettes show the same default background color.
				if (background_bitmap_palette > 0)
				{
					palette_address = background_bitmap_palette
						| (((palette_register_low >> fixed_fine_x_scroll) << 2) & 0b100)
						| (((palette_register_high >> fixed_fine_x_scroll) << 3) & 0b1000);
				}
				unsigned char background_pixel;
				get_pointer_at_ppu_address(&background_pixel, 0x3F00 + palette_address, READ);
				if (background_enable)
				{
					pixel_data = background_pixel;
				}
				bitmap_register_low = (bitmap_register_low << 1) & 0xFFFF;
				bitmap_register_high = (bitmap_register_high << 1) & 0xFFFF;
				palette_register_low = ((palette_register_low << 1) & 0xFF) | (palette_latch & 0b1);
				palette_register_high = ((palette_register_high << 1) & 0xFF) | ((palette_latch & 0b10) >> 1);
				
				if (sprite_enable)
				{
					// Evaluate in reverse order to make sure lower index sprites are drawn on top.
					for (int i = (sprite_count - 1); i >= 0; i--)
					{
						if (sprite_x_positions[i] > 0)
						{
							sprite_x_positions[i]--;
						}
						else
						{
							unsigned char sprite_priority = (sprite_attributes[i] >> 5) & 0b1;
							unsigned char sprite_palette = ((sprite_bitmaps_low[i] >> 7) & 0b1) | ((sprite_bitmaps_high[i] >> 6) & 0b10);
							// Only load in sprite pixel if it's non-transparent and it isn't being masked in the leftmost column.
							if ((sprite_palette > 0) && show_left_sprites)
							{
								// Check for sprite 0 hit
								if (background_enable && (background_bitmap_palette > 0) && (i == 0) && sprite_0_selected)
								{
									ppu_status = ppu_status | 0b01000000;
								}
								
								if (background_enable && (background_bitmap_palette > 0) && (sprite_priority == 1))
								{
									pixel_data = background_pixel;
								}
								else
								{
									sprite_palette = sprite_palette | ((sprite_attributes[i] & 0b11) << 2);
									get_pointer_at_ppu_address(&pixel_data, 0x3F10 + sprite_palette, READ);
								}
							}
							sprite_bitmaps_low[i] = (sprite_bitmaps_low[i] << 1) & 0xFF;
							sprite_bitmaps_high[i] = (sprite_bitmaps_high[i] << 1) & 0xFF;
						}
					}
				}
				// Only the lower six pixels contain color data. Garbage data should be truncated
				// to six bits, so it can't be erroneously read as a 255 'no render' pixel.
				pixel_data = (pixel_data & 0b111111);
			}
			else if (scan_pixel == 257)
			{
				reset_vram_horz();
				increment_vram_vert();
				// Evaluating sprites here for now, in preparation for the next scanline.
				evaluate_sprites();
			}
			else if (scan_pixel == 284)
			{
				load_sprites();
			}
			else if (scan_pixel == 321)
			{
				// Load the first tile of the next scanline.
				load_render_registers();
			}
			else if (scan_pixel == 329)
			{
				// Shift the first tile to be rendered and load the second tile of the next scanline.
				// This probably isn't how the NES really handles it, but it should work?
				for (int i = 0; i < 8; i++)
				{
					bitmap_register_low = (bitmap_register_low << 1) & 0xFFFF;
					bitmap_register_high = (bitmap_register_high << 1) & 0xFFFF;
					palette_register_low = ((palette_register_low << 1) & 0xFF) | (palette_latch & 0b1);
					palette_register_high = ((palette_register_high << 1) & 0xFF) | ((palette_latch & 0b10) >> 1);
				}
				load_render_registers();
			}
		}
	}
	// Start of vblank.
	else if (scanline == 241)
	{
		if (scan_pixel == 1)
		{
			if (reset_cycle)
			{
				reset_cycle = 0;
			}
			else
			{
				
				if (!vblank_skip)
				{
					ppu_status = ppu_status | 0b10000000;
					nmi_occurred = nmi_occurred | 0b01;
				}
				else
				{
					vblank_skip = 0;
				}
			}
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
		pending_interrupt++;
		interrupt_type = NMI;
	}
	
	// Shift the NMI bits into the 'previous frame' bits.
	nmi_occurred = ((nmi_occurred & 0b1) << 1) | (nmi_occurred & 0b1);
	nmi_output = ((nmi_output & 0b1) << 1) | (nmi_output & 0b1);
	
	if (status_read > 0)
	{
		status_read--;
	}
	
	return pixel_data;
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
	ppu_data_buffer = 0;
	register_accessed = 0;
	
	write_toggle = 0;
	odd_frame = 0;
	
	scanline = 240;
	scan_pixel = 0;
	
	nmi_occurred = 0;
	
	ppu_ram = malloc(sizeof(char) * 0x800);
	for (int i = 0; i < 0x800; i++)
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
	sprite_count = 0;
	sprite_0_selected = 0;
}