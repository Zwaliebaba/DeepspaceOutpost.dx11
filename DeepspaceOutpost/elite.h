#ifndef ELITE_H
#define ELITE_H

#include "planet.h"
#include "trade.h"


#define SCR_INTRO_ONE		1
#define SCR_INTRO_TWO		2
// (SCR_GALACTIC_CHART 3, SCR_SHORT_RANGE 4, SCR_PLANET_DATA 5 retired: the charts
//  and system-data panel are the native ChartWindow now. SCR_CMDR_STATUS 7 retired:
//  the docked view is the camera-space scene + the native StationMenuWindow.)
#define SCR_FRONT_VIEW		8
// (SCR_REAR_VIEW 9, SCR_LEFT_VIEW 10, SCR_RIGHT_VIEW 11 retired: the cockpit
//  has a single fixed forward view now; F2-F4 no longer switch views.)
// (SCR_BREAK_PATTERN 12 retired: the launch/dock/hyperspace transition was a
//  first-person cockpit effect; third-person just switches the view directly.)
#define SCR_INVENTORY		13
#define SCR_LOAD_CMDR		16
#define SCR_SAVE_CMDR		17
#define SCR_GAME_OVER		19
#define SCR_ESCAPE_POD		21
// (SCR_MARKET_PRICES, SCR_EQUIP_SHIP, SCR_OPTIONS, SCR_QUIT, SCR_SETTINGS retired with
//  the legacy gfx_display_* screens; those screens are GUI windows now.)


#define PULSE_LASER		0x0F
#define BEAM_LASER		0x8F
#define MILITARY_LASER	0x97
#define MINING_LASER	0x32


#define FLG_DEAD			(1)
#define	FLG_REMOVE			(2)
#define FLG_EXPLOSION		(4)
#define FLG_ANGRY			(8)
#define FLG_FIRING			(16)
#define FLG_HAS_ECM			(32)
#define FLG_HOSTILE			(64)
#define FLG_CLOAKED			(128)
#define FLG_FLY_TO_PLANET	(256)
#define FLG_FLY_TO_STATION	(512)
#define FLG_INACTIVE		(1024)
#define FLG_SLOW			(2048)
#define FLG_BOLD			(4096)
#define FLG_POLICE			(8192)


#define MAX_LOCAL_OBJECTS	20


struct commander
{
	char name[32];
	int mission;
	int ship_x;
	int ship_y;
	struct galaxy_seed galaxy;
	int credits;
	int fuel;
	int unused1;
	int	galaxy_number;
	int front_laser;	/* the ship's one laser mount (front view only) */
	int unused2;
	int unused3;
	int cargo_capacity;
	int current_cargo[NO_OF_STOCK_ITEMS];
	int ecm;
	int fuel_scoop;
	int energy_bomb;
	int energy_unit;
	int docking_computer;
	int galactic_hyperdrive;
	int escape_pod;
	int unused4;
	int unused5;
	int unused6;
	int unused7;
	int missiles;
	int legal_status;
	int station_stock[NO_OF_STOCK_ITEMS];
	int market_rnd;
	int score;
	int saved;
};

/*
 * The player ship's capabilities formerly lived in the `myship` global
 * (struct player_ship). They are now the Neuron::Game::ShipCaps component on
 * the player entity, reached via PlayerCaps() (GameUniverse.h).
 */


extern struct commander cmdr;
extern struct commander saved_cmdr;

extern struct galaxy_seed docked_planet;

extern struct galaxy_seed hyperspace_planet;

extern struct planet_data current_planet_data;

extern int carry_flag;
extern int current_screen;

extern struct ship_data *ship_list[];

extern int anti_alias_gfx;
extern char scanner_filename[256];
extern int hoopy_casinos;
extern int instant_dock;
extern int speed_cap;
extern int scanner_cx;
extern int scanner_cy;
extern int compass_centre_x;
extern int compass_centre_y;

extern int scene_shading;

extern int game_over;
extern int docked;
extern int finish;
extern int mcount;
extern int witchspace;


void restore_saved_commander (void);


#endif
