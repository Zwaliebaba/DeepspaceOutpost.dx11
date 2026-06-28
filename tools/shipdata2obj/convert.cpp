/*
 * shipdata2obj
 *
 * One-shot converter that exports the built-in Elite ship geometry
 * (DeepspaceOutpost/shipdata.cpp + shipface.cpp) to Wavefront OBJ files,
 * a shared material library, and per-ship JSON sidecars holding the
 * non-geometry gameplay stats.
 *
 * It links directly against the real ship_data / ship_solids arrays so the
 * exported geometry is guaranteed to match what the game compiles. The game
 * itself is unchanged: this only emits files into GameData/Models.
 *
 * Build (from repo root):
 *   g++ -std=c++17 -I tools/shipdata2obj/stubs -I DeepspaceOutpost \
 *       tools/shipdata2obj/convert.cpp \
 *       DeepspaceOutpost/shipdata.cpp DeepspaceOutpost/shipface.cpp \
 *       -o tools/shipdata2obj/convert
 *
 * Run (from repo root):
 *   tools/shipdata2obj/convert GameData
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <set>

#include "shipdata.h"
#include "shipface.h"

/* Ship table in SHIP_* id order, mirroring ship_list[] in elite.cpp. */
static const ship_data *const ship_list[] = {
	nullptr,
	&missile_data,  &coriolis_data, &esccaps_data,  &alloy_data,    &cargo_data,
	&boulder_data,  &asteroid_data, &rock_data,     &orbit_data,    &transp_data,
	&cobra3a_data,  &pythona_data,  &boa_data,      &anacnda_data,  &hermit_data,
	&viper_data,    &sidewnd_data,  &mamba_data,    &krait_data,    &adder_data,
	&gecko_data,    &cobra1_data,   &worm_data,     &cobra3b_data,  &asp2_data,
	&pythonb_data,  &ferdlce_data,  &moray_data,    &thargoid_data, &thargon_data,
	&constrct_data, &cougar_data,   &dodec_data
};

/*
 * Stable file-base names. Display names collide ("Cobra MkIII", "Python"
 * each appear twice), so these follow the source variable names instead.
 */
static const char *const base_name[] = {
	"",
	"missile",       "coriolis",      "escape_capsule", "alloy",         "cargo",
	"boulder",       "asteroid",      "rock",           "shuttle",       "transporter",
	"cobra_mk3",     "python",        "boa",            "anaconda",      "rock_hermit",
	"viper",         "sidewinder",    "mamba",          "krait",         "adder",
	"gecko",         "cobra_mk1",     "worm",           "cobra_mk3_lone","asp_mk2",
	"python_lone",   "fer_de_lance",  "moray",          "thargoid",      "thargon",
	"constrictor",   "cougar",        "dodec"
};

static const int NUM_SHIPS = (int)(sizeof(ship_list) / sizeof(ship_list[0])) - 1;

static uint8_t palette[256][3];   /* R, G, B */

/* Read the 256-entry palette out of an 8bpp BMP (scanner.bmp). */
static bool load_palette(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> b(n);
	if (fread(b.data(), 1, n, f) != (size_t)n) { fclose(f); return false; }
	fclose(f);

	if (n < 54 || b[0] != 'B' || b[1] != 'M')
		return false;

	uint32_t dib = *(uint32_t *)&b[14];
	long pal_off = 14 + dib;               /* palette follows the DIB header */
	if (pal_off + 256 * 4 > n)
		return false;

	for (int i = 0; i < 256; i++) {
		/* BMP palette entries are stored B, G, R, reserved. */
		palette[i][2] = b[pal_off + i * 4 + 0];
		palette[i][1] = b[pal_off + i * 4 + 1];
		palette[i][0] = b[pal_off + i * 4 + 2];
	}
	return true;
}

/* The up-to-8 face vertex indices live in named fields; index them in order. */
static int face_vertex(const ship_face &fc, int n)
{
	switch (n) {
	case 0: return fc.p1;
	case 1: return fc.p2;
	case 2: return fc.p3;
	case 3: return fc.p4;
	case 4: return fc.p5;
	case 5: return fc.p6;
	case 6: return fc.p7;
	default: return fc.p8;
	}
}

int main(int argc, char **argv)
{
	std::string gamedata = (argc > 1) ? argv[1] : "GameData";
	std::string models = gamedata + "/Models";
	std::string mkdir_cmd = "mkdir -p \"" + models + "\"";
	if (system(mkdir_cmd.c_str()) != 0)
		fprintf(stderr, "warning: could not create %s\n", models.c_str());

	if (!load_palette(gamedata + "/scanner.bmp")) {
		fprintf(stderr, "error: cannot read palette from %s/scanner.bmp\n",
		        gamedata.c_str());
		return 1;
	}

	std::set<int> used_colours;

	for (int id = 1; id <= NUM_SHIPS; id++) {
		const ship_data *sd = ship_list[id];
		const ship_solid *sol = &ship_solids[id];
		const char *base = base_name[id];

		/* ---- OBJ: vertices + coloured polygon faces ---- */
		std::string obj_path = models + "/" + base + ".obj";
		FILE *o = fopen(obj_path.c_str(), "w");
		if (!o) { fprintf(stderr, "error: cannot write %s\n", obj_path.c_str()); return 1; }

		fprintf(o, "# %s\n", sd->name);
		fprintf(o, "# Exported from DeepspaceOutpost ship data (SHIP id %d).\n", id);
		fprintf(o, "# Gameplay stats and the vertex/line indices the engine\n");
		fprintf(o, "# derives are in the matching %s.json sidecar.\n", base);
		fprintf(o, "mtllib elite.mtl\n");
		fprintf(o, "o %s\n", base);

		for (int i = 0; i < sd->num_points; i++)
			fprintf(o, "v %d %d %d\n",
			        sd->points[i].x, sd->points[i].y, sd->points[i].z);

		for (int i = 0; i < sol->num_faces; i++) {
			const ship_face &fc = sol->face_data[i];
			used_colours.insert(fc.colour);
			fprintf(o, "usemtl col_%d\n", fc.colour);
			fprintf(o, "f");
			for (int v = 0; v < fc.points; v++)
				fprintf(o, " %d", face_vertex(fc, v) + 1);  /* OBJ is 1-based */
			fprintf(o, "\n");
		}
		fclose(o);

		/* ---- JSON sidecar: everything that is not geometry ---- */
		std::string json_path = models + "/" + base + ".json";
		FILE *j = fopen(json_path.c_str(), "w");
		if (!j) { fprintf(stderr, "error: cannot write %s\n", json_path.c_str()); return 1; }

		fprintf(j, "{\n");
		fprintf(j, "  \"name\": \"%s\",\n", sd->name);
		fprintf(j, "  \"ship_id\": %d,\n", id);
		fprintf(j, "  \"model\": \"%s.obj\",\n", base);
		fprintf(j, "  \"num_points\": %d,\n", sd->num_points);
		fprintf(j, "  \"num_lines\": %d,\n", sd->num_lines);
		fprintf(j, "  \"num_faces\": %d,\n", sd->num_faces);
		fprintf(j, "  \"max_loot\": %d,\n", sd->max_loot);
		fprintf(j, "  \"scoop_type\": %d,\n", sd->scoop_type);
		fprintf(j, "  \"size\": %g,\n", sd->size);
		fprintf(j, "  \"front_laser\": %d,\n", sd->front_laser);
		fprintf(j, "  \"bounty\": %d,\n", sd->bounty);
		fprintf(j, "  \"vanish_point\": %d,\n", sd->vanish_point);
		fprintf(j, "  \"energy\": %d,\n", sd->energy);
		fprintf(j, "  \"velocity\": %d,\n", sd->velocity);
		fprintf(j, "  \"missiles\": %d,\n", sd->missiles);
		fprintf(j, "  \"laser_strength\": %d\n", sd->laser_strength);
		fprintf(j, "}\n");
		fclose(j);

		printf("%-16s %2d verts %2d faces -> %s.obj / %s.json\n",
		       sd->name, sd->num_points, sol->num_faces, base, base);
	}

	/* ---- shared material library from the palette ---- */
	std::string mtl_path = models + "/elite.mtl";
	FILE *m = fopen(mtl_path.c_str(), "w");
	if (!m) { fprintf(stderr, "error: cannot write %s\n", mtl_path.c_str()); return 1; }
	fprintf(m, "# Materials for DeepspaceOutpost ship models.\n");
	fprintf(m, "# Colours are the scanner.bmp palette entries used by the ships.\n");
	for (int c : used_colours) {
		double r = palette[c][0] / 255.0;
		double g = palette[c][1] / 255.0;
		double b = palette[c][2] / 255.0;
		fprintf(m, "\nnewmtl col_%d\n", c);
		fprintf(m, "Kd %.4f %.4f %.4f\n", r, g, b);
		fprintf(m, "Ka 0.0000 0.0000 0.0000\n");
		fprintf(m, "d 1.0\n");
		fprintf(m, "illum 1\n");
	}
	fclose(m);

	printf("\n%d ships exported to %s (%zu materials in elite.mtl)\n",
	       NUM_SHIPS, models.c_str(), used_colours.size());
	return 0;
}
