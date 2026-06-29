/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * input_win.cpp  (M4)
 *
 * Win32 keyboard backend for the keyboard.h contract. The window procedure
 * feeds key up/down into a virtual-key state table and printable characters
 * into a small ring queue (used by the "find planet" text entry). Each frame
 * kbd_poll_keyboard() snapshots the held-key state into the kbd_*_pressed
 * globals the game reads, mirroring the original Allegro key[] mapping.
 */

#include "pch.h"

#include "input_win.h"
#include "platform_win.h"

#include "keyboard.h"
#include "EventManager.h"

namespace {

bool g_key[256] = {};          /* current held state, VK indexed */
volatile bool g_any_key = false;

/* WM_CHAR ring queue */
constexpr int QN = 64;
int  g_q[QN];
int  g_qhead = 0, g_qtail = 0;

void q_push(int c) { int n = (g_qtail + 1) % QN; if (n != g_qhead) { g_q[g_qtail] = c; g_qtail = n; } }
bool q_empty()     { return g_qhead == g_qtail; }
int  q_pop()       { if (q_empty()) return 0; int c = g_q[g_qhead]; g_qhead = (g_qhead + 1) % QN; return c; }

inline bool down(int vk) { return (vk >= 0 && vk < 256) && g_key[vk]; }

} // namespace

void input_on_key(WPARAM vk, bool d)
{
	if (vk < 256)
		g_key[vk] = d;
	if (d)
		g_any_key = true;
}

void input_on_char(WPARAM ch)
{
	q_push(static_cast<int>(ch));
}

namespace {

/* Keyboard processor for the EventManager chain. Returns 0 for messages it consumes,
 * -1 otherwise so the chain / DefWindowProc continue (e.g. Alt+F4). */
LRESULT CALLBACK InputWndProc(HWND, UINT msg, WPARAM wparam, LPARAM)
{
	switch (msg)
	{
		case WM_KEYDOWN: input_on_key(wparam, true);  return 0;
		case WM_KEYUP:   input_on_key(wparam, false); return 0;
		case WM_CHAR:    input_on_char(wparam);       return 0;
	}
	return -1;
}

} // namespace

void input_register_event_processor(void)
{
	EventManager::AddEventProcessor(InputWndProc);
}

/* ---- keyboard.h contract ---- */

int kbd_F1_pressed, kbd_F2_pressed, kbd_F3_pressed, kbd_F4_pressed;
int kbd_F5_pressed, kbd_F6_pressed, kbd_F7_pressed, kbd_F8_pressed;
int kbd_F9_pressed, kbd_F10_pressed, kbd_F11_pressed, kbd_F12_pressed;
int kbd_y_pressed, kbd_n_pressed;
int kbd_fire_pressed, kbd_ecm_pressed, kbd_energy_bomb_pressed;
int kbd_hyperspace_pressed, kbd_ctrl_pressed, kbd_jump_pressed, kbd_escape_pressed;
int kbd_dock_pressed, kbd_d_pressed, kbd_origin_pressed, kbd_find_pressed;
int kbd_fire_missile_pressed, kbd_target_missile_pressed, kbd_unarm_missile_pressed;
int kbd_pause_pressed, kbd_resume_pressed;
int kbd_inc_speed_pressed, kbd_dec_speed_pressed;
int kbd_up_pressed, kbd_down_pressed, kbd_left_pressed, kbd_right_pressed;
int kbd_enter_pressed, kbd_backspace_pressed, kbd_space_pressed;

int kbd_keyboard_startup(void)  { return 0; }
int kbd_keyboard_shutdown(void) { return 0; }

void kbd_poll_keyboard(void)
{
	kbd_F1_pressed  = down(VK_F1);  kbd_F2_pressed  = down(VK_F2);
	kbd_F3_pressed  = down(VK_F3);  kbd_F4_pressed  = down(VK_F4);
	kbd_F5_pressed  = down(VK_F5);  kbd_F6_pressed  = down(VK_F6);
	kbd_F7_pressed  = down(VK_F7);  kbd_F8_pressed  = down(VK_F8);
	kbd_F9_pressed  = down(VK_F9);  kbd_F10_pressed = down(VK_F10);
	kbd_F11_pressed = down(VK_F11); kbd_F12_pressed = down(VK_F12);

	kbd_y_pressed = down('Y');
	kbd_n_pressed = down('N');

	kbd_fire_pressed        = down('A');
	kbd_ecm_pressed         = down('E');
	kbd_energy_bomb_pressed = down(VK_TAB);
	kbd_hyperspace_pressed  = down('H');
	kbd_ctrl_pressed        = down(VK_CONTROL) || down(VK_LCONTROL) || down(VK_RCONTROL);
	kbd_jump_pressed        = down('J');
	kbd_escape_pressed      = down(VK_ESCAPE);

	kbd_dock_pressed   = down('C');
	kbd_d_pressed      = down('D');
	kbd_origin_pressed = down('O');
	kbd_find_pressed   = down('F');

	kbd_fire_missile_pressed   = down('M');
	kbd_target_missile_pressed = down('T');
	kbd_unarm_missile_pressed  = down('U');

	kbd_pause_pressed  = down('P');
	kbd_resume_pressed = down('R');

	kbd_inc_speed_pressed = down(VK_SPACE);
	kbd_dec_speed_pressed = down(VK_OEM_2);   /* '/' */

	kbd_up_pressed    = down('S') || down(VK_UP);
	kbd_down_pressed  = down('X') || down(VK_DOWN);
	kbd_left_pressed  = down(VK_OEM_COMMA) || down(VK_LEFT);
	kbd_right_pressed = down(VK_OEM_PERIOD) || down(VK_RIGHT);

	kbd_enter_pressed     = down(VK_RETURN);
	kbd_backspace_pressed = down(VK_BACK);
	kbd_space_pressed     = down(VK_SPACE);
}

int kbd_read_key(void)
{
	kbd_enter_pressed = 0;
	kbd_backspace_pressed = 0;

	if (q_empty())
		return 0;

	int ch = q_pop();
	if (ch == '\r' || ch == '\n') { kbd_enter_pressed = 1; return 0; }
	if (ch == '\b')               { kbd_backspace_pressed = 1; return 0; }
	return ch;
}

void kbd_clear_key_buffer(void)
{
	g_qhead = g_qtail = 0;
}

void kbd_wait_key(void)
{
	g_any_key = false;
	while (!g_any_key)
	{
		platform_pump_messages();
		Sleep(10);
	}
}
