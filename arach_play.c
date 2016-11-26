#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "emu_nes.h"

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	
	if (argc < 2)
	{
		printf("ROM parameter required.\n");
		exit(1);
	}
	
	sdl_init();
	nes_init(argv[1]);
	
	while(1)
	{
		nes_loop();
	 
		for (unsigned int i = 0; i < render_buffer_count; i++)
		{
			render_pixel(render_buffer[i]);
			
			if (frame_finished)
			{
				handle_user_input();
				push_audio();
				frame_finished = 0;
			}
		}
		render_buffer_count = 0;
	}
}