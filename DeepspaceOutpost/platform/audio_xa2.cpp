/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * audio_xa2.cpp  (M3)
 *
 * XAudio2 implementation of the sound.h contract for the 14 PCM sound effects.
 * Each effect gets a dedicated source voice; playing restarts it. The original
 * per-sample replay throttle (runtime/timeleft, ticked by snd_update_sound) is
 * preserved so rapid events don't machine-gun the same sample.
 *
 * Music (theme.mid / danube.mid) is deferred to M6 (Win32 MIDI); the MIDI hooks
 * are no-ops here.
 */

/* WIN32_LEAN_AND_MEAN / NOMINMAX come from the build (CMake). */
#include "pch.h"

#include <windows.h>
#include <mmsystem.h>
#include <xaudio2.h>
#include <winrt/base.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

#include "sound.h"
#include "audio_win.h"
#include "platform_win.h"

using winrt::com_ptr;

namespace {

constexpr int NUM_SAMPLES = 14;

struct Sample
{
	const char*            filename;
	int                    runtime;     /* throttle window, in frames */
	int                    timeleft;
	std::vector<uint8_t>   data;         /* PCM bytes */
	WAVEFORMATEX           fmt;
	IXAudio2SourceVoice*   voice;        /* owned by the engine */
};

Sample g_samples[NUM_SAMPLES] = {
	{ "launch.wav",   32, 0, {}, {}, nullptr },
	{ "crash.wav",     7, 0, {}, {}, nullptr },
	{ "dock.wav",     36, 0, {}, {}, nullptr },
	{ "gameover.wav", 24, 0, {}, {}, nullptr },
	{ "pulse.wav",     4, 0, {}, {}, nullptr },
	{ "hitem.wav",     4, 0, {}, {}, nullptr },
	{ "explode.wav",  23, 0, {}, {}, nullptr },
	{ "ecm.wav",      23, 0, {}, {}, nullptr },
	{ "missile.wav",  25, 0, {}, {}, nullptr },
	{ "hyper.wav",    37, 0, {}, {}, nullptr },
	{ "incom1.wav",    4, 0, {}, {}, nullptr },
	{ "incom2.wav",    5, 0, {}, {}, nullptr },
	{ "beep.wav",      2, 0, {}, {}, nullptr },
	{ "boop.wav",      7, 0, {}, {}, nullptr },
};

com_ptr<IXAudio2>         g_xaudio;
IXAudio2MasteringVoice*   g_master = nullptr;
bool                      g_on = false;

/* Background music via MCI (the "sequencer" device plays standard MIDI files
 * through the system synth). Looping is driven by MM_MCINOTIFY completion. */
bool g_midi_open = false;
bool g_midi_repeat = false;

void midi_play_from_start()
{
	mciSendStringW(L"play bgm from 0 notify", nullptr, 0, platform_window());
}

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

/* Parse a PCM RIFF/WAVE file into fmt + data. */
bool load_wav(const char* path, WAVEFORMATEX& fmt, std::vector<uint8_t>& data)
{
	FILE* fp = std::fopen(path, "rb");
	if (!fp) return false;
	std::vector<uint8_t> b;
	std::fseek(fp, 0, SEEK_END);
	long sz = std::ftell(fp);
	std::fseek(fp, 0, SEEK_SET);
	if (sz > 12) { b.resize(sz); if (std::fread(b.data(), 1, b.size(), fp) != b.size()) b.clear(); }
	std::fclose(fp);
	if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4))
		return false;

	bool haveFmt = false, haveData = false;
	size_t i = 12;
	while (i + 8 <= b.size())
	{
		const uint8_t* id = &b[i];
		uint32_t csz = rd32(&b[i + 4]);
		size_t body = i + 8;
		if (body + csz > b.size()) csz = static_cast<uint32_t>(b.size() - body);

		if (!std::memcmp(id, "fmt ", 4) && csz >= 16)
		{
			std::memset(&fmt, 0, sizeof(fmt));
			fmt.wFormatTag      = rd16(&b[body + 0]);
			fmt.nChannels       = rd16(&b[body + 2]);
			fmt.nSamplesPerSec  = rd32(&b[body + 4]);
			fmt.nAvgBytesPerSec = rd32(&b[body + 8]);
			fmt.nBlockAlign     = rd16(&b[body + 12]);
			fmt.wBitsPerSample  = rd16(&b[body + 14]);
			fmt.cbSize = 0;
			haveFmt = true;
		}
		else if (!std::memcmp(id, "data", 4))
		{
			data.assign(b.begin() + body, b.begin() + body + csz);
			haveData = true;
		}
		i = body + csz + (csz & 1);   /* chunks are word-aligned */
	}
	return haveFmt && haveData;
}

} // namespace

void snd_sound_startup(void)
{
	g_on = false;

	if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
	{
		/* Already initialised on this thread is fine; keep going. */
	}

	if (FAILED(XAudio2Create(g_xaudio.put(), 0, XAUDIO2_DEFAULT_PROCESSOR)))
		return;
	if (FAILED(g_xaudio->CreateMasteringVoice(&g_master)))
		return;

	for (int i = 0; i < NUM_SAMPLES; i++)
	{
		Sample& s = g_samples[i];
		if (!load_wav(s.filename, s.fmt, s.data) || s.data.empty())
			continue;
		if (FAILED(g_xaudio->CreateSourceVoice(&s.voice, &s.fmt)))
			s.voice = nullptr;
	}

	g_on = true;
}

void snd_sound_shutdown(void)
{
	if (!g_on) return;
	for (int i = 0; i < NUM_SAMPLES; i++)
	{
		if (g_samples[i].voice)
		{
			g_samples[i].voice->Stop();
			g_samples[i].voice->DestroyVoice();
			g_samples[i].voice = nullptr;
		}
	}
	if (g_master) { g_master->DestroyVoice(); g_master = nullptr; }
	g_xaudio = nullptr;
	g_on = false;
}

void snd_play_sample(int sample_no)
{
	if (!g_on || sample_no < 0 || sample_no >= NUM_SAMPLES)
		return;

	Sample& s = g_samples[sample_no];
	if (s.timeleft != 0 || !s.voice)
		return;
	s.timeleft = s.runtime;

	XAUDIO2_BUFFER buf{};
	buf.AudioBytes = static_cast<UINT32>(s.data.size());
	buf.pAudioData = s.data.data();
	buf.Flags      = XAUDIO2_END_OF_STREAM;

	s.voice->Stop();
	s.voice->FlushSourceBuffers();
	s.voice->SubmitSourceBuffer(&buf);
	s.voice->Start(0);
}

void snd_update_sound(void)
{
	for (int i = 0; i < NUM_SAMPLES; i++)
		if (g_samples[i].timeleft > 0)
			g_samples[i].timeleft--;
}

void snd_play_midi(int midi_no, int repeat)
{
	const wchar_t* file = (midi_no == SND_BLUE_DANUBE) ? L"danube.mid" : L"theme.mid";

	/* Close any track currently open under our alias. */
	mciSendStringW(L"close bgm", nullptr, 0, nullptr);
	g_midi_open = false;

	wchar_t cmd[256];
	swprintf(cmd, 256, L"open \"%ls\" type sequencer alias bgm", file);
	if (mciSendStringW(cmd, nullptr, 0, nullptr) != 0)
		return;

	g_midi_open = true;
	g_midi_repeat = (repeat != 0);
	midi_play_from_start();
}

void snd_stop_midi(void)
{
	if (!g_midi_open)
		return;
	g_midi_repeat = false;
	mciSendStringW(L"close bgm", nullptr, 0, nullptr);
	g_midi_open = false;
}

/* Called from the window procedure when an MCI play completes. */
void snd_midi_notify(void)
{
	if (g_midi_open && g_midi_repeat)
		midi_play_from_start();
}
