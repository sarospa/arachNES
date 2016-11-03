#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nes_cpu.h"
#include "nes_ppu.h"
#include "controller.h"

#define RENDER 1

#if RENDER
#include <SDL.h>
#endif

#ifdef DEBUG
const int DEBUG_LIMIT = 100;
int debug_counter;
#endif

#if RENDER
union SDL_Event event;
SDL_Rect rect;
SDL_Renderer *renderer;
SDL_Window *window;
SDL_Texture *texture = NULL;
void *pixels;
Uint8 *base;
int pitch;
#endif

const int KB = 1024;
const int PRG_ROM_PAGE = 1024 * 16;
const int CHR_ROM_PAGE = 1024 * 8;
const int STACK_PAGE = 0x100;

unsigned char* chr_rom;
unsigned char* prg_rom;

unsigned int chr_rom_size;

SDL_GameController* pad;

// TODO LIST
// VRAM mirroring
// Mappers
// OAM (sprites)
// Sound
// Palettes
// Scrolling
// Most of PPUCTRL, PPUMASK
void exit_emulator()
{
	#if RENDER
    SDL_Quit();
	#endif
	
	exit(0);
}

int main(int argc, char *argv[])
{
	#ifdef DEBUG
	debug_counter = 0;
	#endif
	
	FILE* rom = fopen(argv[1], "rb");
	fseek(rom, SEEK_SET, 0);
	unsigned char header[16];
	fread(header, 1, 16, rom);
	
	unsigned char prg_pages = header[4];
	unsigned int prg_rom_size = prg_pages * PRG_ROM_PAGE;
	prg_rom = malloc(sizeof(char) * prg_rom_size);
	
	unsigned char chr_pages = header[5];
	chr_rom_size = chr_pages * CHR_ROM_PAGE;
	chr_rom = malloc(sizeof(char) * chr_rom_size);
	
	fread(prg_rom, 1, prg_rom_size, rom);
	fread(chr_rom, 1, chr_rom_size, rom);
	
	fclose(rom);
	
	cpu_init();
	ppu_init();
	controller_init();
	
	#if RENDER
	const unsigned int TEXTURE_WIDTH = 256;
	const unsigned int TEXTURE_HEIGHT = 240;
	const unsigned int WINDOW_WIDTH = TEXTURE_WIDTH;
    const unsigned int WINDOW_HEIGHT = TEXTURE_HEIGHT;
	
	SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
    SDL_CreateWindowAndRenderer(WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, TEXTURE_WIDTH, TEXTURE_HEIGHT);
    #endif
	
	unsigned int x = 0;
    unsigned int y = 0;
	
	int num_joysticks = SDL_NumJoysticks();
	if (num_joysticks > 0)
	{
		pad = SDL_GameControllerOpen(0);
		char* mapping = SDL_GameControllerMapping(pad);
		printf("%s\n", mapping);
	}
	
	while(1)
	{
		#if RENDER
		if (x == 0)
		{
			SDL_LockTexture(texture, NULL, &pixels, &pitch);
		}
		#endif
		
		char opcode = *get_pointer_at_cpu_address(program_counter, READ);
		unsigned int cycles = run_opcode(opcode);
		
		controller_tick();
		
		// PPU runs at triple the speed of the CPU.
		// Call PPU tick three times for every CPU cycle.
		for (int i = 0; i < (cycles * 3); i++)
		{
			unsigned char render_pixel = ppu_tick();
			if (render_pixel != 255)
			{
				#if RENDER
				//printf("Rendering pixel %d at y position %d, x position %d\n", render_pixel, y, x);
				base = ((Uint8 *)pixels) + (4 * (y * TEXTURE_WIDTH + x));
                base[0] = 85 * render_pixel;
                base[1] = 85 * render_pixel;
                base[2] = 85 * render_pixel;
				base[3] = 0;
				#endif
				
				x++;
				if (x >= TEXTURE_WIDTH)
				{
					x = 0;
					y++;
				}
				
				if (y >= TEXTURE_HEIGHT)
				{
					y = 0;
					#if RENDER
					SDL_UnlockTexture(texture);
					SDL_RenderCopy(renderer, texture, NULL, NULL);
					SDL_RenderPresent(renderer);
					SDL_PollEvent(&event);
					switch(event.type)
					{
						case SDL_QUIT:
						{
							printf("Quitting.\n");
							exit_emulator();
							break;
						}
						case SDL_CONTROLLERBUTTONDOWN:
						{
							switch(event.cbutton.button)
							{
								case SDL_CONTROLLER_BUTTON_A:
								{
									controller_1_data = controller_1_data | 0b00000001;
									break;
								}
								case SDL_CONTROLLER_BUTTON_B:
								{
									controller_1_data = controller_1_data | 0b00000010;
									break;
								}
								case SDL_CONTROLLER_BUTTON_BACK:
								{
									controller_1_data = controller_1_data | 0b00000100;
									break;
								}
								case SDL_CONTROLLER_BUTTON_START:
								{
									controller_1_data = controller_1_data | 0b00001000;
									break;
								}
								case SDL_CONTROLLER_BUTTON_DPAD_UP:
								{
									controller_1_data = controller_1_data | 0b00010000;
									break;
								}
								case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
								{
									controller_1_data = controller_1_data | 0b00100000;
									break;
								}
								case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
								{
									controller_1_data = controller_1_data | 0b01000000;
									break;
								}
								case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
								{
									controller_1_data = controller_1_data | 0b10000000;
									break;
								}
							}
							printf("Current controller state: %02X\n", controller_1_data);
							break;
						}
						case SDL_CONTROLLERBUTTONUP:
						{
							switch(event.cbutton.button)
							{
								case SDL_CONTROLLER_BUTTON_A:
								{
									controller_1_data = controller_1_data & 0b11111110;
									break;
								}
								case SDL_CONTROLLER_BUTTON_B:
								{
									controller_1_data = controller_1_data & 0b11111101;
									break;
								}
								case SDL_CONTROLLER_BUTTON_BACK:
								{
									controller_1_data = controller_1_data & 0b11111011;
									break;
								}
								case SDL_CONTROLLER_BUTTON_START:
								{
									controller_1_data = controller_1_data & 0b11110111;
									break;
								}
								case SDL_CONTROLLER_BUTTON_DPAD_UP:
								{
									controller_1_data = controller_1_data & 0b11101111;
									break;
								}
								case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
								{
									controller_1_data = controller_1_data & 0b11011111;
									break;
								}
								case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
								{
									controller_1_data = controller_1_data & 0b10111111;
									break;
								}
								case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
								{
									controller_1_data = controller_1_data & 0b01111111;
									break;
								}
							}
							printf("Current controller state: %02X\n", controller_1_data);
							break;
						}
						case SDL_KEYDOWN:
						{
							if (!event.key.repeat)
							{
								switch(event.key.keysym.scancode)
								{
									case SDL_SCANCODE_Z:
									{
										controller_1_data = controller_1_data | 0b00000001;
										break;
									}
									case SDL_SCANCODE_X:
									{
										controller_1_data = controller_1_data | 0b00000010;
										break;
									}
									case SDL_SCANCODE_RSHIFT:
									case SDL_SCANCODE_LSHIFT:
									{
										controller_1_data = controller_1_data | 0b00000100;
										break;
									}
									case SDL_SCANCODE_RETURN:
									{
										controller_1_data = controller_1_data | 0b00001000;
										break;
									}
									case SDL_SCANCODE_UP:
									{
										controller_1_data = controller_1_data | 0b00010000;
										break;
									}
									case SDL_SCANCODE_DOWN:
									{
										controller_1_data = controller_1_data | 0b00100000;
										break;
									}
									case SDL_SCANCODE_LEFT:
									{
										controller_1_data = controller_1_data | 0b01000000;
										break;
									}
									case SDL_SCANCODE_RIGHT:
									{
										controller_1_data = controller_1_data | 0b10000000;
										break;
									}
								}
								printf("Current controller state: %02X\n", controller_1_data);
							}
							break;
						}
						case SDL_KEYUP:
						{
							if (!event.key.repeat)
							{
								switch(event.key.keysym.scancode)
								{
									case SDL_SCANCODE_Z:
									{
										controller_1_data = controller_1_data & 0b11111110;
										break;
									}
									case SDL_SCANCODE_X:
									{
										controller_1_data = controller_1_data & 0b11111101;
										break;
									}
									case SDL_SCANCODE_RSHIFT:
									case SDL_SCANCODE_LSHIFT:
									{
										controller_1_data = controller_1_data & 0b11111011;
										break;
									}
									case SDL_SCANCODE_RETURN:
									{
										controller_1_data = controller_1_data & 0b11110111;
										break;
									}
									case SDL_SCANCODE_UP:
									{
										controller_1_data = controller_1_data & 0b11101111;
										break;
									}
									case SDL_SCANCODE_DOWN:
									{
										controller_1_data = controller_1_data & 0b11011111;
										break;
									}
									case SDL_SCANCODE_LEFT:
									{
										controller_1_data = controller_1_data & 0b10111111;
										break;
									}
									case SDL_SCANCODE_RIGHT:
									{
										controller_1_data = controller_1_data & 0b01111111;
										break;
									}
								}
								printf("Current controller state: %02X\n", controller_1_data);
							}
							break;
						}
					}
					#endif
				}
			}
		}
	}
}