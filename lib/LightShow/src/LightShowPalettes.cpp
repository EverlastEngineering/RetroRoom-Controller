// The cpt-city gradient palettes and the order they play in.
//
// Converted by script from FastLED's own example sketch,
// `colorwaveswithpalettes.ino` (Mark Kriegsman, August 2015), which
// defines them as:
//
//     DEFINE_GRADIENT_PALETTE( es_vintage_01_gp ) {
//         0,   4,  1,  1,
//         51, 16,  0,  1,
//         ... };
//
// Two things about that form are worth writing down, because both are
// silent if you get them wrong:
//
//  - Each stop is FOUR bytes, not three. The per-palette "Size: N
// bytes" comments in the sketch confirm it: a four-line palette says
// 16 bytes and a nine-line one says 36. The fourth byte is not a
// colour channel and is dropped here.
//
//  - A palette is as long as the macro was written. These run from
//  two stops to thirteen. `GradientPalette::length` therefore
// carries the real count and the unused entries are left zeroed --
// which is NOT the same as black, and colourFromPalette() below
// never looks past `length`.
//
// The table is alphabetical (so the order is obvious) and the
// playlist at the bottom is the order they PLAY, which is the only
// thing to edit to change the show. The original had the same
// separation and the same two commented-out entries, because that is
// all the editability a colour list otherwise has.
//
// Generated rather than typed. A hand-copied table of thirty-three
// palettes is thirty-three chances to mistype a number, and a wrong
// channel in a colour is not something a test would catch.

#include "LightShow.h"

namespace retroroom_core {

const GradientPalette kPalettes[] = {
	// Analogous_1_gp:                  5 stops
	{
		5,
		{
			{  0,   3,   0},
			{ 63,  23,   0},
			{127,  67,   0},
			{191, 142,   0},
			{255, 255,   0},
		},
	},
	// BlacK_Blue_Magenta_White_gp:     7 stops
	{
		7,
		{
			{  0,   0,   0},
			{ 42,   0,   0},
			{ 84,   0,   0},
			{127,  42,   0},
			{170, 255,   0},
			{212, 255,  55},
			{255, 255, 255},
		},
	},
	// BlacK_Magenta_Red_gp:            5 stops
	{
		5,
		{
			{  0,   0,   0},
			{ 63,  42,   0},
			{127, 255,   0},
			{191, 255,   0},
			{255, 255,   0},
		},
	},
	// BlacK_Red_Magenta_Yellow_gp:     7 stops
	{
		7,
		{
			{  0,   0,   0},
			{ 42,  42,   0},
			{ 84, 255,   0},
			{127, 255,   0},
			{170, 255,   0},
			{212, 255,  55},
			{255, 255, 255},
		},
	},
	// Blue_Cyan_Yellow_gp:             5 stops
	{
		5,
		{
			{  0,   0,   0},
			{ 63,   0,  55},
			{127,   0, 255},
			{191,  42, 255},
			{255, 255, 255},
		},
	},
	// Colorfull_gp:                    11 stops
	{
		11,
		{
			{  0,  10,  85},
			{ 25,  29, 109},
			{ 60,  59, 138},
			{ 93,  83,  99},
			{106, 110,  66},
			{109, 123,  49},
			{113, 139,  35},
			{116, 192, 117},
			{124, 255, 255},
			{168, 100, 180},
			{255,  22, 121},
		},
	},
	// Coral_reef_gp:                   6 stops
	{
		6,
		{
			{  0,  40, 199},
			{ 50,  10, 152},
			{ 96,   1, 111},
			{ 96,  43, 127},
			{139,  10,  73},
			{255,   1,  34},
		},
	},
	// Fuschia_7_gp:                    5 stops
	{
		5,
		{
			{  0,  43,   3},
			{ 63, 100,   4},
			{127, 188,   5},
			{191, 161,  11},
			{255, 135,  20},
		},
	},
	// GMT_drywet_gp:                   7 stops
	{
		7,
		{
			{  0,  47,  30},
			{ 42, 213, 147},
			{ 84, 103, 219},
			{127,   3, 219},
			{170,   1,  48},
			{212,   1,   1},
			{255,   1,   7},
		},
	},
	// Magenta_Evening_gp:              7 stops
	{
		7,
		{
			{  0,  71,  27},
			{ 31, 130,  11},
			{ 63, 213,   2},
			{ 70, 232,   1},
			{ 76, 252,   1},
			{108, 123,   2},
			{255,  46,   9},
		},
	},
	// Pink_Purple_gp:                  11 stops
	{
		11,
		{
			{  0,  19,   2},
			{ 25,  26,   4},
			{ 51,  33,   6},
			{ 76,  68,  62},
			{102, 118, 187},
			{109, 163, 215},
			{114, 217, 244},
			{122, 159, 149},
			{149, 113,  78},
			{183, 128,  57},
			{255, 146,  40},
		},
	},
	// Sunset_Real_gp:                  7 stops
	{
		7,
		{
			{  0, 120,   0},
			{ 22, 179,  22},
			{ 51, 255, 104},
			{ 85, 167,  22},
			{135, 100,   0},
			{198,  16,   0},
			{255,   0,   0},
		},
	},
	// departure_gp:                    12 stops
	{
		12,
		{
			{  0,   8,   3},
			{ 42,  23,   7},
			{ 63,  75,  38},
			{ 84, 169,  99},
			{106, 213, 169},
			{116, 255, 255},
			{138, 135, 255},
			{148,  22, 255},
			{170,   0, 255},
			{191,   0, 136},
			{212,   0,  55},
			{255,   0,  55},
		},
	},
	// es_autumn_19_gp:                 13 stops
	{
		13,
		{
			{  0,  26,   1},
			{ 51,  67,   4},
			{ 84, 118,  14},
			{104, 137, 152},
			{112, 113,  65},
			{122, 133, 149},
			{124, 137, 152},
			{135, 113,  65},
			{142, 139, 154},
			{163, 113,  13},
			{204,  55,   3},
			{249,  17,   1},
			{255,  17,   1},
		},
	},
	// es_emerald_dragon_08_gp:         4 stops
	{
		4,
		{
			{  0,  97, 255},
			{101,  47, 133},
			{178,  13,  43},
			{255,   2,  10},
		},
	},
	// es_landscape_33_gp:              6 stops
	{
		6,
		{
			{  0,   1,   5},
			{ 19,  32,  23},
			{ 38, 161,  55},
			{ 63, 229, 144},
			{ 66,  39, 142},
			{255,   1,   4},
		},
	},
	// es_landscape_64_gp:              9 stops
	{
		9,
		{
			{  0,   0,   0},
			{ 37,   2,  25},
			{ 76,  15, 115},
			{127,  79, 213},
			{128, 126, 211},
			{130, 188, 209},
			{153, 144, 182},
			{204,  59, 117},
			{255,   1,  37},
		},
	},
	// es_ocean_breeze_036_gp:          4 stops
	{
		4,
		{
			{  0,   1,   6},
			{ 89,   1,  99},
			{153, 144, 209},
			{255,   0,  73},
		},
	},
	// es_ocean_breeze_068_gp:          6 stops
	{
		6,
		{
			{  0, 100, 156},
			{ 51,   1,  99},
			{101,   1,  68},
			{104,  35, 142},
			{178,   0,  63},
			{255,   1,  10},
		},
	},
	// es_pinksplash_07_gp:             7 stops
	{
		7,
		{
			{  0, 229,   1},
			{ 61, 242,   4},
			{101, 255,  12},
			{127, 249,  81},
			{153, 255,  11},
			{193, 244,   5},
			{255, 232,   1},
		},
	},
	// es_pinksplash_08_gp:             5 stops
	{
		5,
		{
			{  0, 126,  11},
			{127, 197,   1},
			{175, 210, 157},
			{221, 157,   3},
			{255, 157,   3},
		},
	},
	// es_rivendell_15_gp:              5 stops
	{
		5,
		{
			{  0,   1,  14},
			{101,  16,  36},
			{165,  56,  68},
			{242, 150, 156},
			{255, 150, 156},
		},
	},
	// es_vintage_01_gp:                8 stops
	{
		8,
		{
			{  0,   4,   1},
			{ 51,  16,   0},
			{ 76,  97, 104},
			{101, 255, 131},
			{127,  67,   9},
			{153,  16,   0},
			{229,   4,   1},
			{255,   4,   1},
		},
	},
	// es_vintage_57_gp:                5 stops
	{
		5,
		{
			{  0,   2,   1},
			{ 53,  18,   1},
			{104,  69,  29},
			{153, 167, 135},
			{255,  46,  56},
		},
	},
	// fire_gp:                         7 stops
	{
		7,
		{
			{  0,   1,   1},
			{ 76,  32,   5},
			{146, 192,  24},
			{197, 220, 105},
			{240, 252, 255},
			{250, 252, 255},
			{255, 255, 255},
		},
	},
	// gr64_hult_gp:                    8 stops
	{
		8,
		{
			{  0,   1, 124},
			{ 66,   1,  93},
			{104,  52,  65},
			{130, 115, 127},
			{150,  52,  65},
			{201,   1,  86},
			{239,   0,  55},
			{255,   0,  55},
		},
	},
	// gr65_hult_gp:                    6 stops
	{
		6,
		{
			{  0, 247, 176},
			{ 48, 255, 136},
			{ 89, 220,  29},
			{160,   7,  82},
			{216,   1, 124},
			{255,   1, 124},
		},
	},
	// ib15_gp:                         6 stops
	{
		6,
		{
			{  0, 113,  91},
			{ 72, 157,  88},
			{ 89, 208,  85},
			{107, 255,  29},
			{141, 137,  31},
			{255,  59,  33},
		},
	},
	// ib_jul01_gp:                     4 stops
	{
		4,
		{
			{  0, 194,   1},
			{ 94,   1,  29},
			{132,  57, 131},
			{255, 113,   1},
		},
	},
	// lava_gp:                         13 stops
	{
		13,
		{
			{  0,   0,   0},
			{ 46,  18,   0},
			{ 96, 113,   0},
			{108, 142,   3},
			{119, 175,  17},
			{146, 213,  44},
			{174, 255,  82},
			{188, 255, 115},
			{202, 255, 156},
			{218, 255, 203},
			{234, 255, 255},
			{244, 255, 255},
			{255, 255, 255},
		},
	},
	// rainbowsherbet_gp:               7 stops
	{
		7,
		{
			{  0, 255,  33},
			{ 43, 255,  68},
			{ 86, 255,   7},
			{127, 255,  82},
			{170, 255, 255},
			{209,  42, 255},
			{255,  87, 255},
		},
	},
	// retro2_16_gp:                    2 stops
	{
		2,
		{
			{  0, 188, 135},
			{255,  46,   7},
		},
	},
	// rgi_15_gp:                       9 stops
	{
		9,
		{
			{  0,   4,   1},
			{ 31,  55,   1},
			{ 63, 197,   3},
			{ 95,  59,   2},
			{127,   6,   2},
			{159,  39,   6},
			{191, 112,  13},
			{223,  56,   9},
			{255,  22,   6},
		},
	},
};

const int kPaletteCount =
	static_cast<int>(sizeof(kPalettes) / sizeof(kPalettes[0]));

const uint8_t kPlaylist[] = {
	 11,  21,  17,  32,  31,   0,  20,   6,
	 18,  19,  22,  12,  16,  15,  30,  26,
	 25,   8,  28,  23,  27,   7,  14,   5,
	  9,  10,  13,   1,   2,   3,   4,
};

const int kPlaylistCount =
	static_cast<int>(sizeof(kPlaylist) / sizeof(kPlaylist[0]));

// The names, parallel to kPlaylist. For the log and the web API,
// so a cabinet on "es_rivendell_15_gp" can be identified without a
// screenshot. The sketch had no names at all -- it was a demo -- so
// this is the one thing here the original does not have.
const char* const kPaletteNames[] = {
	"Sunset_Real_gp", "es_rivendell_15_gp", "es_ocean_breeze_036_gp", "rgi_15_gp",
	"retro2_16_gp", "Analogous_1_gp", "es_pinksplash_08_gp", "Coral_reef_gp",
	"es_ocean_breeze_068_gp", "es_pinksplash_07_gp", "es_vintage_01_gp", "departure_gp",
	"es_landscape_64_gp", "es_landscape_33_gp", "rainbowsherbet_gp", "gr65_hult_gp",
	"gr64_hult_gp", "GMT_drywet_gp", "ib_jul01_gp", "es_vintage_57_gp",
	"ib15_gp", "Fuschia_7_gp", "es_emerald_dragon_08_gp", "Colorfull_gp",
	"Magenta_Evening_gp", "Pink_Purple_gp", "es_autumn_19_gp", "BlacK_Blue_Magenta_White_gp",
	"BlacK_Magenta_Red_gp", "BlacK_Red_Magenta_Yellow_gp", "Blue_Cyan_Yellow_gp",
};

}  // namespace retroroom_core
