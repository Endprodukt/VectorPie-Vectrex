/* config.h - the settings (key = value, ';' or '#' comments), read in this order, each file
 * overlaying the one before:
 *
 *   vectrex.ini              next to the program (the package's)
 *   save/vectrex.ini         the owner's, for every game
 *   save/games/<id>.ini      the owner's for one game (<id>: the cartridge file's name, vx_game_id)
 *
 * so a game's own file holds only what differs for it. The options menu writes the owner's
 * files. The controls themselves are the SDK's (platform/controls.h). */
#ifndef VX_CONFIG_H
#define VX_CONFIG_H

enum { VX_MONITOR_HORIZONTAL = 0, VX_MONITOR_VERTICAL = 1 };
enum { VX_COLOR_ONE = 0, VX_COLOR_BRIGHTNESS = 1 };

/* The brightness band (0..3) of a Vectrex brightness 0..127, and its colour in c (0xRRGGBB). */
int vx_band_of(int z);
struct vx_config;
int vx_band_color(const struct vx_config *c, int band);

typedef struct vx_config {
    /* --- picture (render/draw.h) --- */
    int   monitor;          /* VX_MONITOR_*: the cabinet's tube, 4:3 lying or 3:4 standing */
    float size;             /* 1.0: the Vectrex's picture as large as the tube allows, its own shape kept */
    float size_x, size_y;   /* stretch on top of size (1.0 each: the Vectrex's shape) */
    int   pos_x, pos_y;     /* board units (0..4095 is the tube) the picture is moved by */
    int   flip_x, flip_y;
    float brightness;       /* 0..2: the line colours' level */
    float gamma;            /* the Vectrex's brightness (0..127) to the colour level: level = (z/127)^gamma */
    int   color_r, color_g, color_b;    /* the lines' colour without a colour profile (color_mode 0) */
    int   color_mode;       /* VX_COLOR_*: lines no colour profile colours: one colour, or by brightness */
    int   band1, band2, band3, band4;   /* by brightness: the colour (0xRRGGBB) of the Vectrex brightness
                             * 0..47, 48..80, 81..111, 112..127 (the BIOS's $3F, $5F and $7F apart) */
    float board_hz;         /* frames a second to the vector board */

    /* --- the machine --- */
    int   skip_bios;        /* 1: the BIOS's intro runs unseen, the game starts at once */
    int   uvmc2_fast;       /* 1: a UVMC2 game in upstream's Fast mode (its list replay handed out by the
                             * emulator, not by the chip's PIO: the console side stays exact, the cartridge's
                             * own timing is approximated); 0: Exact, every chip cycle (too slow on a Pi 5) */

    int   color_profile;    /* 0 OFF, 1 ORIGINAL, 2.. own PROFILE 1.. (render/colors.h vx_colors_setting_used) */
    int   mains_hum;        /* 1: the core's mains hum wobble on the lines (a real tube has its own) */

    /* --- controller port 2 (platform/port2.h) --- */
    int   port2;            /* vx_port2 (core/vx_core.h): joystick, spinner, trackball, atarivox, vecvox, vecvoice */
    float spin_sens;        /* spinner steps per count of the cabinet's spinner */
    int   spin_reverse;
    int   spin_steps;       /* spinner steps at most per Vectrex frame (a game that reads it less often
                             * loses the direction of steps that come faster) */
    float tb_sens;          /* trackball steps per count of the cabinet's trackball / mouse */
    int   tb_reverse_x, tb_reverse_y;
    int   tb_steps;         /* trackball steps at most per frame and axis */

    /* --- digital controls as the Vectrex's analog stick (platform/controls.h) --- */
    float dig_attack;       /* seconds from the middle to all the way, a direction held */
    float dig_release;      /* seconds back to the middle, let go */
    float dig_curve;        /* the ramp's shape: 1 straight, >1 slow at first, <1 fast at first */
    float dig_max;          /* how far a held direction goes, 0..1 */
    float deadzone;         /* an analog stick's dead zone, 0..1 */

    /* --- sound --- */
    float volume;           /* 0..1 */
    float speech_volume;    /* 0..5: a speech module on port 2 (VecVox, AtariVox, VecVoice) against the
                             * console's sound; 1 as the module is */

    /* --- the cartridges (platform/roms.h) --- */
    char  rom_dir[512];     /* the owner's own folder, looked in first ("" none) */
} vx_config;

void vx_config_defaults(vx_config *c);
/* Overlays what the file has. -1: no file. */
int  vx_config_load(vx_config *c, const char *path);
/* Writes every setting (the owner's file for all games). */
int  vx_config_save(const vx_config *c, const char *path);
/* Writes only the settings where c differs from base (a game's own file). No difference: the file is
 * removed. */
int  vx_config_save_diff(const vx_config *c, const vx_config *base, const char *path);

/* The settings one by one (the menu's two levels, all games and one game's own): how many, a
 * setting's index by its key (-1 none), whether a and b differ in it, and copying it. */
#define VX_CONFIG_MAX_FIELDS 48
int  vx_config_nfields(void);
int  vx_config_field(const char *key);
int  vx_config_differs(const vx_config *a, const vx_config *b, int i);
void vx_config_copy_field(vx_config *dst, const vx_config *src, int i);

/* Every setting that differs between before and after is set so in dst (the menu's changes made
 * for ALL GAMES, onto every game's settings). */
void vx_config_take_changed(vx_config *dst, const vx_config *before, const vx_config *after);

/* port2's name in the files ("joystick", "spinner", "trackball", "atarivox", "vecvox", "vecvoice"). */
const char *vx_port2_name(int d);

/* A cartridge file's id for its own files: its name without folder and extension, lower case,
 * a-z 0-9 _ only ("Mine Storm" for an empty slot gives "minestorm"). */
void vx_game_id(const char *cart_path, char *id, int size);

#endif
