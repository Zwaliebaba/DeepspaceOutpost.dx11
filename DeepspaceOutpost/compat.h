/*
 * Elite - The New Kind  ->  DeepspaceOutpost (DirectX 11 / XAudio2 port)
 *
 * compat.h
 *
 * Small compatibility shim for the thin C++23 port. The original game logic
 * pulled a handful of trivial definitions (TRUE/FALSE, the boolean type) in
 * via Allegro's <allegro.h>. Now that the Allegro dependency is gone, those
 * come from here instead. This header carries NO platform dependencies and is
 * safe to include from any translation unit (game logic or platform layer).
 */

#ifndef COMPAT_H
#define COMPAT_H

#ifndef TRUE
#define TRUE	(1)
#endif

#ifndef FALSE
#define FALSE	(0)
#endif

#endif /* COMPAT_H */
