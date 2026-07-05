/*
 * keyboard.h
 *
 * Code to handle keyboard input.
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H
 
extern int kbd_F1_pressed;
extern int kbd_F2_pressed;
extern int kbd_F3_pressed;
extern int kbd_F4_pressed;
extern int kbd_F5_pressed;
extern int kbd_F6_pressed;
extern int kbd_F7_pressed;
extern int kbd_F8_pressed;
extern int kbd_F9_pressed;
extern int kbd_F10_pressed;
extern int kbd_F11_pressed;
extern int kbd_F12_pressed;
extern int kbd_y_pressed;
extern int kbd_n_pressed;
// I7: the combat keys retired into the pointer UX - kbd_fire/ecm/energy_bomb/
// hyperspace/jump/escape(pod)/fire_missile/target_missile/unarm_missile are gone.
extern int kbd_ctrl_pressed;
extern int kbd_dock_pressed;
extern int kbd_d_pressed;
extern int kbd_origin_pressed;
extern int kbd_find_pressed;
extern int kbd_inc_speed_pressed;
extern int kbd_dec_speed_pressed;
extern int kbd_up_pressed;
extern int kbd_down_pressed;
extern int kbd_left_pressed;
extern int kbd_right_pressed;
extern int kbd_enter_pressed;
extern int kbd_backspace_pressed;
extern int kbd_space_pressed;


int kbd_keyboard_startup (void);
int kbd_keyboard_shutdown (void);
void kbd_poll_keyboard (void);
int kbd_read_key (void);
void kbd_clear_key_buffer (void);
void kbd_wait_key (void);		/* Block until a key is pressed (pumps the window). */

#endif
 
