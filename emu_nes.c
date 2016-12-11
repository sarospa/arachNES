#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <SDL.h>
#include "nes_cpu.h"
#include "nes_apu.h"
#include "nes_ppu.h"
#include "controller.h"
#include "cartridge.h"

#define RENDER 1

const unsigned int TEXTURE_WIDTH = 256;
const unsigned int TEXTURE_HEIGHT = 240;
unsigned int window_width;
unsigned int window_height;

union SDL_Event event;
SDL_Rect rect;
SDL_Renderer *renderer;
SDL_Window *window;
SDL_Texture *texture = NULL;
void *pixels;
Uint8 *base;
int pitch;
unsigned const int frame_millisecs = 16;
unsigned int x;
unsigned int y;
unsigned char* render_buffer;
const unsigned int RENDER_BUFFER_MAX = 5000;
unsigned int render_buffer_count;
unsigned int current_frame;
unsigned int next_frame;
unsigned char unbound_framerate;
unsigned char frame_finished;

unsigned char dummy;

SDL_AudioSpec want;
SDL_AudioDeviceID device;
unsigned const int audio_frequency = 48000;
float target_samples_per_frame;
float current_samples_per_frame;
float samples_this_frame;

// Trying out using a ring buffer that is written by the emulator
// and read by the audio callback.
unsigned char* audio_buffer;
unsigned int audio_buffer_writer = 0;
unsigned int audio_buffer_reader = 0;
unsigned char audio_buffer_last_value = 128;
const unsigned int audio_device_samples = 2048;
const unsigned int audio_buffer_max = 2048 * 4;
unsigned char audio_paused = 1;
unsigned char silence = 128;
// Value for how fast the audio can move between playing and silence.
unsigned char limit_step = 1;
unsigned char debug_log_sound;

struct Color
{
	unsigned char red;
	unsigned char green;
	unsigned char blue;
};

const unsigned int KB = 1024;
const unsigned int STACK_PAGE = 0x100;

SDL_GameController* pad;
struct Color* palette;

unsigned char pause_emulator = 0;

// TODO LIST
// Mappers
// Sound
// Most of PPUMASK
void exit_emulator()
{
    SDL_Quit();
	
	exit(0);
}

void audio_callback(void* userdata, Uint8* stream, int len)
{
	for (int i = 0; i < len; i++)
	{
		if (audio_buffer_reader == audio_buffer_writer)
		{
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
	// Nothing to do if there's no APU samples, and also we don't
	// want to divide by zero.
	if (apu_buffer_length == 0)
	{
		return;
	}
	
	float index;
	// The plus 15 is a little bit magic? I don't want to touch it.
	samples_this_frame += current_samples_per_frame + 15;
	float sample_fraction = samples_this_frame / apu_buffer_length;
	float apu_sample_processing = 0;
	unsigned int apu_sample_count = 0;
	for (index = 1; index <= samples_this_frame; index++)
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
			unsigned char next_value = audio_buffer_last_value;
			if (sample_size > 0)
			{
				next_value = (signed char)((roundf((sample_total / sample_size) * 1000) / 1000) * UCHAR_MAX);
			}
			
			audio_buffer[audio_buffer_writer] = next_value;
			audio_buffer_last_value = next_value;
			audio_buffer_writer = (audio_buffer_writer + 1) % audio_buffer_max;
			
			if (debug_log_sound)
			{
				printf("Sample value %d\n", next_value);
			}
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

void nes_loop()
{
	unsigned char opcode;
	get_pointer_at_cpu_address(&opcode, program_counter, READ);
	unsigned int cycles = run_opcode(opcode);
	
	for (unsigned int i = 0; i < cycles; i++)
	{
		apu_tick();
	}
	
	// PPU runs at triple the speed of the CPU.
	// Call PPU tick three times for every CPU cycle.
	for (unsigned int i = 0; i < (cycles * 3); i++)
	{
		unsigned char pixel_data = ppu_tick();
		if (render_buffer_count < RENDER_BUFFER_MAX)
		{
			render_buffer[render_buffer_count] = pixel_data;
			render_buffer_count++;
		}
	}
}

void save_state()
{
	FILE* save_state = fopen("arachNES_save_state", "wb");
	if (save_state != NULL)
	{
		cpu_save_state(save_state);
		ppu_save_state(save_state);
		apu_save_state(save_state);
		controller_save_state(save_state);
		cartridge_save_state(save_state);
		fclose(save_state);
	}
}

void load_state()
{
	FILE* save_state = fopen("arachNES_save_state", "rb");
	if (save_state != NULL)
	{
		cpu_load_state(save_state);
		ppu_load_state(save_state);
		apu_load_state(save_state);
		controller_load_state(save_state);
		cartridge_load_state(save_state);
		fclose(save_state);
		audio_buffer_writer = 0;
		audio_buffer_reader = 0;
		audio_buffer_last_value = 128;

	}
}

void render_pixel(unsigned char pixel)
{
	if (pixel != 255)
	{
		if ((x == 0) && (y == 0))
		{
			SDL_LockTexture(texture, NULL, &pixels, &pitch);
		}
		
		base = ((Uint8 *)pixels) + (4 * (y * TEXTURE_WIDTH + x));
		struct Color pixel_data = palette[pixel];
		
		base[0] = pixel_data.blue;
		base[1] = pixel_data.green;
		base[2] = pixel_data.red;
		base[3] = 0;
		
		x++;
		if (x >= TEXTURE_WIDTH)
		{
			x = 0;
			y++;
		}
		
		if (y >= TEXTURE_HEIGHT)
		{
			y = 0;
			SDL_UnlockTexture(texture);
			SDL_RenderCopy(renderer, texture, NULL, NULL);
			SDL_RenderPresent(renderer);
			
			frame_finished = 1;
			
			current_frame = SDL_GetTicks();
			if ((current_frame < next_frame) && (!unbound_framerate))
			{
				SDL_Delay(next_frame - current_frame);
			}
			current_frame = SDL_GetTicks();
			next_frame = current_frame + frame_millisecs;
		}
	}
}

void handle_user_input()
{
	unsigned char queued_event = 1;
	while (queued_event || pause_emulator)
	{
		queued_event = SDL_PollEvent(&event);
		if (pause_emulator)
		{
			SDL_Delay(10);
		}
		
		if (queued_event)
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
						case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
						{
							save_state();
							break;
						}
						case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
						{
							load_state();
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
							// Savestate
							case SDL_SCANCODE_F1:
							{
								save_state();
								break;
							}
							case SDL_SCANCODE_F2:
							{
								load_state();
								break;
							}
							// debug hotkeys
							case SDL_SCANCODE_R:
							{
								printf("ROM DUMP\n");
								for (int i = 0; i < 0x1000; i++)
								{
									unsigned char data;
									get_pointer_at_cpu_address(&data, i, READ);
									printf("%04X: %02X\n", i, data);
								}
								break;
							}
							case SDL_SCANCODE_N:
							{
								printf("NAMETABLE DUMP\n");
								for (int i = 0x2000; i <= 0x2FFF; i++)
								{
									unsigned char data;
									get_pointer_at_nametable_address(&data, i, READ);
									printf("%04X: %02X\n", i, data);
								}
								break;
							}
							case SDL_SCANCODE_P:
							{
								printf("PATTERN TABLE DUMP\n");
								for (int i = 0; i <= 0x1FFF; i++)
								{
									unsigned char data;
									get_pointer_at_chr_address(&data, i, READ);
									printf("%04X: %02X\n", i, data);
								}
								break;
							}
							case SDL_SCANCODE_S:
							{
								debug_log_sound = 1;
								break;
							}
							case SDL_SCANCODE_PAUSE:
							{
								pause_emulator = !pause_emulator;
								printf("Setting emulator pause to %d\n", pause_emulator);
								break;
							}
							// Silence controls for APU channels.
							case SDL_SCANCODE_1:
							{
								pulse_1_silence = !pulse_1_silence;
								break;
							}
							case SDL_SCANCODE_2:
							{
								pulse_2_silence = !pulse_2_silence;
								break;
							}
							case SDL_SCANCODE_3:
							{
								triangle_silence = !triangle_silence;
								break;
							}
							case SDL_SCANCODE_4:
							{
								noise_silence = !noise_silence;
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
							// debug hotkeys
							case SDL_SCANCODE_S:
							{
								debug_log_sound = 0;
								break;
							}
						}
					}
					break;
				}
			}
		}
	}
}

void handle_movie_input(unsigned char player_one_input, unsigned char command)
{
	controller_1_data = player_one_input;
	if (command & 0b1)
	{
		reset_cpu();
	}
	
	unsigned char queued_event = 1;
	while (queued_event)
	{
		queued_event = SDL_PollEvent(&event);
		if (queued_event)
		{
			switch(event.type)
			{
				case SDL_QUIT:
				{
					printf("Quitting.\n");
					exit_emulator();
					break;
				}
				case SDL_KEYDOWN:
				{
					if (!event.key.repeat)
					{
						switch(event.key.keysym.scancode)
						{
							case SDL_SCANCODE_EQUALS:
							{
								unbound_framerate = 1;
								break;
							}
							case SDL_SCANCODE_MINUS:
							{
								unbound_framerate = 0;
								break;
							}
						}
					}
				}
			}
		}
	}
}

void sdl_init()
{
	window_width = TEXTURE_WIDTH * 2;
    window_height = TEXTURE_HEIGHT * 2;
	unbound_framerate = 0;
	
	SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO);
	window = SDL_CreateWindow("arachNES Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, 0);
	renderer = SDL_CreateRenderer(window, -1, 0);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, TEXTURE_WIDTH, TEXTURE_HEIGHT);
	render_buffer_count = 0;
	
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
	debug_log_sound = 0;
	
	x = 0;
    y = 0;
	
	int num_joysticks = SDL_NumJoysticks();
	if (num_joysticks > 0)
	{
		pad = SDL_GameControllerOpen(0);
	}
}

void nes_init(char* rom_name)
{
	FILE* rom = fopen(rom_name, "rb");
	if (rom == NULL)
	{
		printf("ROM file could not be opened.\n");
		exit_emulator();
	}
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
	
	current_frame = SDL_GetTicks();
	next_frame = current_frame + frame_millisecs;
	
	render_buffer = malloc(sizeof(char) * RENDER_BUFFER_MAX);
	frame_finished = 0;
}
