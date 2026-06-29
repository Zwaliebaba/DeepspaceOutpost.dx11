/*
 * file.c
 */

#include "pch.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elite.h"
#include "config.h"
#include "file.h"

/*
 * Return a pointer to the filename portion of a path (the part after the last
 * '/', '\\' or ':'). Mirrors the Allegro get_filename() the game relied on.
 */

char *get_filename (char *path)
{
	char *p = path;
	char *fname = path;

	for (; *p; p++)
	{
		if (*p == '/' || *p == '\\' || *p == ':')
			fname = p + 1;
	}

	return fname;
}


void write_config_file (void)
{
	FILE *fp;
	
	fp = fopen ("newkind.cfg", "w");
	if (fp == NULL)
		return;

	fprintf (fp, "%d\t\t# Game Speed, the lower the number the faster the game.\n", speed_cap);

	fprintf (fp, "%d\t\t# Graphics: 0 = Solid, 1 = Wireframe\n", wireframe);

	fprintf (fp, "%d\t\t# Anti-Alias Wireframe: 0 = Normal, 1 = Anti-Aliased\n", anti_alias_gfx);

	fprintf (fp, "%d\t\t# Planet style: 0 = Wireframe, 1 = Green, 2 = SNES, 3 = Fractal\n", planet_render_style);
	
	fprintf (fp, "%d\t\t# Planet Descriptions: 0 = Tree Grubs, 1 = Hoopy Casinos\n", hoopy_casinos);

	fprintf (fp, "%d\t\t# Instant dock: 0 = off, 1 = on\n", instant_dock);
	
	fprintf (fp, "newscan.cfg\t# Name of scanner config file to use.\n");

	fclose (fp);
}


/*
 * Read a line from a .cfg file.
 * Ignore blanks, comments and strip white space.
 */

void read_cfg_line (char *str, int max_size, FILE *fp)
{
	char *s;

	do
	{	
		fgets (str, max_size, fp);

		for (s = str; *s; s++)					/* End of line at LF or # */
		{
			if ((*s == '\n') || (*s == '#'))
			{
				*s = '\0';
				break;
			} 		
		}

		if (s != str)							/* Trim any trailing white space */
		{
			s--;
			while (isspace(*s))
			{
				*s = '\0';
				if (s == str)
					break;
				s--;
			}
		}

	} while (*str == '\0');
}


/*
 * Read in the scanner .cfg file.
 */

void read_scanner_config_file (char *filename)
{
	FILE *fp;
	char str[256];
	
	fp = fopen (filename, "r");
	if (fp == NULL)
		return;

	read_cfg_line (str, sizeof(str), fp);
	strcpy (scanner_filename, str);

	read_cfg_line (str, sizeof(str), fp);
	sscanf (str, "%d,%d", &scanner_cx, &scanner_cy);
	scanner_cy += 385;

	read_cfg_line (str, sizeof(str), fp);
	sscanf (str, "%d,%d", &compass_centre_x, &compass_centre_y);
	compass_centre_y += 385;
	
	fclose (fp);
}

/*
 * Read in the newkind.cfg file.
 */

void read_config_file (void)
{
	FILE *fp;
	char str[256];
	
	fp = fopen ("newkind.cfg", "r");
	if (fp == NULL)
		return;

	read_cfg_line (str, sizeof(str), fp);
	sscanf (str, "%d", &speed_cap);

	read_cfg_line (str, sizeof(str), fp);
	sscanf (str, "%d", &wireframe);

	read_cfg_line (str, sizeof(str), fp);
	sscanf (str, "%d", &anti_alias_gfx);

	read_cfg_line (str, sizeof(str), fp);
	sscanf (str, "%d", &planet_render_style);
	
	read_cfg_line (str, sizeof(str), fp);
	sscanf (str, "%d", &hoopy_casinos);

	read_cfg_line (str, sizeof(str), fp);
	sscanf (str, "%d", &instant_dock);

	read_cfg_line (str, sizeof(str), fp);
	read_scanner_config_file (str);
		
	fclose (fp);
}

