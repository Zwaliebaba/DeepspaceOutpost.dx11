#ifndef DOCKED_H
#define DOCKED_H

void display_short_range_chart (void);
void display_galactic_chart (void);
void display_data_on_planet (void);
void show_distance_to_planet (void);
void teleport_to_cursor (void);   // thin-client: teleport to the system at the chart crosshair
void move_cursor_to_origin (void);
void find_planet_by_name (char *find_name);
void display_market_prices (void);
void display_commander_status (void);
int calc_distance_to_planet (struct galaxy_seed from_planet, struct galaxy_seed to_planet);
void highlight_stock (int i);
void select_previous_stock (void);
void select_next_stock (void);
void buy_stock (void);
void sell_stock (void);

/* Render-free market accessors / actions, used by the GUI market window so it can
 * read and trade without driving the legacy gfx_display_* screen. */
int  market_item_count (void);
void market_format_row (int item, char *buf, int buflen); /* "Name  t   12.3   5t    -" */
int  market_credits (void);                                /* tenths of a credit */
int  market_buy (int item);                                /* returns 1 if state changed */
int  market_sell (int item);                               /* returns 1 if state changed */

/* Render-free read-only info screens for the GUI info windows. Each *_line_count()
 * rebuilds the line list from current game state; *_line() copies line i. */
int  cmdr_status_line_count (void);
void cmdr_status_line (int i, char *buf, int buflen);
void cmdr_status_title (char *buf, int buflen);
int  inventory_line_count (void);
void inventory_line (int i, char *buf, int buflen);
int  planet_data_line_count (void);
void planet_data_line (int i, char *buf, int buflen);
void planet_data_title (char *buf, int buflen);
void display_inventory (void);
void equip_ship (void);
void select_next_equip (void);
void select_previous_equip (void);
void buy_equip (void);

/* Render-free equip-screen access for the GUI equip window. */
int  equip_do (int index);          /* expand a laser sub-menu, or buy the item */
void equip_reset (void);            /* collapse to the top-level list */
int  equip_visible_count (void);    /* rebuilds the visible set + canbuy flags */
int  equip_visible_index (int i);   /* equip_stock index of the i-th visible row */
void equip_row_text (int index, char *buf, int buflen);
int  equip_buyable (int index);


extern int cross_x;
extern int cross_y;

#endif

