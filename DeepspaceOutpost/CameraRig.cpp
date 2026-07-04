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

	/* Starfield motion cue state: the previous look angles + eye, so the dust can
	 * stream/pan with the camera the way it used to with the ship. */
	float s_prevYaw = 0.0f;
	float s_prevPitch = 0.0f;
	double s_prevEye[3] = {0, 0, 0};

	std::chrono::steady_clock::time_point s_prevTime;
	bool s_haveTime = false;

	/* Snap the rig behind the ship again when it teleports out from under us
	 * (hyperspace, in-system jump, respawn at a distant station). */
	constexpr double kReanchorDistance = 200000.0;
	constexpr double kAnchorBack = 700.0;   // behind the hull, along -nose
	constexpr double kAnchorUp = 180.0;     // above it, along +roof

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

		s_fpv.SetEyeWorld(ship[0] - _me.noseX * kAnchorBack + _me.roofX * kAnchorUp,
						  ship[1] - _me.noseY * kAnchorBack + _me.roofY * kAnchorUp,
						  ship[2] - _me.noseZ * kAnchorBack + _me.roofZ * kAnchorUp);
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
		if (!s_ready || shipDist > kReanchorDistance)
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
	 * cases (the wheel is still consumed so notches don't burst through later). */
	Client::CameraInput in{};
	in.dt = static_cast<float>(dt);

	int mx = 0, my = 0;
	bool lmb = false, rmb = false;
	input_mouse_state(mx, my, lmb, rmb);
	const float wheel = input_take_mouse_wheel();

	const bool uiOwns = GuiOverlay::IsShown() || (current_screen != SCR_FRONT_VIEW);
	if (!uiOwns)
	{
		if (rmb && s_prevRmb)
		{
			in.lookDX = static_cast<float>(mx - s_prevMouseX);
			in.lookDY = static_cast<float>(my - s_prevMouseY);
		}
		in.looking = rmb;
		in.wheelSteps = wheel;

		/* Camera movement keys: the arrows + PgUp/PgDn, freed by the piloting
		 * removal (WASD stays with the combat bindings: A fires, D is chart
		 * distance, S is menu-up). Shift boosts. */
		in.moveForward = KeyAxis(VK_UP, VK_DOWN);
		in.moveRight = KeyAxis(VK_RIGHT, VK_LEFT);
		in.moveUp = KeyAxis(VK_PRIOR, VK_NEXT);
		in.boost = input_key_down(VK_SHIFT);
	}
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
