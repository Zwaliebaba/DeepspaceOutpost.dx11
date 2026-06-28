/*
 * sound.h
 */
 
#ifndef SOUND_H
#define SOUND_H

#define SND_LAUNCH		0
#define SND_CRASH		1
#define SND_DOCK		2
#define SND_GAMEOVER	3
#define SND_PULSE		4
#define SND_HIT_ENEMY	5
#define SND_EXPLODE		6
#define SND_ECM			7
#define SND_MISSILE		8
#define SND_HYPERSPACE	9
#define SND_INCOMMING_FIRE_1	10
#define SND_INCOMMING_FIRE_2	11
#define SND_BEEP		12
#define SND_BOOP		13

#define SND_ELITE_THEME 0
#define SND_BLUE_DANUBE 1

void snd_sound_startup (void);
void snd_sound_shutdown (void);
void snd_play_sample (int sample_no);
void snd_play_midi (int midi_no, int repeat);
void snd_update_sound (void);
void snd_stop_midi (void);

#endif
