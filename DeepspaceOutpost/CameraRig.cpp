#include "pch.h"

#include "CameraRig.h"

#include <chrono>
#include <cmath>

#include "vector.h"
#include "elite.h"               // current_screen (the arrows belong to the charts there)
#include "space.h"               // local_object, g_missile_lock_target
#include "stars.h"               // set_starfield_motion (dust streaming cue)
#include "Camera.h"
#include "CameraController.h"
#include "ReplicationClient.h"
#include "GuiOverlay.h"
#include "GraphicsCore.h"        // Graphics::Core::GetOutputSize (pan scale = units/pixel)
#include "input_win.h"           // PointerInput front door (mouse / wheel / key state)

// Defined in main.cpp: the world position the F-key focus should target (the
// selection's primary, else the own ship), and the reset of the pointer-command
// state (selection / grid / band) the rig's reset seam clears (input.md §6.2).
bool SelectionFocusWorld(double _out[3]);
void ResetCommandState(void);

using namespace Neuron;

namespace
{
	Client::FirstPersonCameraController s_fpv;
	Client::OrbitCameraController s_orbit;
	bool s_orbitMode = true;   // Homeworld default: the focus-orbit camera (FPV on F12)

	bool s_ready = false;
	long long s_origin[3] = {0, 0, 0};

	/* The entity the focus-orbit camera FOLLOWS (0xFFFFFFFF = detached/free after a
	 * pan). Default is the player's own ship; F / double-tap re-attaches it to the
	 * current selection. Unlike the retired per-frame slaving, selecting an enemy
	 * no longer moves the camera - focusing does (input.md H3, Homeworld §3.6). */
	unsigned int s_followEntity = 0xFFFFFFFFu;

	/* Middle-button zoom drag: dragging up/down while MMB is held dollies. */
	constexpr double MMB_ZOOM_PER_PIXEL = 0.03;
	bool s_prevMmb = false;
	bool s_prevFocusKey = false;   // rising-edge detector for the F focus key

	/* Previous pointer, for the MMB zoom-drag delta (RMB/chord deltas come from the
	 * gesture recognizer via PointerInput). */
	int s_prevMouseX = 0;
	int s_prevMouseY = 0;
	bool s_prevRmb = false;

	/* Starfield motion cue state: the previous look angles + eye, so the dust can
	 * stream/pan with the camera the way it used to with the ship. */
	float s_prevYaw = 0.0f;
	float s_prevPitch = 0.0f;
	double s_prevEye[3] = {0, 0, 0};

	std::chrono::steady_clock::time_point s_prevTime;
	bool s_haveTime = false;

	/* Snap the rig behind the ship again when it teleports out from under us
	 * (hyperspace, in-system jump, respawn at a distant station). */
	constexpr double REANCHOR_DISTANCE = 200000.0;
	constexpr double ANCHOR_BACK = 700.0;   // behind the hull, along -nose
	constexpr double ANCHOR_UP = 180.0;     // above it, along +roof

	double FrameDt(void)
	{
		const auto now = std::chrono::steady_clock::now();
		double dt = 1.0 / 60.0;
		if (s_haveTime)
			dt = std::chrono::duration<double>(now - s_prevTime).count();
		s_prevTime = now;
		s_haveTime = true;
		if (dt < 0.0) dt = 0.0;
		if (dt > 0.1) dt = 0.1;   // clamp hitches so the camera never leaps
		return dt;
	}

	float KeyAxis(int _plusVk, int _minusVk)
	{
		float a = 0.0f;
		if (input_key_down(_plusVk)) a += 1.0f;
		if (input_key_down(_minusVk)) a -= 1.0f;
		return a;
	}

	Client::CameraController& Active(void)
	{
		if (s_orbitMode)
			return s_orbit;
		return s_fpv;
	}

	void AnchorBehindShip(const Neuron::Net::EntitySnapshot& _me)
	{
		const double ship[3] = {static_cast<double>(_me.x), static_cast<double>(_me.y), static_cast<double>(_me.z)};

		s_fpv.SetEyeWorld(ship[0] - _me.noseX * ANCHOR_BACK + _me.roofX * ANCHOR_UP,
						  ship[1] - _me.noseY * ANCHOR_BACK + _me.roofY * ANCHOR_UP,
						  ship[2] - _me.noseZ * ANCHOR_BACK + _me.roofZ * ANCHOR_UP);
		s_fpv.LookTowards(ship);

		s_orbit.SetTarget(ship);
		s_orbit.SetOrbit(s_fpv.YawAngle() + 3.14159265f, -0.35f, 900.0);

		/* Follow the own ship by default; F re-attaches the follow to the selection. */
		s_followEntity = Client::ReplicationClientInstance().LocalPlayer();
	}
}

void camera_rig_reset(void)
{
	s_ready = false;
	s_orbitMode = true;   // orbit is the default; a fresh scene starts in it
	s_origin[0] = s_origin[1] = s_origin[2] = 0;
	s_haveTime = false;

	/* Homeworld-state teardown (input.md §6.2): no focus ease, latched pan or
	 * live selection/grid survives a scene change (death -> intro -> flight). */
	s_orbit.CancelFocusAnim();
	s_followEntity = 0xFFFFFFFFu;
	ResetCommandState();            // clears selection / grid / band (main.cpp)
	Client::MainCamera().SetViewParams(DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
									   DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f),
									   DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
	set_starfield_motion(0.0, 0.0, 0.0);
}

void camera_rig_toggle_mode(void)
{
	if (!s_ready)
		return;

	if (!s_orbitMode)
	{
		/* Enter orbit roughly from where the free camera stands: keep the flown-to
		 * viewpoint by orbiting from the current eye offset to the target. */
		const double* e = s_fpv.EyeWorld();
		const double* t = s_orbit.TargetWorld();
		const double dx = e[0] - t[0], dy = e[1] - t[1], dz = e[2] - t[2];
		const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (dist > 1.0)
		{
			const double flat = std::sqrt(dx * dx + dz * dz);
			const float yaw = static_cast<float>(std::atan2(-dx, -dz));   // look direction = target - eye
			const float pitch = static_cast<float>(std::atan2(-dy, flat));
			s_orbit.SetOrbit(yaw, pitch, dist);
		}
	}
	else
	{
		/* Back to first person from the orbit eye, keeping the view on the target. */
		const double* e = s_orbit.EyeWorld();
		s_fpv.SetEyeWorld(e[0], e[1], e[2]);
		s_fpv.LookTowards(s_orbit.TargetWorld());
	}

	s_orbitMode = !s_orbitMode;
}

int camera_rig_ready(void) { return s_ready ? 1 : 0; }

const long long* camera_rig_origin(void) { return s_origin; }

void camera_rig_focus(void)
{
	/* Animate the focus point onto the current selection (else the own ship) and
	 * re-attach the follow to it (input.md H3: F key / double-tap). */
	const unsigned int want = g_missile_lock_target != 0xFFFFFFFFu
	                        ? g_missile_lock_target
	                        : Client::ReplicationClientInstance().LocalPlayer();

	/* Already following that unit and settled on it: nothing to animate (avoids a
	 * pointless half-second follow-pause when F re-targets the current subject). */
	if (want == s_followEntity && s_orbit.FocusSettled())
		return;

	double target[3];
	if (!SelectionFocusWorld(target))
		return;
	s_orbit.FocusOn(target);
	s_followEntity = want;
}

void camera_rig_update(void)
{
	Client::ReplicationClient& rc = Client::ReplicationClientInstance();
	const double dt = FrameDt();

	Neuron::Net::EntitySnapshot me{};
	const bool haveMe = rc.IsOpen() && rc.Sample(rc.LocalPlayer(), rc.InterpolationAlpha(), me);

	/* Anchor on first sight of the replicated ship, and re-anchor when the ship
	 * teleports away (hyperspace / jump / respawn) so the view follows it. */
	if (haveMe)
	{
		const double* eye = Active().EyeWorld();
		const double dx = static_cast<double>(me.x) - eye[0];
		const double dy = static_cast<double>(me.y) - eye[1];
		const double dz = static_cast<double>(me.z) - eye[2];
		const double shipDist = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (!s_ready || shipDist > REANCHOR_DISTANCE)
		{
			AnchorBehindShip(me);
			s_ready = true;
		}
	}

	if (!s_ready)
		return;   // nothing replicated yet: keep the identity camera

	/* Follow the focus ENTITY (input.md H3): the orbit camera tracks whatever
	 * s_followEntity names - the own ship by default, the selection after F /
	 * double-tap, or nothing (a free focus point) after a manual pan. Unlike the
	 * retired per-frame slaving, selecting an enemy does NOT move the camera. The
	 * per-frame snap resumes only once a FocusOn ease has landed, so the animated
	 * re-centre plays out first. A followed entity that despawns detaches. */
	if (s_orbitMode && s_followEntity != 0xFFFFFFFFu && s_orbit.FocusSettled())
	{
		Neuron::Net::EntitySnapshot ts{};
		if (rc.Sample(s_followEntity, rc.InterpolationAlpha(), ts))
		{
			const double target[3] = {static_cast<double>(ts.x), static_cast<double>(ts.y), static_cast<double>(ts.z)};
			s_orbit.SetTarget(target);
		}
		else if (s_followEntity != rc.LocalPlayer())
		{
			s_followEntity = 0xFFFFFFFFu;   // it left the world; the focus point stays put
		}
	}

	/* Docked: force the orbit camera around the station-parked ship for the whole
	 * docked stay; on launch, drop to the free (FPV) camera anchored behind the
	 * hull. RMB-drag rotate is now the GLOBAL binding (below), so no separate
	 * docked RMB path is needed - it just stays live while the menu owns LMB. */
	static bool s_prevDocked = false;
	if (docked)
	{
		if (!s_prevDocked && haveMe)
		{
			const double t[3] = {static_cast<double>(me.x), static_cast<double>(me.y), static_cast<double>(me.z)};
			s_orbit.SetTarget(t);
			s_orbit.SetOrbit(s_fpv.YawAngle() + 3.14159265f, -0.30f, 3000.0);
			s_followEntity = rc.LocalPlayer();
		}
		s_orbitMode = true;
	}
	else if (s_prevDocked)
	{
		if (haveMe)
			AnchorBehindShip(me);   // resets s_followEntity to the own ship
	}
	s_prevDocked = (docked != 0);

	/* Gather this frame's camera input through the H1 pointer front door. The GUI
	 * overlay owns the pointer/keys while a window is up, and on the non-flight
	 * screens the arrows belong to the chart crosshair - the camera goes quiet in
	 * both, leaving the wheel for an open window (the chart) to zoom with. A docked
	 * station menu owns LMB but RMB-drag still rotates around the station. */
	Client::CameraInput in{};
	in.dt = static_cast<float>(dt);
	in.tanHalfFovY = Client::CameraTanHalfFovY(Client::MainCamera());
	in.viewportH = static_cast<float>(Neuron::Graphics::Core::GetOutputSize().Height);

	int mx = 0, my = 0;
	bool lmb = false, rmb = false, mmb = false;
	PointerInput::MouseState(mx, my, lmb, rmb, mmb);

	const bool uiOwns = GuiOverlay::IsShown() || (current_screen != SCR_FRONT_VIEW) || g_radial_open;
	const bool cameraLive = !uiOwns || docked;   // docked keeps RMB rotate live
	bool panned = false;

	/* Always DRAIN the recognizer's drag/chord/pan deltas (even when the camera is
	 * not live), so nothing accumulates across a window/radial period and snaps the
	 * view when input becomes live again. They are only APPLIED when cameraLive. */
	float rdx = 0.f, rdy = 0.f;
	const bool rDrag = PointerInput::DragState(PointerButton::Right, rdx, rdy);
	float cdx = 0.f, cdy = 0.f;
	const bool chord = PointerInput::ChordPan(cdx, cdy);
	float tdx = 0.f, tdy = 0.f;
	input_take_pan(tdx, tdy);                    // two-finger touch pan (finally consumed)

	if (cameraLive)
	{
		if (!uiOwns)
			in.wheelSteps = input_take_mouse_wheel();   // wheel + touch pinch (I6 leaves it for windows)

		/* RMB-drag = ROTATE (the global binding). A drag owned by an in-progress
		 * move gizmo or an open radial menu is a command, not a camera rotate. */
		if (rDrag && !g_gizmo_active && !g_radial_open)
		{
			in.lookDX = rdx; in.lookDY = rdy; in.looking = true;
		}

		if (!uiOwns)
		{
			/* Pan: the LMB+RMB chord, the touch two-finger drag, and the arrow/WASD
			 * key axes all slide the focus point in the screen plane. */
			if (chord && (cdx != 0.f || cdy != 0.f))
			{
				in.panDX += cdx; in.panDY += cdy; in.panning = true;
			}
			if (tdx != 0.f || tdy != 0.f) { in.panDX += tdx; in.panDY += tdy; in.panning = true; }

			in.keyPanRight = KeyAxis(VK_RIGHT, VK_LEFT) + KeyAxis('D', 'A');
			in.keyPanUp    = KeyAxis(VK_UP, VK_DOWN)    + KeyAxis('W', 'S');
			if (in.keyPanRight != 0.f || in.keyPanUp != 0.f) in.panning = true;

			/* MMB-drag = zoom (vertical): dragging up dollies in, down dollies out. */
			if (mmb && s_prevMmb)
				in.wheelSteps += static_cast<float>((s_prevMouseY - my) * MMB_ZOOM_PER_PIXEL);

			/* FPV observer keeps the free-fly axes; Shift boosts only there (in orbit
			 * Shift is the move-grid's elevation modifier and does nothing to the view). */
			if (!s_orbitMode)
			{
				in.moveForward = KeyAxis(VK_UP, VK_DOWN);
				in.moveRight = KeyAxis(VK_RIGHT, VK_LEFT);
				in.moveUp = KeyAxis(VK_PRIOR, VK_NEXT);
				in.boost = input_key_down(VK_SHIFT);
			}

			/* Focus (F): animate the focus onto the selection (else the own ship) and
			 * re-attach the follow to it. A manual pan this frame detaches it. */
			const bool focusKey = input_key_down('F');
			if (focusKey && !s_prevFocusKey)
				camera_rig_focus();
			s_prevFocusKey = focusKey;

			panned = in.panning;
		}
	}

	/* A manual pan detaches the follow: the focus point becomes free until the next
	 * F / double-tap re-attaches it (Homeworld grab-the-world feel). */
	if (panned)
		s_followEntity = 0xFFFFFFFFu;

	s_prevMouseX = mx;
	s_prevMouseY = my;
	s_prevRmb = rmb;
	s_prevMmb = mmb;

	Client::CameraController& active = Active();
	active.Update(in);

	/* Floating origin: round the eye to whole world units and hand the camera the
	 * sub-unit remainder, so every rebased entity stays in small-float range. */
	const double* eye = active.EyeWorld();
	s_origin[0] = static_cast<long long>(std::llround(eye[0]));
	s_origin[1] = static_cast<long long>(std::llround(eye[1]));
	s_origin[2] = static_cast<long long>(std::llround(eye[2]));
	const double origin[3] = {static_cast<double>(s_origin[0]), static_cast<double>(s_origin[1]),
							  static_cast<double>(s_origin[2])};
	active.ApplyView(Client::MainCamera(), origin);

	/* Dust streaming cue: the starfield used to stream with the ship's speed and
	 * drift with its roll/climb; feed it the camera's motion instead - forward
	 * speed along the look, and the frame's look deltas as screen-space pan. */
	{
		Client::Camera& cam = Client::MainCamera();
		const float yaw = cam.Yaw();
		const float pitch = cam.Pitch();

		float dyaw = yaw - s_prevYaw;
		if (dyaw > 3.14159265f) dyaw -= 6.2831853f;
		if (dyaw < -3.14159265f) dyaw += 6.2831853f;
		const float dpitch = pitch - s_prevPitch;

		const double mvx = eye[0] - s_prevEye[0];
		const double mvy = eye[1] - s_prevEye[1];
		const double mvz = eye[2] - s_prevEye[2];
		const double cp = std::cos(pitch);
		const double look[3] = {cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)};
		const double fwdPerFrame = mvx * look[0] + mvy * look[1] + mvz * look[2];

		double cue = fwdPerFrame * 0.5;
		if (cue > 40.0) cue = 40.0;
		if (cue < -40.0) cue = -40.0;
		set_starfield_motion(cue, -dyaw * 180.0, dpitch * 180.0);

		s_prevYaw = yaw;
		s_prevPitch = pitch;
		s_prevEye[0] = eye[0];
		s_prevEye[1] = eye[1];
		s_prevEye[2] = eye[2];
	}
}

/* ---- world (origin-relative) -> camera-space, through the view matrix -------- */

void camera_view_point(struct vector* v)
{
	DirectX::XMFLOAT4X4 m;
	DirectX::XMStoreFloat4x4(&m, Client::MainCamera().View());

	/* Row-vector convention: p' = p * V (point transform includes the translation row). */
	const double x = v->x, y = v->y, z = v->z;
	v->x = x * m._11 + y * m._21 + z * m._31 + m._41;
	v->y = x * m._12 + y * m._22 + z * m._32 + m._42;
	v->z = x * m._13 + y * m._23 + z * m._33 + m._43;
}

void camera_view_object(struct local_object* obj)
{
	DirectX::XMFLOAT4X4 m;
	DirectX::XMStoreFloat4x4(&m, Client::MainCamera().View());

	const double x = obj->location.x, y = obj->location.y, z = obj->location.z;
	obj->location.x = x * m._11 + y * m._21 + z * m._31 + m._41;
	obj->location.y = x * m._12 + y * m._22 + z * m._32 + m._42;
	obj->location.z = x * m._13 + y * m._23 + z * m._33 + m._43;

	for (int i = 0; i < 3; i++)
	{
		const double bx = obj->rotmat[i].x, by = obj->rotmat[i].y, bz = obj->rotmat[i].z;
		obj->rotmat[i].x = bx * m._11 + by * m._21 + bz * m._31;   /* directions: no translation */
		obj->rotmat[i].y = bx * m._12 + by * m._22 + bz * m._32;
		obj->rotmat[i].z = bx * m._13 + by * m._23 + bz * m._33;
	}
}
