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

