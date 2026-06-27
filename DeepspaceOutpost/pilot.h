/*
 * pilot.h
 */

#ifndef PILOT_H
#define PILOT_H

void fly_to_vector (struct local_object *ship, Vector vec);
void auto_pilot_ship (struct local_object *ship);
void engage_auto_pilot (void);
void disengage_auto_pilot (void);

#endif
 
