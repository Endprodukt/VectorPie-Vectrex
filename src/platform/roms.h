/* roms.h - the Vectrex cartridges on the cabinet.
 *
 * The folders, in this order (a game in an earlier one hides one of the same id in a later one):
 *
 *   rom_dir of the settings          the owner's own, if set
 *   /persistent/games/rom/vectrex    survives VectorPie's updates (made at the start when
 *                                    /persistent/games is there)
 *   /usr/local/share/advance/image/vectrex   AdvanceMESS's cartridges, the VectorPie image's own
 *   roms/ next to the program        (development: the PC)
 *
 * Cartridges: .bin .vec .gam .rom (ROM images), .um2 (UVMC2 games), .pit (PiTrex programs).
 * A game's name: the title in its header (the BIOS's "g GCE" block, as the console shows it), else
 * its file's name.
 *
 * The VectorPie menu (later, see STATUS.md) launches a game by a short name, as it does for
 * AdvanceMESS ("vectrex -cart NAME"): vx_rom_resolve finds it in these folders. */
#ifndef VX_ROMS_H
#define VX_ROMS_H

#define VX_ROM_PATH 1024
#define VX_ROM_NAME 96

typedef struct vx_rom {
    char path[VX_ROM_PATH];
    char name[VX_ROM_NAME];     /* for the screen: capitals */
    char id[64];                /* vx_game_id: its settings' and controls' files */
    int  uvmc2;
} vx_rom;

typedef struct vx_rom_list {
    vx_rom *rom;
    int     n, cap;
    char    dirs[4][VX_ROM_PATH];   /* the folders looked in (for the screen when none is found) */
    int     ndirs;
} vx_rom_list;

/* Reads the folders (rom_dir may be "" ), sorted by name. 0, or -1 without memory. */
int  vx_roms_scan(vx_rom_list *l, const char *rom_dir);
void vx_roms_free(vx_rom_list *l);

/* A cartridge given on the command line: a file that exists, else a name (with or without its
 * extension, any case) in the folders. 0 and the file in out, or -1. */
int  vx_rom_resolve(const char *arg, const char *rom_dir, char *out, int size);

/* The title in a ROM image's header, or "" (not a Vectrex header). */
void vx_rom_title(const unsigned char *img, long n, char *out, int size);

#endif
