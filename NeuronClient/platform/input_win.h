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

#endif /* INPUT_WIN_H */
