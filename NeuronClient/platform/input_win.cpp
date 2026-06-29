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

#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM

namespace {

bool g_key[256] = {};          /* current held state, VK indexed */
volatile bool g_any_key = false;

/* When a modal GUI owns input, the game's kbd_* snapshot reports no keys (the GUI
 * reads raw key state via input_key_down, which ignores this). */
bool s_suppressGameKeys = false;

/* Rising-edge state for the GUI menu controls (recomputed once per frame). */
bool s_menuPrev[MenuControlCount] = {};
bool s_menuEdge[MenuControlCount] = {};

/* Mouse / primary-pointer state, in client pixels (full-window GUI space). */
int  g_mouseX = 0;
int  g_mouseY = 0;
bool g_lmb = false;
bool g_rmb = false;

/* WM_CHAR ring queue */
constexpr int QN = 64;
int  g_q[QN];
int  g_qhead = 0, g_qtail = 0;

void q_push(int c) { int n = (g_qtail + 1) % QN; if (n != g_qhead) { g_q[g_qtail] = c; g_qtail = n; } }
bool q_empty()     { return g_qhead == g_qtail; }
int  q_pop()       { if (q_empty()) return 0; int c = g_q[g_qhead]; g_qhead = (g_qhead + 1) % QN; return c; }

/* Game-facing held state: gated by the modal-GUI suppression so the whole kbd_*
 * snapshot below goes quiet in one place when a menu owns input. */
inline bool down(int vk) { return !s_suppressGameKeys && (vk >= 0 && vk < 256) && g_key[vk]; }

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
LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_KEYDOWN: input_on_key(wparam, true);  return 0;
		case WM_KEYUP:   input_on_key(wparam, false); return 0;
		case WM_CHAR:    input_on_char(wparam);       return 0;

		case WM_MOUSEMOVE:
			g_mouseX = GET_X_LPARAM(lparam);
			g_mouseY = GET_Y_LPARAM(lparam);
			return 0;
		case WM_LBUTTONDOWN:
			g_lmb = true;  SetCapture(hwnd); return 0;
		case WM_LBUTTONUP:
			g_lmb = false; ReleaseCapture();  return 0;
		case WM_RBUTTONDOWN:
			g_rmb = true;  return 0;
		case WM_RBUTTONUP:
			g_rmb = false; return 0;

		// Minimal touch: map the primary pointer to the mouse (no multi-touch yet).
		case WM_POINTERDOWN:
		case WM_POINTERUPDATE:
		case WM_POINTERUP:
		{
			POINTER_INFO pi{};
			if (GetPointerInfo(GET_POINTERID_WPARAM(wparam), &pi))
			{
				POINT pt = pi.ptPixelLocation;
				ScreenToClient(hwnd, &pt);
				g_mouseX = pt.x;
				g_mouseY = pt.y;
				g_lmb = (msg != WM_POINTERUP) && (pi.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
			}
			return 0;
		}
	}
	return -1;
}

} // namespace

void input_register_event_processor(void)
{
	EventManager::AddEventProcessor(InputWndProc);
}

/* ---- GUI menu input (raw key state, independent of the game's kbd_* gate) ---- */

bool input_key_down(int vk)
{
	return (vk >= 0 && vk < 256) && g_key[vk];
}

void input_suppress_game_keys(bool suppress)
{
	s_suppressGameKeys = suppress;
}

void input_update_menu_edges(void)
{
	bool cur[MenuControlCount] = {};
	cur[MenuUp]       = input_key_down(VK_UP)    || input_key_down('S');
	cur[MenuDown]     = input_key_down(VK_DOWN)  || input_key_down('X');
	cur[MenuLeft]     = input_key_down(VK_LEFT)  || input_key_down(VK_OEM_COMMA);
	cur[MenuRight]    = input_key_down(VK_RIGHT) || input_key_down(VK_OEM_PERIOD);
	cur[MenuActivate] = input_key_down(VK_RETURN);
	cur[MenuClose]    = input_key_down(VK_ESCAPE);

	for (int i = 0; i < MenuControlCount; ++i)
	{
		s_menuEdge[i] = cur[i] && !s_menuPrev[i];
		s_menuPrev[i] = cur[i];
	}
}

bool input_menu_edge(MenuControl control)
{
	return control >= 0 && control < MenuControlCount && s_menuEdge[control];
}

void input_mouse_state(int& x, int& y, bool& lmb, bool& rmb)
{
	x = g_mouseX;
	y = g_mouseY;
	lmb = g_lmb;
	rmb = g_rmb;
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
