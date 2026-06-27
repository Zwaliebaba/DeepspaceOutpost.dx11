/*
 * space.h
 */

#ifndef SPACE_H
#define SPACE_H

#include "vector.h"
#include "shipdata.h"

struct point
{
	int x;
	int y;
	int z;
};


struct local_object
{
	int type;
	Vector location;
	Matrix rotmat;
	int rotx;
	int rotz;
	int flags;
	int energy;
	int velocity;
	int acceleration;
	int missiles;
	int target;
	int bravery;
	int exp_delta;
	int exp_seed;
	int distance;
};

#define MAX_LOCAL_OBJECTS	20

extern struct local_object local_objects[MAX_LOCAL_OBJECTS];
extern int ship_count[NO_OF_SHIPS + 1];  /* many */



void clear_local_objects (void);
int add_new_ship (int ship_type, int x, int y, int z, struct vector *rotmat, int rotx, int rotz);
void add_new_station (double sx, double sy, double sz, Matrix rotmat);
void remove_ship (int un);
void move_local_object (struct local_object *obj);
void update_local_objects (void);

void update_console (void);

void update_altitude (void);
void update_cabin_temp (void);
void regenerate_shields (void);

void increase_flight_roll (void);
void decrease_flight_roll (void);
void increase_flight_climb (void);
void decrease_flight_climb (void);
void dock_player (void);

void damage_ship (int damage, int front);
void decrease_energy (int amount);

extern int hyper_ready;

void start_hyperspace (void);
void start_galactic_hyperspace (void);
void display_hyper_status (void);
void countdown_hyperspace (void);
void jump_warp (void);
void launch_player (void);

void engage_docking_computer (void);

#endif

