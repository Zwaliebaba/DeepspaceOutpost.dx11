/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * input_win.h
 *
 * Bridge from the Win32 window procedure into the keyboard backend.
 */

#ifndef INPUT_WIN_H
#define INPUT_WIN_H

#include <windows.h>

void input_on_key(WPARAM vk, bool down);   /* WM_KEYDOWN / WM_KEYUP */
void input_on_char(WPARAM ch);             /* WM_CHAR (text entry)  */

/* Register the keyboard processor with EventManager so the engine's window procedure
 * feeds key/char messages into this backend. Called once from ClientEngine::Startup. */
void input_register_event_processor(void);

/* ---- GUI menu input -------------------------------------------------------- */
/* The GUI reads raw key state (and edge-triggered menu controls) directly, bypassing
 * the game's kbd_* snapshot. While a modal GUI owns input, the game's snapshot is
 * suppressed so only the menu responds. */

enum MenuControl { MenuUp, MenuDown, MenuLeft, MenuRight, MenuActivate, MenuClose, MenuControlCount };

bool input_key_down(int vk);                 /* raw held state of a virtual key */
void input_update_menu_edges(void);          /* recompute edges; call once per frame */
bool input_menu_edge(MenuControl control);   /* true on the frame the control is pressed */
void input_suppress_game_keys(bool suppress);/* hide keys from the game's kbd_* snapshot */

/* Mouse / primary-pointer state in client pixels (full-window GUI space). */
void input_mouse_state(int& x, int& y, bool& lmb, bool& rmb);

/* Wheel notches accumulated since the previous call (consumed once per frame by
 * the camera rig; + = wheel up). */
float input_take_mouse_wheel(void);

/* ---- I5 touch gestures (device-neutral recognizer) ------------------------- */
/* Call once per frame so the recognizer's time-based transitions (long-press) can
 * fire without a pointer message. */
void input_pointer_tick(void);

/* Two-finger pan delta accumulated since the previous call (client pixels). */
void input_take_pan(float& dx, float& dy);

/* One-shot touch gestures: true (with position) on the frame they occur, then
 * consumed. Long-press opens the radial command menu; double-tap focuses the
 * camera on whatever is under the point. */
bool input_take_long_press(int& x, int& y);
bool input_take_double_tap(int& x, int& y);

/* Touch pointers currently in contact (0-2); the finger lift commits a
 * touch-opened radial menu. */
int input_touch_count(void);

#endif /* INPUT_WIN_H */
