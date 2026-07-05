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
#include "input_win.h"           // raw mouse / wheel / key state (camera policy keys)

using namespace Neuron;

namespace
{
	Client::FirstPersonCameraController s_fpv;
	Client::OrbitCameraController s_orbit;
	bool s_orbitMode = false;

	bool s_ready = false;
	long long s_origin[3] = {0, 0, 0};

	/* Mouse-look drag state (deltas are computed here; the platform reports the
	 * absolute pointer). */
	int s_prevMouseX = 0;
	int s_prevMouseY = 0;
	bool s_prevRmb = false;

	/* I2 pointer selection: an LMB press-then-release that stayed within the slop
	 * is a CLICK (select the entity under the cursor); a drag beyond it is not (it
	 * is reserved for the camera - RMB orbits today, so LMB-drag is simply ignored).
	 */
	bool s_prevLmb = false;
	int  s_lmbDownX = 0;
	int  s_lmbDownY = 0;
	bool s_lmbMoved = false;
	constexpr int CLICK_SLOP = 6;   // pixels of travel that still counts as a click

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
	}
}

void camera_rig_reset(void)
{
	s_ready = false;
	s_orbitMode = false;
	s_origin[0] = s_origin[1] = s_origin[2] = 0;
	s_haveTime = false;
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

	/* The orbit "selected object": the missile-locked entity (T key) while it is
	 * still replicated, else the player's own ship. Fed every frame - targets move. */
	if (g_missile_lock_target != 0xFFFFFFFFu)
	{
		Neuron::Net::EntitySnapshot ts{};
		if (rc.Sample(g_missile_lock_target, rc.InterpolationAlpha(), ts))
		{
			const double target[3] = {static_cast<double>(ts.x), static_cast<double>(ts.y), static_cast<double>(ts.z)};
			s_orbit.SetTarget(target);
		}
	}
	else if (haveMe)
	{
		const double target[3] = {static_cast<double>(me.x), static_cast<double>(me.y), static_cast<double>(me.z)};
		s_orbit.SetTarget(target);
	}

	/* Gather this frame's camera input. The GUI overlay owns the pointer and the
	 * keys while a window is up, and on the non-flight screens (charts, status)
	 * the arrows belong to the chart crosshair - the camera goes quiet in both
	 * cases, and it leaves the wheel unconsumed so an open window (the chart) can
	 * zoom with it (I6). */
	Client::CameraInput in{};
	in.dt = static_cast<float>(dt);

	int mx = 0, my = 0;
	bool lmb = false, rmb = false;
	input_mouse_state(mx, my, lmb, rmb);

	/* The camera also goes quiet while a radial command menu is open (I3/I5): the
	 * finger driving the menu highlight must not orbit or select underneath it. */
	const bool uiOwns = GuiOverlay::IsShown() || (current_screen != SCR_FRONT_VIEW) || g_radial_open;
	if (!uiOwns)
	{
		/* Consume the wheel only when the camera owns input; when a GUI window is up
		 * (e.g. the chart) it leaves the wheel for that window to zoom with (I6). */
		in.wheelSteps = input_take_mouse_wheel();

		/* I3: camera orbit is LMB-DRAG now (an LMB click without a drag is I2
		 * selection; RMB is freed for the pointer commands in main.cpp). Look only
		 * once the press has crossed the slop, so a click never nudges the view. A
		 * press that began on the I4 ability bar belongs to the bar, not the camera. */
		const bool lmbDrag = lmb && s_lmbMoved
		                  && ability_bar_button_at(s_lmbDownX, s_lmbDownY) < 0
		                  && nav_strip_button_at(s_lmbDownX, s_lmbDownY) < 0;
		if (lmbDrag && s_prevLmb)
		{
			in.lookDX = static_cast<float>(mx - s_prevMouseX);
			in.lookDY = static_cast<float>(my - s_prevMouseY);
		}
		in.looking = lmbDrag;

		/* Camera movement keys: the arrows + PgUp/PgDn, freed by the piloting
		 * removal (WASD stays with the combat bindings: A fires, D is chart
		 * distance, S is menu-up). Shift boosts. */
		in.moveForward = KeyAxis(VK_UP, VK_DOWN);
		in.moveRight = KeyAxis(VK_RIGHT, VK_LEFT);
		in.moveUp = KeyAxis(VK_PRIOR, VK_NEXT);
		in.boost = input_key_down(VK_SHIFT);
	}
	/* I2 pointer selection: track the LMB press so a release inside the slop is a
	 * click. On the flight screen with no UI in front, a click selects the entity
	 * under the cursor (empty space clears) - the reticle, orbit subject and missile
	 * target all follow g_missile_lock_target. A drag beyond the slop is left to the
	 * camera. */
	if (lmb && !s_prevLmb)
	{
		s_lmbDownX = mx;
		s_lmbDownY = my;
		s_lmbMoved = false;
	}
	else if (lmb)
	{
		int ddx = mx - s_lmbDownX; if (ddx < 0) ddx = -ddx;
		int ddy = my - s_lmbDownY; if (ddy < 0) ddy = -ddy;
		if (ddx > CLICK_SLOP || ddy > CLICK_SLOP)
			s_lmbMoved = true;
	}
	else if (s_prevLmb && !s_lmbMoved && !uiOwns
	         && ability_bar_button_at(s_lmbDownX, s_lmbDownY) < 0     // not a bar click (I4)
	         && nav_strip_button_at(s_lmbDownX, s_lmbDownY) < 0)      // not a nav-strip click (I4)
	{
		g_missile_lock_target = pick_entity_at_screen(mx, my);
	}
	s_prevLmb = lmb;

	s_prevMouseX = mx;
	s_prevMouseY = my;
	s_prevRmb = rmb;

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
