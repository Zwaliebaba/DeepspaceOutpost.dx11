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
#include "input/GestureRecognizer.h"   // I5 device-neutral gesture recognizer (tested core)

#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM
#include <cmath>      // std::sqrt (I5 pinch distance)

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

/* Accumulated wheel notches since last consumed (camera dolly/zoom). Both the mouse
 * wheel and the I5 touch PINCH feed this, so zoom is one device-neutral event. */
float g_wheelSteps = 0.0f;

/* I5 touch: track up to two active pointers so a two-finger PINCH can drive zoom.
 * A single pointer still maps to the mouse/LMB (tap = select, drag = orbit). */
struct TouchPt { UINT32 id = 0; int x = 0; int y = 0; bool active = false; };
TouchPt g_touch[2];
float   g_pinchPrevDist = -1.0f;   /* < 0 = no pinch in progress */

/* I5 gesture recognizer: fed every WM_POINTER sample (both fingers) as the tested
 * source of the gestures the ad-hoc mapping above lacks. The one-finger->mouse and
 * two-finger->pinch synthesis above stays (proven), so from the recognizer we
 * consume ONLY the new gestures: LONG-PRESS (touch radial menu), DOUBLE-TAP (focus
 * camera) and two-finger PAN (exposed for a camera-pan follow-up). Its Tap/Drag/
 * Pinch events are ignored here. All timestamps use one clock (GetTickCount). */
Neuron::Input::GestureRecognizer g_gestures;
float g_panDX = 0.0f, g_panDY = 0.0f;                  /* accumulated two-finger pan */
bool  g_longPress = false; int g_longPressX = 0, g_longPressY = 0;  /* one-shot */
bool  g_doubleTap = false; int g_doubleTapX = 0, g_doubleTapY = 0;  /* one-shot */
bool  g_touchSuppress = false;   /* a long-press fired this contact: no synth select */

void handle_gesture(const Neuron::Input::GestureEvent& e)
{
	using GT = Neuron::Input::GestureType;
	switch (e.type)
	{
		case GT::LongPress:
			g_longPress = true; g_longPressX = static_cast<int>(e.x); g_longPressY = static_cast<int>(e.y);
			g_touchSuppress = true; g_lmb = false;   // long-press owns the contact, not a tap
			break;
		case GT::DoubleTap:
			g_doubleTap = true; g_doubleTapX = static_cast<int>(e.x); g_doubleTapY = static_cast<int>(e.y);
			break;
		case GT::PanMove:
			g_panDX += e.dx; g_panDY += e.dy;
			break;
		default:
			break;   // Tap/Drag/Pinch are handled by the synthesis path above
	}
}

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
			g_lmb = false; if (!g_rmb) ReleaseCapture(); return 0;
		/* RMB captures too: it is the camera look-drag, and the drag must keep
		 * feeding deltas when the pointer leaves the client area. */
		case WM_RBUTTONDOWN:
			g_rmb = true;  SetCapture(hwnd); return 0;
		case WM_RBUTTONUP:
			g_rmb = false; if (!g_lmb) ReleaseCapture(); return 0;

		case WM_MOUSEWHEEL:
			g_wheelSteps += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
			return 0;

		// I5 multi-touch: track up to two pointers. One finger maps to the mouse
		// (tap = select, drag = orbit); two fingers PINCH to zoom (into the same wheel
		// accumulator as the mouse wheel). In parallel every sample feeds the tested
		// gesture recognizer, which adds LONG-PRESS (touch radial menu), DOUBLE-TAP
		// (focus) and two-finger PAN (exposed via input_take_pan; the Orbit controller
		// has no translate axis yet, so pan awaits that camera-math follow-up).
		case WM_POINTERDOWN:
		case WM_POINTERUPDATE:
		case WM_POINTERUP:
		{
			const UINT32 pid = GET_POINTERID_WPARAM(wparam);
			POINTER_INFO pi{};
			if (!GetPointerInfo(pid, &pi))
				return 0;
			POINT pt = pi.ptPixelLocation;
			ScreenToClient(hwnd, &pt);
			const bool inContact = (msg != WM_POINTERUP)
			                    && (pi.pointerFlags & POINTER_FLAG_INCONTACT) != 0;

			// Feed the tested recognizer (one clock for Push + Tick); consume only its
			// long-press / double-tap / pan events (handle_gesture). Tap/drag/pinch stay
			// on the synthesis path below.
			{
				Neuron::Input::PointerSample gs;
				gs.id = pid; gs.x = static_cast<float>(pt.x); gs.y = static_cast<float>(pt.y);
				gs.timeMs = static_cast<uint32_t>(GetTickCount());
				gs.phase = (msg == WM_POINTERDOWN) ? Neuron::Input::PointerPhase::Down
				         : (!inContact)            ? Neuron::Input::PointerPhase::Up
				         :                           Neuron::Input::PointerPhase::Move;
				if ((pi.pointerFlags & POINTER_FLAG_CANCELED) != 0)
					gs.phase = Neuron::Input::PointerPhase::Cancel;
				for (const auto& e : g_gestures.Push(gs))
					handle_gesture(e);
			}

			// Assign this pointer id to a slot (reuse its slot, else a free one).
			int slot = -1;
			for (int i = 0; i < 2; ++i)
				if (g_touch[i].active && g_touch[i].id == pid) { slot = i; break; }
			if (slot < 0 && inContact)
				for (int i = 0; i < 2; ++i)
					if (!g_touch[i].active) { g_touch[i].id = pid; g_touch[i].active = true; slot = i; break; }
			if (slot >= 0)
			{
				g_touch[slot].x = pt.x;
				g_touch[slot].y = pt.y;
				if (!inContact)
					g_touch[slot].active = false;
			}

			const int n = (g_touch[0].active ? 1 : 0) + (g_touch[1].active ? 1 : 0);
			if (n >= 2)
			{
				g_lmb = false;   // two fingers is a pinch, not a click/drag
				const float dx = static_cast<float>(g_touch[0].x - g_touch[1].x);
				const float dy = static_cast<float>(g_touch[0].y - g_touch[1].y);
				const float dist = std::sqrt(dx * dx + dy * dy);
				if (g_pinchPrevDist > 0.0f)
					g_wheelSteps += (dist - g_pinchPrevDist) / 60.0f;   // ~60 px per zoom notch
				g_pinchPrevDist = dist;
			}
			else
			{
				g_pinchPrevDist = -1.0f;
				if (n == 1)
				{
					const TouchPt& p = g_touch[0].active ? g_touch[0] : g_touch[1];
					g_mouseX = p.x;
					g_mouseY = p.y;
					g_lmb = !g_touchSuppress;   // long-press owns the contact -> no synth select
				}
				else
				{
					g_lmb = false;           // all fingers lifted
					g_touchSuppress = false; // contact ended: re-arm select
				}
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

float input_take_mouse_wheel(void)
{
	const float steps = g_wheelSteps;
	g_wheelSteps = 0.0f;
	return steps;
}

/* I5 per-frame tick: drive the recognizer's time-based transitions (long-press
 * fires without a pointer message). One clock with the WM_POINTER samples. */
void input_pointer_tick(void)
{
	for (const auto& e : g_gestures.Tick(static_cast<uint32_t>(GetTickCount())))
		handle_gesture(e);
}

/* I5 two-finger pan delta since the last poll (accumulated, then cleared). */
void input_take_pan(float& dx, float& dy)
{
	dx = g_panDX; dy = g_panDY;
	g_panDX = g_panDY = 0.0f;
}

/* I5 one-shot touch long-press (opens the radial menu / gizmo at x,y). */
bool input_take_long_press(int& x, int& y)
{
	if (!g_longPress) return false;
	x = g_longPressX; y = g_longPressY; g_longPress = false;
	return true;
}

/* I5 one-shot touch double-tap (focus the camera on whatever is at x,y). */
bool input_take_double_tap(int& x, int& y)
{
	if (!g_doubleTap) return false;
	x = g_doubleTapX; y = g_doubleTapY; g_doubleTap = false;
	return true;
}

/* I5 number of touch pointers currently in contact (0-2). The game uses the
 * finger lift (count -> 0) to commit a touch-opened radial menu. */
int input_touch_count(void)
{
	return (g_touch[0].active ? 1 : 0) + (g_touch[1].active ? 1 : 0);
}

/* ---- keyboard.h contract ---- */

int kbd_F1_pressed, kbd_F2_pressed, kbd_F3_pressed, kbd_F4_pressed;
int kbd_F5_pressed, kbd_F6_pressed, kbd_F7_pressed, kbd_F8_pressed;
int kbd_F9_pressed, kbd_F10_pressed, kbd_F11_pressed, kbd_F12_pressed;
int kbd_y_pressed, kbd_n_pressed;
// I7: the combat keys (A/E/Tab/M/T/U/pod/J/H) retired into the pointer UX (I2 select,
// I3 orders, I4 ability bar, I6 chart button); their kbd_* globals + mappings are
// gone. kbd_ctrl stays (a modifier), and the chart D/F/O keys + the crosshair arrows
// stay as accelerators.
int kbd_ctrl_pressed;
int kbd_dock_pressed, kbd_d_pressed, kbd_origin_pressed, kbd_find_pressed;
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

	kbd_ctrl_pressed = down(VK_CONTROL) || down(VK_LCONTROL) || down(VK_RCONTROL);

	kbd_dock_pressed   = down('C');
	kbd_d_pressed      = down('D');
	kbd_origin_pressed = down('O');
	kbd_find_pressed   = down('F');

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
