/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * platform_win.h
 *
 * Shared hooks into the Win32 platform layer, used by the graphics, audio and
 * input backends (e.g. to obtain the window handle or pump the message queue).
 */

#ifndef PLATFORM_WIN_H
#define PLATFORM_WIN_H

#include <windows.h>

/* The main game window (null before gfx_graphics_startup). */
HWND platform_window(void);

/* Drain pending Win32 messages. Terminates the process if the window closed. */
void platform_pump_messages(void);

/* Request an orderly application quit, exactly as if the user closed the window
 * (the next message pump unwinds the game loop and exits the process). */
void platform_request_quit(void);

#endif /* PLATFORM_WIN_H */
