#include "pch.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

#include <string>
#include <vector>

#include "gfx.h"
#include "GameUniverse.h"
#include "elite.h"
#include "planet.h"
#include "shipdata.h"
#include "space.h"
#include "sound.h"
#include "ReplicationClient.h"
#include "Messages/Defs/Travel.h"   // TravelRequest (hyperspace / jump drive)
#include "ChartData.h"              // render-free chart data API for the native ChartWindow





char *economy_type[] = {"Rich Industrial",
						"Average Industrial",
						"Poor Industrial",
						"Mainly Industrial",
						"Mainly Agricultural",
						"Rich Agricultural",
						"Average Agricultural",
						"Poor Agricultural"};

char *government_type[] = {	"Anarchy",
							"Feudal",
							"Multi-Government",
							"Dictatorship",
							"Communist",
							"Confederacy",
							"Democracy",
							"Corporate State"};






int cross_x = 0;
int cross_y = 0;

// Manifest index of the system the player last selected on a chart (the teleport
// target / "hyperspace system"), or -1 before any selection. Updated as the chart
// crosshair settles; read by the status screen so it agrees with the chart.
static int g_chart_selected = -1;


// Thin-client galactic chart helpers, consumed by the render-free ChartData API
// (the native ChartWindow) further down.
int chart_current_system (void);
static void chart_project_all (const std::vector<Neuron::Net::GalaxySystemInfo>& _g,
							   std::vector<int>& _px, std::vector<int>& _py);





int calc_distance_to_planet (struct galaxy_seed from_planet, struct galaxy_seed to_planet)
{
	int dx,dy;
	int light_years;

	dx = abs(to_planet.d - from_planet.d);
	dy = abs(to_planet.b - from_planet.b);

	dx = dx * dx;
	dy = dy / 2;
	dy = dy * dy;

	light_years = sqrt(dx + dy);
	light_years *= 4;

	return light_years;
}


void show_distance (int ypos, struct galaxy_seed from_planet, struct galaxy_seed to_planet)
{
	char str[100];
	int light_years;

	light_years = calc_distance_to_planet (from_planet, to_planet);
	
	if (light_years > 0)
		sprintf (str, "Distance: %2d.%d Light Years ", light_years / 10, light_years % 10);
	else
		strcpy (str,"                                                     ");

	gfx_display_text (16, ypos, str);
}



// ===== Thin-client galactic chart (driven by the server's galaxy manifest) =====
//
// The legacy chart walks the procedural galaxy_seed locally; in MMO mode the
// galaxy is the server's, delivered once as a manifest. We plot those systems,
// let the existing chart crosshair roam over them, and teleport (while docked) to
// whichever system is nearest the crosshair. No generation happens client-side.

// Project the manifest's planet positions (world x,z) into chart pixels, scaling
// to the manifest's own bounds so the whole galaxy fits whatever its extent is.
static void chart_project_all (const std::vector<Neuron::Net::GalaxySystemInfo>& _g,
							   std::vector<int>& _px, std::vector<int>& _py)
{
	const int L = 12, R = 499, T = 48, B = 285;   // chart area in actual pixels
	long long minX = _g[0].x, maxX = _g[0].x, minZ = _g[0].z, maxZ = _g[0].z;
	for (size_t i = 0; i < _g.size(); i++)
	{
		if (_g[i].x < minX) minX = _g[i].x;
		if (_g[i].x > maxX) maxX = _g[i].x;
		if (_g[i].z < minZ) minZ = _g[i].z;
		if (_g[i].z > maxZ) maxZ = _g[i].z;
	}
	_px.resize (_g.size());
	_py.resize (_g.size());
	for (size_t i = 0; i < _g.size(); i++)
	{
		double nx = (maxX > minX) ? (double)(_g[i].x - minX) / (double)(maxX - minX) : 0.5;
		double nz = (maxZ > minZ) ? (double)(_g[i].z - minZ) / (double)(maxZ - minZ) : 0.5;
		_px[i] = L + (int)(nx * (R - L));
		_py[i] = T + (int)(nz * (B - T));
	}
}

// The short range chart shares the galactic chart's manifest, but instead of
// scaling the whole galaxy to fit it shows a zoomed view around the player's
// current system. World units are converted to light years so the fuel circle
// (drawn in light years) lines up with the plotted neighbours.
static const long long SR_UNITS_PER_LY = 2'000'000LL;   // world units per light year
static const int       SR_PX_PER_LY    = 20;            // chart pixels per light year (isotropic)

// Index of the manifest system the player's ship is currently in (the nearest
// system to the live ship position), or -1 with no galaxy. Falls back to system 0
// until the first snapshot carrying the ship's position has arrived.
int chart_current_system (void)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !rc.HasGalaxy())
		return -1;

	Neuron::Net::EntitySnapshot ship;
	if (!rc.Sample (rc.LocalPlayer(), 1.0, ship))
		return 0;

	const std::vector<Neuron::Net::GalaxySystemInfo>& g = rc.Galaxy();
	int best = 0;
	long long bestD = 1LL << 62;
	for (size_t i = 0; i < g.size(); i++)
	{
		long long dx = g[i].x - ship.x;
		long long dz = g[i].z - ship.z;
		long long d = dx * dx + dz * dz;
		if (d < bestD)
		{
			bestD = d;
			best = (int)i;
		}
	}
	return best;
}

// Name of the player's current system. In thin-client mode this is the manifest
// name of the system the ship is in (so it matches the galactic chart); otherwise
// the legacy procedural name of docked_planet. `_out` must hold at least 16 chars.
void current_system_name (char *_out)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (rc.IsOpen() && rc.HasGalaxy())
	{
		const int cur = chart_current_system();
		if (cur >= 0 && cur < (int)rc.Galaxy().size())
		{
			strncpy (_out, rc.Galaxy()[cur].name, 15);
			_out[15] = '\0';
			return;
		}
	}

	name_planet (_out, docked_planet);
}

// Name of the player's selected hyperspace (teleport) target. In thin-client mode
// this is the manifest name of the system last picked on a chart, defaulting to
// the current system before any selection (mirroring the legacy start where the
// hyperspace target equals the present system); otherwise the legacy procedural
// name of hyperspace_planet. `_out` must hold at least 16 chars.
void hyperspace_system_name (char *_out)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (rc.IsOpen() && rc.HasGalaxy())
	{
		if (g_chart_selected >= 0 && g_chart_selected < (int)rc.Galaxy().size())
		{
			strncpy (_out, rc.Galaxy()[g_chart_selected].name, 15);
			_out[15] = '\0';
			return;
		}

		current_system_name (_out);
		return;
	}

	name_planet (_out, hyperspace_planet);
}

// Project the manifest into short-range chart pixels, centred on `_origin`
// (the current system) and scaled by light years. Uses the same world x/z axes
// as the galactic chart, so the two charts agree on where systems lie.
static void chart_project_short_range (const std::vector<Neuron::Net::GalaxySystemInfo>& _g,
									   int _origin, std::vector<int>& _px, std::vector<int>& _py)
{
	const long long ox = (_origin >= 0 && _origin < (int)_g.size()) ? _g[_origin].x : 0;
	const long long oz = (_origin >= 0 && _origin < (int)_g.size()) ? _g[_origin].z : 0;
	_px.resize (_g.size());
	_py.resize (_g.size());
	for (size_t i = 0; i < _g.size(); i++)
	{
		double lyx = (double)(_g[i].x - ox) / (double)SR_UNITS_PER_LY;
		double lyz = (double)(_g[i].z - oz) / (double)SR_UNITS_PER_LY;
		_px[i] = ChartData::PLOT_W / 2 + (int)lround (lyx * SR_PX_PER_LY);
		_py[i] = ChartData::PLOT_H / 2 + (int)lround (lyz * SR_PX_PER_LY);
	}
}

// ===== Render-free chart data API (ChartData.h) ================================
//
// Drawing-free accessors over the replicated galaxy manifest for the native
// ChartWindow (GameWindows.cpp), which draws in the GUI overlay's Render2D pass and so
// cannot touch the legacy gfx_* layer. Everything is expressed in the fixed chart-canvas
// pixel space (ChartData::PLOT_W x PLOT_H). The crosshair (cross_x/cross_y) and the
// selection (g_chart_selected) are the SAME state the legacy chart used, so the two
// agree during the transition.

namespace
{
	// Per-frame projection cache filled by ChartData::Begin.
	std::vector<int> s_plotPx, s_plotPy;

	// Nearest manifest system to the crosshair, projected for an EXPLICIT kind. (Unlike
	// chart_nearest_to_cursor, which reads current_screen - the native window no longer
	// sets that, so it passes the kind directly.)
	int chart_nearest_for_kind (int _kind, const std::vector<Neuron::Net::GalaxySystemInfo>& _g)
	{
		std::vector<int> px, py;
		if (_kind == ChartData::SHORT_RANGE)
			chart_project_short_range (_g, chart_current_system(), px, py);
		else
			chart_project_all (_g, px, py);

		int best = 0;
		long long bestD = 1LL << 62;
		for (size_t i = 0; i < _g.size(); i++)
		{
			long long dx = px[i] - cross_x;
			long long dy = py[i] - cross_y;
			long long d = dx * dx + dy * dy;
			if (d < bestD) { bestD = d; best = (int) i; }
		}
		return best;
	}
}

bool ChartData::Ready (void)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	return rc.IsOpen() && rc.HasGalaxy();
}

int ChartData::Count (void)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	return (rc.IsOpen() && rc.HasGalaxy()) ? (int) rc.Galaxy().size() : 0;
}

void ChartData::Begin (int _kind)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !rc.HasGalaxy()) { s_plotPx.clear(); s_plotPy.clear(); return; }
	const std::vector<Neuron::Net::GalaxySystemInfo>& g = rc.Galaxy();
	if (_kind == ChartData::SHORT_RANGE)
		chart_project_short_range (g, chart_current_system(), s_plotPx, s_plotPy);
	else
		chart_project_all (g, s_plotPx, s_plotPy);
}

int ChartData::X (int _i) { return (_i >= 0 && _i < (int) s_plotPx.size()) ? s_plotPx[_i] : 0; }
int ChartData::Y (int _i) { return (_i >= 0 && _i < (int) s_plotPy.size()) ? s_plotPy[_i] : 0; }

bool ChartData::Visible (int _i)
{
	if (_i < 0 || _i >= (int) s_plotPx.size()) return false;
	const int x = s_plotPx[_i], y = s_plotPy[_i];
	return x >= 2 && x <= PLOT_W - 2 && y >= 2 && y <= PLOT_H - 2;
}

void ChartData::Name (int _i, char* _buf, int _buflen)
{
	if (_buflen <= 0) return;
	_buf[0] = '\0';
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !rc.HasGalaxy() || _i < 0 || _i >= (int) rc.Galaxy().size()) return;
	char name[16];
	strncpy (name, rc.Galaxy()[_i].name, sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	capitalise_name (name);
	strncpy (_buf, name, _buflen - 1);
	_buf[_buflen - 1] = '\0';
}

int ChartData::Blob (int _i)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !rc.HasGalaxy() || _i < 0 || _i >= (int) rc.Galaxy().size()) return ChartData::SCALE * 2;
	const Neuron::Net::GalaxySystemInfo& s = rc.Galaxy()[_i];
	const int blob = ((s.economy ^ s.techLevel) & 1) + (s.government & 1) + 2;   // 2..4, echoing the legacy blobs
	return blob * ChartData::SCALE;
}

int ChartData::CurrentIndex (void) { return chart_current_system(); }
int ChartData::SelectedIndex (void) { return g_chart_selected; }

bool ChartData::FuelCircle (int _kind, int* _cx, int* _cy, int* _r)
{
	if (_kind != ChartData::SHORT_RANGE) return false;   // the galactic chart draws no fuel ring
	// Short-range is centred on the current system, so the fuel ring sits at the chart
	// centre; radius is the fuel range in chart px (cmdr.fuel is tenths of a light year).
	if (_cx) *_cx = ChartData::PLOT_W / 2;
	if (_cy) *_cy = ChartData::PLOT_H / 2;
	if (_r)  *_r  = cmdr.fuel * ChartData::SCALE;
	return true;
}

void ChartData::GetCursor (int* _cx, int* _cy)
{
	if (_cx) *_cx = cross_x;
	if (_cy) *_cy = cross_y;
}

void ChartData::SetCursor (int _kind, int _cx, int _cy)
{
	if (_cx < 1) _cx = 1;
	if (_cx > PLOT_W - 2) _cx = PLOT_W - 2;
	if (_cy < 1) _cy = 1;
	if (_cy > PLOT_H - 2) _cy = PLOT_H - 2;
	cross_x = _cx;
	cross_y = _cy;

	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (rc.IsOpen() && rc.HasGalaxy() && !rc.Galaxy().empty())
		g_chart_selected = chart_nearest_for_kind (_kind, rc.Galaxy());
}

void ChartData::Jump (int _kind)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !rc.HasGalaxy() || rc.Galaxy().empty()) return;

	const int sel = chart_nearest_for_kind (_kind, rc.Galaxy());

	Neuron::Msg::TravelRequest req;
	req.kind = Neuron::Msg::TravelKind::Hyperspace;
	req.systemId = rc.Galaxy()[sel].id;   // server resolves + validates the destination
	rc.Send (req);

	snd_play_sample (SND_HYPERSPACE);
	// No client-side transition: stay on the docked view until the server's
	// TravelResponse{Arrived} flips us into flight (the break pattern is retired).
}

int ChartData::DataLineCount (void)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !rc.HasGalaxy() || g_chart_selected < 0 || g_chart_selected >= (int) rc.Galaxy().size())
		return 0;
	return 5;
}

void ChartData::DataLine (int _i, char* _buf, int _buflen)
{
	if (_buflen <= 0) return;
	_buf[0] = '\0';
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !rc.HasGalaxy() || g_chart_selected < 0 || g_chart_selected >= (int) rc.Galaxy().size())
		return;
	const Neuron::Net::GalaxySystemInfo& s = rc.Galaxy()[g_chart_selected];
	switch (_i)
	{
		case 0: snprintf (_buf, _buflen, "Economy: %s", economy_type[s.economy & 7]); break;
		case 1: snprintf (_buf, _buflen, "Government: %s", government_type[s.government & 7]); break;
		case 2: snprintf (_buf, _buflen, "Tech Level: %d", s.techLevel + 1); break;
		case 3: snprintf (_buf, _buflen, "Population: %d.%d Billion", s.population / 10, s.population % 10); break;
		case 4: snprintf (_buf, _buflen, "Gross Productivity: %d M CR", s.productivity); break;
		default: break;
	}
}


struct rank
{
	int score;
	char *title;
};

#define NO_OF_RANKS	9

struct rank rating[NO_OF_RANKS] =
{
	{0x0000, "Harmless"},
	{0x0008, "Mostly Harmless"},
	{0x0010, "Poor"},
	{0x0020, "Average"},
	{0x0040, "Above Average"},
	{0x0080, "Competent"},
	{0x0200, "Dangerous"},
	{0x0A00, "Deadly"},
	{0x1900, "---- E L I T E ---"}
};

char *laser_name[5] = {"Pulse", "Beam", "Military", "Mining", "Custom"};



char *laser_type (int strength)
{
	switch (strength)
	{
		case PULSE_LASER:
			return laser_name[0];

		case BEAM_LASER:
			return laser_name[1];
		
		case MILITARY_LASER:
			return laser_name[2];
		
		case MINING_LASER:
			return laser_name[3];
	}	

	return laser_name[4];
}




static char *condition_txt[] =
{
	"Docked",
	"Green",
	"Yellow",
	"Red"
};




/***********************************************************************************/

#define TONNES		0
#define	KILOGRAMS	1
#define GRAMS		2

static char *unit_name[] = {"t", "kg", "g"};


// Render-free buy: trading is server-authoritative. Sends a buy request and
// lets the StationResponse update credits/cargo - no local mutation.
// Returns 1 if a request was sent (so a caller can refresh its display).
int market_buy (int item)
{
	if (!docked)
		return 0;

	Neuron::Net::StationRequest req;
	req.kind = Neuron::Net::StationRequestKind::Buy;
	req.commodity = (uint16_t) item;
	req.quantity = 1;
	Neuron::Client::ReplicationClientInstance().SendStationRequest(req);
	return 1;
}


// Render-free sell counterpart to market_buy.
int market_sell (int item)
{
	if ((!docked) || (cmdr.current_cargo[item] == 0))
		return 0;

	Neuron::Net::StationRequest req;
	req.kind = Neuron::Net::StationRequestKind::Sell;
	req.commodity = (uint16_t) item;
	req.quantity = 1;
	Neuron::Client::ReplicationClientInstance().SendStationRequest(req);
	return 1;
}


int market_item_count (void) { return 17; }

int market_credits (void) { return cmdr.credits; }

// Format one stock row as a fixed-width line (the GUI font is monospaced, so the
// columns line up): "<name> <unit> <price> <for-sale> <in-hold>".
void market_format_row (int item, char *buf, int buflen)
{
	char price[16], sale[16], hold[16];

	sprintf (price, "%d.%d", stock_market[item].current_price / 10,
							 stock_market[item].current_price % 10);

	if (stock_market[item].current_quantity > 0)
		sprintf (sale, "%d%s", stock_market[item].current_quantity,
							  unit_name[stock_market[item].units]);
	else
		strcpy (sale, "-");

	if (cmdr.current_cargo[item] > 0)
		sprintf (hold, "%d%s", cmdr.current_cargo[item],
							  unit_name[stock_market[item].units]);
	else
		strcpy (hold, "-");

	snprintf (buf, buflen, "%-15s %-2s %7s %6s %6s", stock_market[item].name,
			  unit_name[stock_market[item].units], price, sale, hold);
}





/* =================================================================================
 * Render-free accessors for the GUI info windows (Commander Status, Inventory,
 * Data on Planet). Each rebuilds a list of preformatted text lines the GUI's
 * InfoWindow renders, reusing the exact field logic above instead of the legacy
 * gfx_display_* drawing. Declared in docked.h.
 * ================================================================================= */

namespace {

std::vector<std::string> s_cmdrLines;
std::vector<std::string> s_invLines;
std::vector<std::string> s_planetLines;

void info_add (std::vector<std::string>& lines, const char* fmt, ...)
{
	char buf[160];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (buf, sizeof(buf), fmt, ap);
	va_end (ap);
	lines.push_back (buf);
}

// Word-wrap into <= width-char lines (the GUI font is monospaced, so chars == columns).
void info_add_wrapped (std::vector<std::string>& lines, const char* text, int width)
{
	std::string line;
	const char* p = text;
	while (*p)
	{
		const char* start = p;
		while (*p && *p != ' ') p++;
		std::string word (start, p);
		while (*p == ' ') p++;

		if (!line.empty() && (int)(line.size() + 1 + word.size()) > width)
		{
			lines.push_back (line);
			line.clear();
		}
		if (!line.empty()) line += ' ';
		line += word;
	}
	if (!line.empty())
		lines.push_back (line);
}

void copy_line (const std::vector<std::string>& lines, int i, char* buf, int buflen)
{
	if (buflen <= 0) return;
	if (i < 0 || i >= (int)lines.size()) { buf[0] = '\0'; return; }
	int n = (int)lines[i].size();
	if (n > buflen - 1) n = buflen - 1;
	memcpy (buf, lines[i].c_str(), n);
	buf[n] = '\0';
}

void build_cmdr_status (void)
{
	char planet_name[16];
	int i, condition, type;

	s_cmdrLines.clear();

	if (!witchspace)
	{
		current_system_name (planet_name);
		capitalise_name (planet_name);
		info_add (s_cmdrLines, "Present System:    %s", planet_name);
	}

	hyperspace_system_name (planet_name);
	capitalise_name (planet_name);
	info_add (s_cmdrLines, "Hyperspace System: %s", planet_name);

	if (docked)
		condition = 0;
	else
	{
		condition = 1;
		for (i = 0; i < MAX_LOCAL_OBJECTS; i++)
		{
			type = local_objects[i].type;
			if ((type == SHIP_MISSILE) || ((type > SHIP_ROCK) && (type < SHIP_DODEC)))
			{
				condition = 2;
				break;
			}
		}
		if ((condition == 2) && (PlayerDefense().energy < 128))
			condition = 3;
	}

	info_add (s_cmdrLines, "Condition:         %s", condition_txt[condition]);
	info_add (s_cmdrLines, "Fuel:              %d.%d Light Years", cmdr.fuel / 10, cmdr.fuel % 10);
	info_add (s_cmdrLines, "Cash:              %d.%d Cr", cmdr.credits / 10, cmdr.credits % 10);

	const char* legal = (cmdr.legal_status == 0) ? "Clean"
					   : (cmdr.legal_status > 50 ? "Fugitive" : "Offender");
	info_add (s_cmdrLines, "Legal Status:      %s", legal);

	const char* title = rating[0].title;
	for (i = 0; i < NO_OF_RANKS; i++)
		if (cmdr.score >= rating[i].score)
			title = rating[i].title;
	info_add (s_cmdrLines, "Rating:            %s", title);

	info_add (s_cmdrLines, "%s", "");
	info_add (s_cmdrLines, "%s", "EQUIPMENT:");

	if (cmdr.cargo_capacity > 20)  info_add (s_cmdrLines, "  Large Cargo Bay");
	if (cmdr.escape_pod)           info_add (s_cmdrLines, "  Escape Pod");
	if (cmdr.fuel_scoop)           info_add (s_cmdrLines, "  Fuel Scoops");
	if (cmdr.ecm)                  info_add (s_cmdrLines, "  E.C.M. System");
	if (cmdr.energy_bomb)          info_add (s_cmdrLines, "  Energy Bomb");
	if (cmdr.energy_unit)          info_add (s_cmdrLines, "  %s", cmdr.energy_unit == 1 ? "Extra Energy Unit" : "Naval Energy Unit");
	if (cmdr.docking_computer)     info_add (s_cmdrLines, "  Docking Computers");
	if (cmdr.galactic_hyperdrive)  info_add (s_cmdrLines, "  Galactic Hyperspace");
	if (cmdr.front_laser)          info_add (s_cmdrLines, "  %s Laser", laser_type (cmdr.front_laser));
}

void build_inventory (void)
{
	int i;
	bool any = false;

	s_invLines.clear();
	info_add (s_invLines, "Fuel:  %d.%d Light Years", cmdr.fuel / 10, cmdr.fuel % 10);
	info_add (s_invLines, "Cash:  %d.%d Cr", cmdr.credits / 10, cmdr.credits % 10);
	info_add (s_invLines, "%s", "");

	for (i = 0; i < 17; i++)
	{
		if (cmdr.current_cargo[i] > 0)
		{
			info_add (s_invLines, "%-20s %d%s", stock_market[i].name,
					  cmdr.current_cargo[i], unit_name[stock_market[i].units]);
			any = true;
		}
	}

	if (!any)
		info_add (s_invLines, "%s", "Hold empty.");
}

void build_planet_data (void)
{
	char str[100];
	struct planet_data pd;
	int ly;

	s_planetLines.clear();

	generate_planet_data (&pd, hyperspace_planet);

	ly = calc_distance_to_planet (docked_planet, hyperspace_planet);
	if (ly > 0)
		info_add (s_planetLines, "Distance: %d.%d Light Years", ly / 10, ly % 10);

	info_add (s_planetLines, "Economy: %s", economy_type[pd.economy]);
	info_add (s_planetLines, "Government: %s", government_type[pd.government]);
	info_add (s_planetLines, "Tech Level: %d", pd.techlevel + 1);
	info_add (s_planetLines, "Population: %d.%d Billion", pd.population / 10, pd.population % 10);

	describe_inhabitants (str, hyperspace_planet);
	info_add (s_planetLines, "%s", str);

	info_add (s_planetLines, "Gross Productivity: %d M CR", pd.productivity);
	info_add (s_planetLines, "Average Radius: %d km", pd.radius);
	info_add (s_planetLines, "%s", "");
	info_add_wrapped (s_planetLines, describe_planet (hyperspace_planet), 52);
}

} // namespace

int cmdr_status_line_count (void) { build_cmdr_status(); return (int) s_cmdrLines.size(); }
void cmdr_status_line (int i, char *buf, int buflen) { copy_line (s_cmdrLines, i, buf, buflen); }
void cmdr_status_title (char *buf, int buflen) { snprintf (buf, buflen, "Commander %s", cmdr.name); }

int inventory_line_count (void) { build_inventory(); return (int) s_invLines.size(); }
void inventory_line (int i, char *buf, int buflen) { copy_line (s_invLines, i, buf, buflen); }

int planet_data_line_count (void) { build_planet_data(); return (int) s_planetLines.size(); }
void planet_data_line (int i, char *buf, int buflen) { copy_line (s_planetLines, i, buf, buflen); }
void planet_data_title (char *buf, int buflen)
{
	char planet_name[16];
	name_planet (planet_name, hyperspace_planet);
	snprintf (buf, buflen, "Data on %s", planet_name);
}

/***********************************************************************************/

// The lasers are single items now: the ship has one (front) mount, so the
// legacy per-mount sub-menus (front/rear/left/right) are gone.
enum equip_types
{
	EQ_FUEL, EQ_MISSILE, EQ_CARGO_BAY, EQ_ECM, EQ_FUEL_SCOOPS,
	EQ_ESCAPE_POD, EQ_ENERGY_BOMB, EQ_ENERGY_UNIT, EQ_DOCK_COMP,
	EQ_GAL_DRIVE, EQ_PULSE_LASER, EQ_BEAM_LASER, EQ_MINING_LASER,
	EQ_MILITARY_LASER
};



#define NO_OF_EQUIP_ITEMS	14

struct equip_item
{
	int canbuy;
	int y;
	int show;
	int level;
	int price;
	char *name;
	int type;
};

struct equip_item equip_stock[NO_OF_EQUIP_ITEMS] =
{
	{0, 0, 1, 1,     2, " Fuel",					EQ_FUEL},
	{0, 0, 1, 1,   300, " Missile",					EQ_MISSILE},
	{0, 0, 1, 1,  4000, " Large Cargo Bay",			EQ_CARGO_BAY},
	{0, 0, 1, 2,  6000, " E.C.M. System",			EQ_ECM},
	{0, 0, 1, 5,  5250, " Fuel Scoops",				EQ_FUEL_SCOOPS},
	{0, 0, 1, 6, 10000, " Escape Pod",				EQ_ESCAPE_POD},
	{0, 0, 1, 7,  9000, " Energy Bomb",				EQ_ENERGY_BOMB},
	{0, 0, 1, 8, 15000, " Extra Energy Unit",		EQ_ENERGY_UNIT},
	{0, 0, 1, 9, 15000, " Docking Computers",		EQ_DOCK_COMP},
	{0, 0, 1,10, 50000, " Galactic Hyperdrive",		EQ_GAL_DRIVE},
	{0, 0, 1, 3,  4000, " Pulse Laser",				EQ_PULSE_LASER},
	{0, 0, 1, 4, 10000, " Beam Laser",				EQ_BEAM_LASER},
	{0, 0, 1,10,  8000, " Mining Laser",			EQ_MINING_LASER},
	{0, 0, 1,10, 60000, " Military Laser",			EQ_MILITARY_LASER}
};


int equip_present (int type)
{
	switch (type)
	{
		case EQ_FUEL:
			return (cmdr.fuel >= 70);
		
		case EQ_MISSILE:
			return (cmdr.missiles >= 4);
		
		case EQ_CARGO_BAY:
			return (cmdr.cargo_capacity > 20);
		
		case EQ_ECM:
			return cmdr.ecm;
		
		case EQ_FUEL_SCOOPS:
			return cmdr.fuel_scoop;
		
		case EQ_ESCAPE_POD:
			return cmdr.escape_pod;
		
		case EQ_ENERGY_BOMB:
			return cmdr.energy_bomb;

		case EQ_ENERGY_UNIT:
			return cmdr.energy_unit;
			
		case EQ_DOCK_COMP:
			return cmdr.docking_computer;
			
		case EQ_GAL_DRIVE:
			return cmdr.galactic_hyperdrive;

		case EQ_PULSE_LASER:
			return (cmdr.front_laser == PULSE_LASER);

		case EQ_BEAM_LASER:
			return (cmdr.front_laser == BEAM_LASER);

		case EQ_MINING_LASER:
			return (cmdr.front_laser == MINING_LASER);

		case EQ_MILITARY_LASER:
			return (cmdr.front_laser == MILITARY_LASER);
	}

	return 0;
}




// Map a legacy equip-screen item to the server's authoritative equipment catalog.
// Returns true and fills `_item` for a server-modeled purchase; returns false for
// items the server does not model (lasers, extra energy unit, docking computer,
// galactic hyperdrive) - those are not purchasable. Fuel is a Refuel request
// handled by the caller, not an EquipItem.
static bool server_equip_item (int _type, Neuron::Net::EquipItem& _item)
{
	switch (_type)
	{
		case EQ_MISSILE:      _item = Neuron::Net::EquipItem::Missile;       return true;
		case EQ_CARGO_BAY:    _item = Neuron::Net::EquipItem::LargeCargoBay; return true;
		case EQ_ECM:          _item = Neuron::Net::EquipItem::Ecm;           return true;
		case EQ_FUEL_SCOOPS:  _item = Neuron::Net::EquipItem::FuelScoop;     return true;
		case EQ_ENERGY_BOMB:  _item = Neuron::Net::EquipItem::EnergyBomb;    return true;
		case EQ_ESCAPE_POD:   _item = Neuron::Net::EquipItem::EscapePod;     return true;
		default:              return false;
	}
}


// Render-free equip action for a given stock index: buy the item. Equipment and
// fuel are server-authoritative: the purchase is a station request and cmdr
// state changes only when the StationResponse/PlayerStatus reply arrives.
// Returns 1 if it acted.
int equip_do (int index)
{
	Neuron::Net::StationRequest req;
	if (equip_stock[index].type == EQ_FUEL)
	{
		req.kind = Neuron::Net::StationRequestKind::Refuel;
	}
	else
	{
		Neuron::Net::EquipItem item;
		if (!server_equip_item(equip_stock[index].type, item))
			return 0;
		req.kind = Neuron::Net::StationRequestKind::Equip;
		req.commodity = (uint16_t) item;
	}
	Neuron::Client::ReplicationClientInstance().SendStationRequest(req);
	return 1;
}



/* ---- Render-free equip accessors for the GUI equip window. The visible set =
 * items with show && tech-level >= level (the same filter list_equip_prices uses). */
static int s_equipVisible[NO_OF_EQUIP_ITEMS];
static int s_equipVisibleCount = 0;

void equip_reset (void) {}   // the list is flat now (no laser sub-menus to collapse)

int equip_visible_count (void)
{
	int i, tech_level;

	tech_level = current_planet_data.techlevel + 1;
	equip_stock[0].price = (70 - cmdr.fuel) * 2;   // fuel price tracks the tank

	s_equipVisibleCount = 0;
	for (i = 0; i < NO_OF_EQUIP_ITEMS; i++)
	{
		equip_stock[i].canbuy = ((equip_present (equip_stock[i].type) == 0) &&
								 (equip_stock[i].price <= cmdr.credits));
		if (equip_stock[i].show && (tech_level >= equip_stock[i].level))
			s_equipVisible[s_equipVisibleCount++] = i;
	}

	return s_equipVisibleCount;
}

int equip_visible_index (int i) { return (i >= 0 && i < s_equipVisibleCount) ? s_equipVisible[i] : 0; }

void equip_row_text (int index, char *buf, int buflen)
{
	const char *name = &equip_stock[index].name[1];   // strip the ' ' prefix
	if (equip_stock[index].price != 0)
		snprintf (buf, buflen, "%-22s %d.%d", name, equip_stock[index].price / 10, equip_stock[index].price % 10);
	else
		snprintf (buf, buflen, "%s", name);
}

int equip_buyable (int index) { return equip_stock[index].canbuy; }
