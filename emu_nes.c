#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "nes_cpu.h"
#include "nes_ppu.h"
#include "controller.h"
#include "cartridge.h"

#define RENDER 1

#if RENDER
#include <SDL.h>

union SDL_Event event;
SDL_Rect rect;
SDL_Renderer *renderer;
SDL_Window *window;
SDL_Texture *texture = NULL;
void *pixels;
Uint8 *base;
int pitch;
#endif

struct Color
{
	unsigned char red;
	unsigned char green;
	unsigned char blue;
};

const int KB = 1024;
const int STACK_PAGE = 0x100;

SDL_GameController* pad;
struct Color* palette;

// TODO LIST
// Mappers
// 8x16 sprite mode
// Sprite priority
// Sound
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
	#if RENDER
	const unsigned int TEXTURE_WIDTH = 256;
	const unsigned int TEXTURE_HEIGHT = 240;
	const unsigned int WINDOW_WIDTH = TEXTURE_WIDTH * 2;
    const unsigned int WINDOW_HEIGHT = TEXTURE_HEIGHT * 2;
	
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
	}
	
	FILE* rom = fopen(argv[1], "rb");
	fseek(rom, SEEK_SET, 0);
	unsigned char header[16];
	fread(header, 1, 16, rom);
	
	unsigned char prg_pages = header[4];
	unsigned char chr_pages = header[5];
	unsigned char mapper = ((header[6] >> 4) & 0xF) | (header[7] & 0xF0);
	unsigned char mirroring = header[6] & 0b1;
	
	cartridge_init(mapper, prg_pages, chr_pages, mirroring, rom);
	ppu_init();
	controller_init();
	cpu_init();
	
	fclose(rom);
	
	palette = malloc(sizeof(struct Color) * 64);
	
	FILE* paletteData = fopen("palettes\\ntscpalette.pal", "rb");
	fseek(paletteData, SEEK_SET, 0);
	fread(palette, sizeof(struct Color), 64, paletteData);
	fclose(paletteData);
	
	unsigned int currentFrame = SDL_GetTicks();
	unsigned int nextFrame = currentFrame + 16;
	
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
				struct Color pixel_data = palette[render_pixel];
				
                base[0] = pixel_data.blue;
                base[1] = pixel_data.green;
                base[2] = pixel_data.red;
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
					while (SDL_PollEvent(&event))
					{
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
								}
								break;
							}
						}
					}
					currentFrame = SDL_GetTicks();
					if (currentFrame < nextFrame)
					{
						SDL_Delay(nextFrame - currentFrame);
					}
					currentFrame = SDL_GetTicks();
					nextFrame = currentFrame + 16;
					#endif
				}
			}
		}
	}
}