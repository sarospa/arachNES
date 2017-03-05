#ifndef EMU_HEADER
#define EMU_HEADER

extern unsigned char* render_buffer;
extern unsigned int render_buffer_count;
extern unsigned char frame_finished;
extern unsigned char debug_log_sound;

// Dummy register for stubbing unimplemented registers and capturing 'ignored' writes.
extern unsigned char dummy;
extern unsigned char full_log;

void exit_emulator();
void sdl_init();
void nes_init(char* rom_name);
void nes_loop();
void handle_user_input();
void handle_movie_input(unsigned char player_one_input, unsigned char command);
void push_audio();
void render_pixel(unsigned char pixel_data);

#endif