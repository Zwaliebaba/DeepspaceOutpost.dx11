/*
 * pilot.h
 */

#ifndef PILOT_H
#define PILOT_H

void fly_to_vector (struct univ_object *ship, Vector vec);
void auto_pilot_ship (struct univ_object *ship);
void engage_auto_pilot (void);
void disengage_auto_pilot (void);

#endif
 
