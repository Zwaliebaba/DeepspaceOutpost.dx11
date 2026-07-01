 /*
  * intro.c
  *
  * Run the two intro screens.
  * First is a rolling Cobra MkIII.
  * Second is a parade of the various ships.
  *
  */
 
 
#include "pch.h"

#include <stdlib.h>

#include "config.h"
#include "elite.h"
#include "gfx.h"
#include "GameUniverse.h"
#include "vector.h"
#include "shipdata.h"
#include "shipface.h"
#include "threed.h"
#include "space.h"
#include "stars.h"

static int ship_no;
static int show_time;
static int direction;


static int min_dist[NO_OF_SHIPS+1] = {0, 200, 800, 200,   200, 200, 300, 384,   200,
								  200, 200, 420, 900, 500, 800, 384, 384,
							      384, 384, 384, 200, 384, 384, 384,   0,
								  384,   0, 384, 384, 700, 384,   0,   0,
							 	  900};


static Matrix intro_ship_matrix;


void initialise_intro1 (void)
{
	clear_local_objects();
	set_init_matrix (intro_ship_matrix);
	add_new_ship (SHIP_COBRA3, 0, 0, 4500, intro_ship_matrix, -127, -127);
}


void initialise_intro2 (void)
{
	ship_no = 0;
	show_time = 0;
	direction = 100;

	clear_local_objects();
	create_new_stars();
	set_init_matrix (intro_ship_matrix);
	add_new_ship (1, 0, 0, 5000, intro_ship_matrix, -127, -127);
}



void update_intro1 (void)
{
	// Client-space intro: the ship fills the window (FOV-preserving optics), the title
	// sprite centres on the window and the prompts anchor to the bottom edge. The (514-y)
	// offsets keep the legacy distance-from-bottom of the old 512x514 layout.
	gfx_set_scene_fullwindow (1);
	int ch;
	gfx_canvas_size (nullptr, &ch);

	local_objects[0].location.z -= 100;

	if (local_objects[0].location.z < 384)
		local_objects[0].location.z = 384;

	gfx_clear_display();

	PlayerFlight().roll = 1;
	update_local_objects();

	gfx_draw_sprite(IMG_ELITE_TXT, -1, 10);

	gfx_display_centre_text (ch - 194, "DEEPSPACE OUTPOST", 120, GFX_COL_WHITE);
	gfx_display_centre_text (ch - 154, "Press Space to Begin, Commander.", 140, GFX_COL_GOLD);
}


void update_intro2 (void)
{
	show_time++;

	if ((show_time >= 140) && (direction < 0))
		direction = -direction;

	local_objects[0].location.z += direction;

	if (local_objects[0].location.z < min_dist[ship_no])
		local_objects[0].location.z = min_dist[ship_no];

	if (local_objects[0].location.z > 4500)
	{
		do
		{
			ship_no++;
			if (ship_no > NO_OF_SHIPS)
				ship_no = 1;
		} while (min_dist[ship_no] == 0);

		show_time = 0;
		direction = -100;

		ship_count[local_objects[0].type] = 0;
		local_objects[0].type = 0;		

		add_new_ship (ship_no, 0, 0, 4500, intro_ship_matrix, -127, -127);
	}


	// Client-space intro (see update_intro1): full-window ship parade + starfield, title
	// centred, ship name + prompt anchored to the bottom edge.
	gfx_set_scene_fullwindow (1);
	int ch;
	gfx_canvas_size (nullptr, &ch);

	gfx_clear_display();
	update_starfield();
	update_local_objects();

	gfx_draw_sprite (IMG_ELITE_TXT, -1, 10);

	gfx_display_centre_text (ch - 184, ship_list[ship_no]->name, 120, GFX_COL_WHITE);
	gfx_display_centre_text (ch - 154, "Press Fire or Space, Commander.", 140, GFX_COL_GOLD);
}

