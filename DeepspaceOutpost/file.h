/*
 * Elite - The New Kind.
 *
 * Reverse engineered from the BBC disk version of Elite.
 * Additional material by C.J.Pinder.
 *
 * The original Elite code is (C) I.Bell & D.Braben 1984.
 * This version re-engineered in C by C.J.Pinder 1999-2001.
 *
 * email: <christian@newkind.co.uk>
 *
 *
 */

/*
 * file.h
 */

#ifndef FILE_H
#define FILE_H

void write_config_file (void);
void read_config_file (void);
int save_commander_file (char *path);
int load_commander_file (char *path);

/* Return a pointer to the filename portion of a path (after the last path
 * separator or drive colon). Replaces the Allegro get_filename(). */
char *get_filename (char *path);

#endif

