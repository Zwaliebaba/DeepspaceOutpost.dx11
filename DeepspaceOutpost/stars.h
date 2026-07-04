#ifndef STARS_H
#define STARS_H

extern int warp_stars;

void create_new_stars (void);
void update_starfield (void);

/* The camera's motion this frame, fed by the camera rig: forward speed along the
 * look (legacy speed scale, ~[-40,40]) and the frame's look deltas as star-space
 * pan. Drives the dust streaming/drift now that the camera is decoupled. */
void set_starfield_motion (double legacy_speed, double pan_x, double pan_y);

#endif

