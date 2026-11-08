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

typedef struct vx_config {
    /* --- picture (render/draw.h) --- */
    int   monitor;          /* VX_MONITOR_*: the cabinet's tube, 4:3 lying or 3:4 standing */
    float size;             /* 1.0: the Vectrex's picture as large as the tube allows, its own shape kept */
    float size_x, size_y;   /* stretch on top of size (1.0 each: the Vectrex's shape) */
    int   pos_x, pos_y;     /* board units (0..4095 is the tube) the picture is moved by */
    int   flip_x, flip_y;
    float brightness;       /* 0..2: the line colours' level */
    float gamma;            /* the Vectrex's brightness (0..127) to the colour level: level = (z/127)^gamma */
    int   color_r, color_g, color_b;    /* the lines' colour without a colour profile */
    float board_hz;         /* frames a second to the vector board */

    /* --- the machine --- */
    int   skip_bios;        /* 1: the BIOS's intro runs unseen, the game starts at once */
    int   uvmc2_fast;       /* 1: a UVMC2 game in upstream's Fast mode (its list replay handed out by the
                             * emulator, not by the chip's PIO: the console side stays exact, the cartridge's
                             * own timing is approximated); 0: Exact, every chip cycle (too slow on a Pi 5) */

    /* --- digital controls as the Vectrex's analog stick (platform/controls.h) --- */
    float dig_attack;       /* seconds from the middle to all the way, a direction held */
    float dig_release;      /* seconds back to the middle, let go */
    float dig_curve;        /* the ramp's shape: 1 straight, >1 slow at first, <1 fast at first */
    float dig_max;          /* how far a held direction goes, 0..1 */
    float deadzone;         /* an analog stick's dead zone, 0..1 */

    /* --- sound --- */
    float volume;           /* 0..1 */

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

/* Every setting that differs between before and after is set so in dst (the menu's changes made
 * for ALL GAMES, onto every game's settings). */
void vx_config_take_changed(vx_config *dst, const vx_config *before, const vx_config *after);

/* A cartridge file's id for its own files: its name without folder and extension, lower case,
 * a-z 0-9 _ only ("Mine Storm" for an empty slot gives "minestorm"). */
void vx_game_id(const char *cart_path, char *id, int size);

#endif
