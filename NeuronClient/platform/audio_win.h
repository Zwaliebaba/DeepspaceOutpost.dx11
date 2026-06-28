/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * audio_win.h
 *
 * Platform hook: the window procedure forwards MCI completion (MM_MCINOTIFY)
 * here so looping background music can restart.
 */

#ifndef AUDIO_WIN_H
#define AUDIO_WIN_H

void snd_midi_notify(void);   /* called on MM_MCINOTIFY */

#endif /* AUDIO_WIN_H */
