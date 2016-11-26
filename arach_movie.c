#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "emu_nes.h"
#include "controller.h"

unsigned char* player_one_input;
unsigned char* player_two_input;

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
		if ((!strncmp(line, "|", 1)) && (len >= 22))
		{
			frames++;
		}
		len = 0;
	}
	
	player_one_input = malloc(sizeof(char) * frames);
	player_two_input = malloc(sizeof(char) * frames);
	
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
		if ((!strncmp(line, "|", 1)) && (len >= 22))
		{
			for (unsigned int i = 0; i < 8; i++)
			{
				if (line[3 + i] != '.')
				{
					player_one_input[current_frame] = player_one_input[current_frame] | (0b1 << (7 - i));
				}
				
				if (line[12 + i] != '.')
				{
					player_two_input[current_frame] = player_two_input[current_frame] | (0b1 << (7 - i));
				}
			}
			current_frame++;
		}
		len = 0;
	}
	
	frame_count = 0;
	
	sdl_init();
	nes_init(argv[1]);
	
	while (frame_count < frames)
	{
		nes_loop();
	 
		for (unsigned int i = 0; i < render_buffer_count; i++)
		{
			render_pixel(render_buffer[i]);
			
			if (frame_finished)
			{
				handle_movie_input(player_one_input[frame_count]);
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