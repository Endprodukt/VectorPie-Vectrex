/* colors.h - colour profiles: parts of a game coloured by what draws them (not a static overlay).
 *
 * A profile is one file per game, package/colors/<id>.ini (the owner's save/colors/<id>.ini instead,
 * if there is one). Each line a rule, the first that matches a line of the picture gives its colour:
 *
 *   name = CONDITIONS : RRGGBB [*GAIN]
 *
 * GAIN (e.g. *1.6) makes the rule's lines brighter (or, below 1, dimmer) than the Vectrex draws them.
 *
 * CONDITIONS, all of which must hold (space separated):
 *   x AAAA-BBBB      the line's vector list pointer (the 6809's X when it was drawn, vx_tag) in the range
 *   pc AAAA-BBBB     the drawing routine's address in the range
 *   u AAAA-BBBB      the U register in the range
 *   dot              the line is a dot (at most VX_DOT_LEN long: a BIOS dot drifts a little)
 *   line             the line is not a dot
 *   ram AAAA=VV      a RAM byte ($C800-$CBFF) is VV this frame; AAAA&MM=VV with a mask; != for "is not"
 *   bright LO-HI     the Vectrex brightness (0..127) in the range
 *   caller AAAA-BBBB the game's code that called the drawing (vx_tag caller) in the range
 *   stroke LO-HI     the line's vector in its piece (vx_line stroke, from 0; decimal): one stroke of a shape
 *   length LO-HI     the line's length in canvas units (decimal): long walls and short dots one code draws
 *   area X0-X1 Y0-Y1 the whole line inside the box (canvas units, decimal, y down): fixed scenery in one
 *                    place (Clean Sweep's vaults), drawn by the same code as the rest
 *   part LO-HI       the line's part of its piece (vx_line part, from 0; decimal): the k-th vector of a list a
 *                    game works out in RAM each frame (Pole Position's road edges)
 *                    a game draws without a vector list
 *
 * The Vectrex's brightness still dims the colour (a dim line of a red ship stays a dim red). Lines no
 * rule matches keep the picture's own colour (vx_config color_*). A UVMC2 / PiTrex game has no tags
 * (the 6809 does not draw): only dot / line / bright conditions work there. */
#ifndef VX_COLORS_H
#define VX_COLORS_H

#include "core/vx_core.h"

#define VX_COLOR_RULES 512
#define VX_DOT_LEN     250      /* canvas units: a line no longer than this is a dot */
#define VX_RULE_CONDS  6

enum { VX_C_X, VX_C_PC, VX_C_U, VX_C_DOT, VX_C_LINE, VX_C_RAM, VX_C_BRIGHT, VX_C_CALLER, VX_C_STROKE, VX_C_PART, VX_C_LENGTH, VX_C_AREA };

typedef struct vx_cond {
    int      kind;
    uint16_t lo, hi;            /* a range (x, pc, u, bright; area: x) */
    uint16_t lo2, hi2;          /* area: y */
    uint16_t addr;              /* ram */
    uint8_t  mask, val;
    int      neg;               /* ram: != */
} vx_cond;

typedef struct vx_rule {
    char     name[32];
    vx_cond  cond[VX_RULE_CONDS];
    int      ncond;
    uint8_t  r, g, b;
    float    gain;              /* the line's level times this (": RRGGBB *1.6"; 1 without) */
    int      ram_ok;            /* this frame: its RAM conditions hold */
} vx_rule;

#define VX_COLOR_CRCS 8
typedef struct vx_colors {
    vx_rule  rule[VX_COLOR_RULES];
    int      n;
    char     file[1024];        /* where it came from ("" none) */
    uint32_t crc[VX_COLOR_CRCS];/* the cartridges it is made for ("crc = 1A2B3C4D ..."; none: any of the name) */
    int      ncrc;
} vx_colors;

/* The game's profile: save/colors/<id>.ini, else package/colors/<id>.ini. 0, or -1 (none: n = 0). */
int  vx_colors_load(vx_colors *c, const char *game_id);
/* From one file (the tests). Lines it cannot read are reported on stderr and left out. */
int  vx_colors_load_file(vx_colors *c, const char *path);
/* Once a frame, before the lines: the RAM conditions. */
void vx_colors_frame(vx_colors *c);
/* The line's colour: 1 and r, g, b when a rule matches. */
int  vx_colors_line(const vx_colors *c, const vx_line *l, uint8_t *r, uint8_t *g, uint8_t *b);
/* The rule that matches the line (its colour and gain), or NULL. */
const vx_rule *vx_colors_find(const vx_colors *c, const vx_line *l);
/* Whether one rule's line conditions hold for the line (its RAM conditions aside). */
int  vx_rule_matches(const vx_rule *r, const vx_line *l);

/* Changing a profile (the colour editor): put a rule first (one with the same conditions is replaced,
 * moved to the top); remove every rule that matches a line (it falls back to the colour mode); write
 * the profile to a file. 0, or -1 (no room for the rule, the file not written). */
int  vx_colors_put(vx_colors *c, const vx_rule *r);
int  vx_colors_remove_matching(vx_colors *c, const vx_line *l);
int  vx_colors_save(const vx_colors *c, const char *path, const char *title);

/* The cartridge's checksum: CRC32 of its file without the padding at its end (trailing FF or 00
 * bytes: dumps of one game padded to 8 or 32 KiB have the same one). 0 when it cannot be read. */
uint32_t vx_cart_crc(const char *path);
/* The game being played or coloured, for finding its profiles and for the profiles saved (they get
 * its checksum: "crc = ..."). NULL / "" (Mine Storm, no cartridge): no checksum. */
void vx_colors_game(const char *cart_path);
uint32_t vx_colors_game_crc(void);              /* (0 none) */
/* Whether the profile file was made for this game: 1 yes (its checksum, or one without checksums),
 * 0 for other cartridges (a profile of the same name for another version: its rules may miss). */
int  vx_colors_file_fits(const char *path);

/* A game's profiles (the setting color_profile): 0 OFF, 1 ORIGINAL (the package's colors/<id>.ini),
 * 2, 3 ... the owner's own PROFILE 1, 2 ... (save/colors/<id>.ini, <id>.2.ini, ...: the colour editor's;
 * the first keeps the name an earlier version gave it).
 * ORIGINAL is found by the cartridge's checksum first: the package's colors/<id>.ini when it fits,
 * else any profile in colors/ listing the checksum (another file name, a related game), else the
 * one of the name. Own profiles go by the name, or, when the game has none of its name, by an own
 * profile listing its checksum (the cartridge renamed: its own ones found, new ones saved beside them).
 * The file of ORIGINAL (k 0) or of own profile k (1 ..): out, and 0 when it is there. */
#define VX_PROFILES_OWN 20
int  vx_colors_profile_path(const char *game_id, int k, char *out, size_t size);
/* How many own profiles the game has (the highest k there). */
int  vx_colors_own_count(const char *game_id);
/* The setting as it works for this game: 1 without an ORIGINAL is its PROFILE 1 (2), or OFF (0) when
 * it has none; an own profile not there works as ORIGINAL does. */
int  vx_colors_setting_used(const char *game_id, int setting);
/* The next setting left / right (d) among those the game has: OFF, ORIGINAL, PROFILE 1 .. */
int  vx_colors_setting_step(const char *game_id, int setting, int d);
/* Loads what the setting uses (OFF: none). 0, or -1 (none: n = 0). */
int  vx_colors_load_setting(vx_colors *c, const char *game_id, int setting);

#endif
