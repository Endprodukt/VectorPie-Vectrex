/* vxunit - checks of the settings' layering and the digital ramp, without a screen or controls.
 * Prints each check, returns 0 when all pass. */
#include "platform/config.h"
#include "platform/controls.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;
#define CHECK(c, what) do { int ok_ = (c); printf("%s  %s\n", ok_ ? "ok  " : "FAIL", what); if (!ok_) failed++; } while (0)

static int file_has(const char *path, const char *s)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, s) != NULL;
}

static int exists(const char *p) { FILE *f = fopen(p, "r"); if (f) fclose(f); return f != NULL; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char game[1024], every[1024];
    snprintf(game, sizeof game, "%s/vxunit_game.ini", dir);
    snprintf(every, sizeof every, "%s/vxunit_all.ini", dir);

    /* --- a game's own file holds only what differs from every game's --- */
    vx_config all, cfg, before;
    vx_config_defaults(&all);
    cfg = all;
    cfg.size = 0.9f;
    cfg.skip_bios = 1;
    vx_config_save_diff(&cfg, &all, game);
    CHECK(file_has(game, "size = 0.900") && file_has(game, "skip_bios = 1"), "game file: its two changes");
    CHECK(!file_has(game, "brightness"), "game file: nothing else");
    vx_config back = all;
    vx_config_load(&back, game);
    CHECK(fabsf(back.size - 0.9f) < 1e-4f && back.skip_bios == 1 && back.brightness == all.brightness, "game file read back over every game's");

    /* --- ALL GAMES: only what changed now goes to every game's; the game's own overrides stay --- */
    before = cfg;                  /* (the menu opened: the game has size 0.9, skip_bios 1 of its own) */
    cfg.brightness = 1.5f;         /* changed in the menu with ALL GAMES */
    vx_config_take_changed(&all, &before, &cfg);
    CHECK(fabsf(all.brightness - 1.5f) < 1e-4f, "ALL GAMES: the change in every game's");
    CHECK(fabsf(all.size - 1.0f) < 1e-4f && all.skip_bios == 0, "ALL GAMES: the game's own settings not taken over");
    vx_config_save(&all, every);
    vx_config_save_diff(&cfg, &all, game);
    CHECK(!file_has(game, "brightness") && file_has(game, "size = 0.900"), "game file after ALL GAMES: its own still, not the shared one");

    /* --- THIS GAME AS ALL GAMES: the file goes --- */
    cfg = all;
    vx_config_save_diff(&cfg, &all, game);
    CHECK(!exists(game), "no difference: the game's file removed");
    remove(every);

    /* --- the rom_dir string --- */
    FILE *f = fopen(every, "w");
    fprintf(f, "rom_dir = /some where/vectrex\nmonitor = vertical\n");
    fclose(f);
    vx_config r;
    vx_config_defaults(&r);
    vx_config_load(&r, every);
    CHECK(!strcmp(r.rom_dir, "/some where/vectrex") && r.monitor == VX_MONITOR_VERTICAL, "rom_dir with a space, monitor by name");
    remove(every);

    /* --- the digital ramp --- */
    vx_config c;
    vx_config_defaults(&c);
    c.dig_attack = 0.2f; c.dig_release = 0.1f; c.dig_curve = 1.0f; c.dig_max = 1.0f;
    float ph = 0.0f;
    for (int i = 0; i < 6; i++) ph = vx_ramp_step(ph, 1, 1.0 / 60, &c);    /* 0.1 s held */
    CHECK(fabsf(ph - 0.5f) < 0.01f, "ramp out: half way after half the attack");
    for (int i = 0; i < 12; i++) ph = vx_ramp_step(ph, 1, 1.0 / 60, &c);
    CHECK(ph == 1.0f, "ramp out: all the way, and no further");
    for (int i = 0; i < 3; i++) ph = vx_ramp_step(ph, 0, 1.0 / 60, &c);    /* 0.05 s let go */
    CHECK(fabsf(ph - 0.5f) < 0.01f, "ramp back: half way after half the release");
    for (int i = 0; i < 30; i++) ph = vx_ramp_step(ph, 0, 1.0 / 60, &c);
    CHECK(ph == 0.0f, "ramp back: in the middle");
    ph = 1.0f;
    for (int i = 0; i < 6; i++) ph = vx_ramp_step(ph, -1, 1.0 / 60, &c);   /* reversed: back at the release's speed */
    CHECK(ph == 0.0f, "reversed: through the middle first");
    for (int i = 0; i < 6; i++) ph = vx_ramp_step(ph, -1, 1.0 / 60, &c);
    CHECK(fabsf(ph + 0.5f) < 0.01f, "reversed: then out the other way");
    c.dig_curve = 2.0f;
    CHECK(fabsf(vx_ramp_out(0.5f, &c) - 0.25f) < 1e-4f && fabsf(vx_ramp_out(-0.5f, &c) + 0.25f) < 1e-4f, "curve 2: slow at first, both ways");
    c.dig_curve = 1.0f; c.dig_max = 0.6f;
    CHECK(fabsf(vx_ramp_out(1.0f, &c) - 0.6f) < 1e-4f, "how far: 60 %");
    c.dig_attack = 0.0f;
    CHECK(vx_ramp_step(0.0f, 1, 1.0 / 60, &c) == 1.0f, "attack 0: at once");

    printf("%s\n", failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}
