#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include "nes_cpu.h"
#include "nes_apu.h"
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
unsigned const int frame_millisecs = 16;

SDL_AudioSpec want;
SDL_AudioDeviceID device;
unsigned const int audio_frequency = 44100;
float target_samples_per_frame;
float current_samples_per_frame;
float samples_this_frame;

// Trying out using a ring buffer that is written by the emulator
// and read by the audio callback.
unsigned char* audio_buffer;
unsigned int audio_buffer_writer = 0;
unsigned int audio_buffer_reader = 0;
unsigned char audio_buffer_last_value = 128;
const unsigned int audio_device_samples = 4096;
const unsigned int audio_buffer_max = 4096 * 4;
unsigned char audio_paused = 10;
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

void audio_callback(void* userdata, Uint8* stream, int len)
{
	for (int i = 0; i < len; i++)
	{
		if (audio_buffer_reader == audio_buffer_writer)
		{
			printf("Buffering silence.\n");
			stream[i] = audio_buffer_last_value;
		}
		else
		{
			stream[i] = audio_buffer[audio_buffer_reader];
			audio_buffer[audio_buffer_reader] = 128;
			audio_buffer_reader = (audio_buffer_reader + 1) % audio_buffer_max;
		}
	}
}

// Calculate how to squeeze the NES samples down into the sample frequency of the audio output.
// Currently downsampling by averaging each group of samples.
void push_audio()
{
	// Add more or less samples to keep the buffer in sync with the audio device.
	/*unsigned int buffered_amount;
	if (audio_buffer_reader > audio_buffer_writer)
	{
		buffered_amount = audio_buffer_max - (audio_buffer_reader - audio_buffer_writer);
	}
	else
	{
			buffered_amount = audio_buffer_writer - audio_buffer_reader;
	}
	
	if ((buffered_amount > (audio_buffer_max / 2)) && (current_samples_per_frame > target_samples_per_frame))
	{
		current_samples_per_frame--;
	}
	else if (buffered_amount < (audio_buffer_max / 2))
	{
		current_samples_per_frame++;
	}*/
	
	float index;
	// The plus 15 is a little bit magic? I don't want to touch it.
	samples_this_frame += current_samples_per_frame + 15;
	float sample_fraction = current_samples_per_frame / apu_buffer_length;
	float apu_sample_processing = 0;
	unsigned int apu_sample_count = 0;
	for (index = 0; index < samples_this_frame; index++)
	{
		unsigned char sample_size = 0;
		float sample_total = 0;
		while ((apu_sample_processing < index) && (apu_sample_count < apu_buffer_length))
		{
			sample_total += mixer_buffer[apu_sample_count];
			sample_size++;
			apu_sample_processing += sample_fraction;
			apu_sample_count++;
		}
		if (((audio_buffer_writer + 1) % audio_buffer_max) != audio_buffer_reader)
		{
			if (sample_size > 0)
			{
			audio_buffer[audio_buffer_writer] = (signed char)((roundf((sample_total / sample_size) * 1000) / 1000) * UCHAR_MAX);
			audio_buffer_last_value = audio_buffer[audio_buffer_writer];
			//printf("Buffering sample %d at %d\n", audio_buffer[audio_buffer_writer], audio_buffer_writer);
			}
			else
			{
				audio_buffer[audio_buffer_writer] = audio_buffer_last_value;
			}
			audio_buffer_writer = (audio_buffer_writer + 1) % audio_buffer_max;
		}
		else
		{
			printf("Audio buffer full.\n");
		}
	}
	
	// Any leftover samples get moved to the start of the buffer to be processed in the next frame.
	unsigned int next_apu_buffer_length = 0;
	for (int i = 0; (apu_sample_count + i) < apu_buffer_length; i++)
	{
		mixer_buffer[i] = mixer_buffer[apu_sample_count + i];
		next_apu_buffer_length++;
	}
	apu_buffer_length = next_apu_buffer_length;
	
	index--;
	samples_this_frame -= index;
	
	//SDL_QueueAudio(device, audio_buffer, audio_buffer_writer);
	
	if (audio_paused > 1)
	{
		audio_paused--;
	}
	else if (audio_paused)
	{
		SDL_PauseAudioDevice(device, 0);
		audio_paused = 0;
	}
}

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	
	#if RENDER
	const unsigned int TEXTURE_WIDTH = 256;
	const unsigned int TEXTURE_HEIGHT = 240;
	const unsigned int WINDOW_WIDTH = TEXTURE_WIDTH * 2;
    const unsigned int WINDOW_HEIGHT = TEXTURE_HEIGHT * 2;
	
	SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO);
    //SDL_CreateWindowAndRenderer(WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer);
	window = SDL_CreateWindow("arachNES Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
	renderer = SDL_CreateRenderer(window, -1, 0);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, TEXTURE_WIDTH, TEXTURE_HEIGHT);
	
	SDL_memset(&want, 0, sizeof(want));
	want.freq = audio_frequency;
	want.format = AUDIO_U8;
	want.channels = 1;
	want.samples = audio_device_samples;
	want.callback = audio_callback;
	device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
	
	target_samples_per_frame = (float)audio_frequency * (frame_millisecs / 1000.0f);
	current_samples_per_frame = target_samples_per_frame;
	samples_this_frame = 0;
	audio_buffer = malloc(sizeof(int32_t) * audio_buffer_max);
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
	apu_init();
	ppu_init();
	controller_init();
	cpu_init();
	
	fclose(rom);
	
	palette = malloc(sizeof(struct Color) * 64);
	
	FILE* paletteData = fopen("palettes/ntscpalette.pal", "rb");
	fseek(paletteData, SEEK_SET, 0);
	fread(palette, sizeof(struct Color), 64, paletteData);
	fclose(paletteData);
	
	unsigned int currentFrame = SDL_GetTicks();
	unsigned int nextFrame = currentFrame + frame_millisecs;
	
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
		
		for (int i = 0; i < cycles; i++)
		{
			apu_tick();
		}
		
		controller_tick();
		
		// PPU runs at triple the speed of the CPU.
		// Call PPU tick three times for every CPU cycle.
		for (int i = 0; i < (cycles * 3); i++)
		{
			unsigned char render_pixel = ppu_tick();
			if (render_pixel != 255)
			{
				#if RENDER
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
											printf("SPRITE PAGE DUMP\n");
											for (int i = 0; i < 256; i++)
											{
												printf("%02X: %02X\n", i, *get_pointer_at_cpu_address(0x0200 + i, READ));
											}
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
					
					push_audio();
					
					currentFrame = SDL_GetTicks();
					if (currentFrame < nextFrame)
					{
						SDL_Delay(nextFrame - currentFrame);
					}
					currentFrame = SDL_GetTicks();
					nextFrame = currentFrame + frame_millisecs;
					#endif
				}
			}
		}
	}
}
