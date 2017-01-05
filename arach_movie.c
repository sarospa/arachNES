#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "emu_nes.h"
#include "controller.h"

const unsigned char CONTROLLER_NONE = 0;
const unsigned char CONTROLLER_STANDARD = 1;
const unsigned char CONTROLLER_ZAPPER = 2;

unsigned char player_one_type = 0xFF;
unsigned char player_two_type = 0xFF;

unsigned char* player_one_input;
unsigned char* player_two_input;
unsigned char* commands;

unsigned int frame_count;

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	
	if (argc != 3)
	{
		printf("Error: Requires ROM and movie parameters.\n");
		return 1;
	}
	
	FILE* movie = fopen(argv[2], "rb");
	if (movie == NULL)
	{
		printf("Error: Movie could not be opened.\n");
		return 3;
	}
	fseek(movie, SEEK_SET, 0);
	
	char line[100];
	size_t len = 0;
	unsigned int frames = 0;
	
	while (fscanf(movie, "%[^\n]s", line) > 0)
	{
		len = strlen(line);
		fgetc(movie);
		if (!strncmp(line, "|", 1))
		{
			frames++;
		}
		else if ((!strncmp(line, "port0", 5)) && (len >= 7))
		{
			player_one_type = line[6] - '0';
		}
		else if ((!strncmp(line, "port1", 5)) && (len >= 7))
		{
			player_two_type = line[6] - '0';
		}
		len = 0;
	}
	
	if ((player_one_type > CONTROLLER_ZAPPER) || (player_two_type > CONTROLLER_ZAPPER))
	{
		printf("Bad movie: Player controller types not set to valid values.\n");
		return 4;
	}
	
	if ((player_one_type == CONTROLLER_ZAPPER) || (player_two_type == CONTROLLER_ZAPPER))
	{
		printf("Error: Zapper not currently supported.\n");
		return 5;
	}
	
	player_one_input = malloc(sizeof(char) * frames);
	player_two_input = malloc(sizeof(char) * frames);
	commands = malloc(sizeof(char) * frames);
	
	for (unsigned int i = 0; i < frames; i++)
	{
		player_one_input[i] = 0;
		player_two_input[i] = 0;
	}
	
	fseek(movie, SEEK_SET, 0);
	
	unsigned int current_frame = 0;
	
	while ((fscanf(movie, "%[^\n]s", line) > 0) && (current_frame < frames))
	{
		len = strlen(line);
		fgetc(movie);
		if ((!strncmp(line, "|", 1)))
		{
			unsigned char line_char = 1;
			commands[current_frame] = line[line_char] - '0';
			line_char += 2;
			if (player_one_type == CONTROLLER_STANDARD)
			{
				for (int i = 0; i < 8; i++)
				{
					if (line[line_char] != '.')
					{
						player_one_input[current_frame] = player_one_input[current_frame] | (0b1 << (7 - i));
					}
					line_char++;
				}
			}
			line_char++;
			if (player_two_type == CONTROLLER_STANDARD)
			{
				for (int i = 0; i < 8; i++)
				{
					if (line[line_char] != '.')
					{
						player_two_input[current_frame] = player_two_input[current_frame] | (0b1 << (7 - i));
					}
					line_char++;
				}
			}
			current_frame++;
		}
		len = 0;
	}
	
	frame_count = 0;
	
	sdl_init();
	nes_init(argv[1]);
	
	// Load the first input before the start of the first frame.
	handle_movie_input(player_one_input[frame_count], commands[frame_count]);
	frame_count++;
	
	while (frame_count < frames)
	{
		nes_loop();
	 
		for (unsigned int i = 0; i < render_buffer_count; i++)
		{
			render_pixel(render_buffer[i]);
			
			if (frame_finished)
			{
				handle_movie_input(player_one_input[frame_count], commands[frame_count]);
				frame_count++;
				push_audio();
				frame_finished = 0;
			}
		}
		render_buffer_count = 0;
	}
	
	while (1)
	{
		handle_user_input();
	}
}