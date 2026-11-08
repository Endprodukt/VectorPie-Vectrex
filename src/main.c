/* main.c - the Vectrex on VectorPie.
 *
 *   vectrex                      the game list (platform/browser.h): how the VectorPie menu starts the
 *                                package; each game chosen runs as its own process (below), then the list
 *                                comes back
 *   vectrex CARTRIDGE            one game: a file, or a name in the cartridge folders (platform/roms.h)
 *   vectrex -cart CARTRIDGE      the same, as the VectorPie menu launches AdvanceMESS today
 *                                ("vectrex /usr/local/bin/advmess vectrex -cart "$(clone_rom)"" in
 *                                vector_pie_menu.cfg): the door for the menu to start our games itself
 *   vectrex --minestorm          the empty slot: the console's own Mine Storm
 *
 * A game started from the list is left with Esc (ui_cancel), back to the list; the list with Esc
 * back to the VectorPie menu. A game started directly goes straight back.
 *
 * Options: --skip-bios, and the SDK's: --dvg [PORT], --window, --windowed, --maximized, --vertical,
 * --hz N. Development: --frames N stops after N frames sent; in a game --svg OUT.svg writes the last
 * of them as the board sees it (0..4095, y up); in the list --list prints the games, --pick N plays
 * entry N at once, --child-frames N gives the game it starts --frames N; --menu PAGE opens the options
 * menu on that page at the start (0 main, 1/2 controls, 3 stick, 4 picture, 5 machine, 6 sound).
 *
 * Tab or F2 opens the options menu (platform/menu.h) while a game runs.
 *
 * The BIOS is the package's system/bios.bin. The settings: platform/config.h. */
#include "core/vx_core.h"
#include "platform/app.h"
#include "platform/audio.h"
#include "platform/browser.h"
#include "platform/config.h"
#include "platform/controls.h"
#include "platform/menu.h"
#include "platform/roms.h"
#include "render/draw.h"

#include <vectorpie/vp.h>
#include <vectorpie/vp_paths.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

/* the options that take a value (so their value is not taken for the cartridge) */
static int takes_value(int argc, char **argv, int i)
{
    const char *a = argv[i];
    if (!strcmp(a, "--frames") || !strcmp(a, "--svg") || !strcmp(a, "--hz") || !strcmp(a, "--pick") || !strcmp(a, "--menu") ||
        !strcmp(a, "--child-frames") || !strcmp(a, "-cart"))
        return 1;
    return !strcmp(a, "--dvg") && i + 1 < argc && argv[i + 1][0] != '-';
}

static const char *cart_arg(int argc, char **argv)
{
    const char *c = vx_arg(argc, argv, "-cart");
    if (c) return c;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') return argv[i];
        if (takes_value(argc, argv, i)) i++;
    }
    return NULL;
}

static void load_config(vx_config *all, vx_config *game, const char *id)
{
    char path[1024];
    vx_config_defaults(all);
    vx_config_load(all, vp_asset_path("vectrex.ini", path, sizeof path));
    snprintf(path, sizeof path, "%s/vectrex.ini", vp_save_dir());
    vx_config_load(all, path);
    *game = *all;
    if (id) {
        snprintf(path, sizeof path, "%s/games/%s.ini", vp_save_dir(), id);
        vx_config_load(game, path);
    }
}

static void write_svg(const char *fn, const vx_config *cfg, const vx_line *l, int n)
{
    FILE *f = fopen(fn, "w");
    if (!f) { perror(fn); return; }
    double ar = cfg->monitor == VX_MONITOR_VERTICAL ? 3.0 / 4.0 : 4.0 / 3.0;   /* the tube's shape */
    int W = (int)(800 * ar), H = 800;
    fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' viewBox='0 0 %d %d'>"
               "<rect width='100%%' height='100%%' fill='black'/><rect x='1' y='1' width='%d' height='%d' "
               "fill='none' stroke='#333'/>\n", W, H, W, H, W - 2, H - 2);
    for (int i = 0; i < n; i++) {
        if (!l[i].z) continue;
        float x0, y0, x1, y1;
        vx_map(cfg, l[i].x0, l[i].y0, &x0, &y0);
        vx_map(cfg, l[i].x1, l[i].y1, &x1, &y1);
        int v = 60 + l[i].z * 195 / 127;
        fprintf(f, "<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='rgb(%d,%d,%d)' stroke-width='1.5' "
                   "stroke-linecap='round'/>\n", x0 * W / 4096.0, H - y0 * H / 4096.0, x1 * W / 4096.0,
                H - y1 * H / 4096.0, v, v, v);
    }
    fprintf(f, "</svg>\n");
    fclose(f);
}

/* ---------- one game ---------- */

/* The game's name for the menu's title: its header's title, else its file's name. */
static void game_title(const char *cart, char *out, int size)
{
    snprintf(out, size, "MINE STORM");
    if (!cart) return;
    const char *b = cart;
    for (const char *p = cart; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    snprintf(out, size, "%s", b);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    FILE *f = fopen(cart, "rb");
    if (!f) return;
    unsigned char head[256];
    long n = (long)fread(head, 1, sizeof head, f);
    fclose(f);
    char t[96];
    vx_rom_title(head, n, t, sizeof t);
    if (t[0]) snprintf(out, size, "%s", t);
}

static int run_game(const char *cart, int argc, char **argv)
{
    char id[64];
    vx_game_id(cart, id, sizeof id);
    vx_config all, cfg;
    load_config(&all, &cfg, id);
    if (vx_flag(argc, argv, "--skip-bios")) cfg.skip_bios = 1;
    const char *fr = vx_arg(argc, argv, "--frames"), *svg = vx_arg(argc, argv, "--svg");
    long max_frames = fr ? atol(fr) : 0;

    /* the core reads its UVMC2 mode from the environment when the game is loaded (uvm2_cart.c) */
#ifdef _WIN32
    _putenv(cfg.uvmc2_fast ? "VT_UVM2_FAST=1" : "VT_UVM2_FAST=0");
#else
    setenv("VT_UVM2_FAST", cfg.uvmc2_fast ? "1" : "0", 1);
#endif

    char bios[1024], err[512];
    vp_asset_path("system/bios.bin", bios, sizeof bios);
    if (vx_open(bios, cart, err, sizeof err)) {
        fprintf(stderr, "vectrex: %s\n", err);
        return 1;
    }

    vx_app app;
    if (vx_app_open(&app, "Vectrex", argc, argv, cfg.board_hz, cfg.monitor == VX_MONITOR_VERTICAL)) return 1;
    vx_audio_open();
    vx_controls ctl;
    vx_controls_init(&ctl, app.in, id);
    printf("vectrex: %s (%s)%s\n", cart ? cart : "Mine Storm", id, cfg.skip_bios ? ", BIOS intro skipped" : "");
    fflush(stdout);

    vx_power_on(cfg.skip_bios);
    double owed = 0.0;              /* Vectrex cycles the real time asks for and not yet run */
    long sent = 0;
    vx_pad p1 = { 0, 0, 0 }, p2 = { 0, 0, 0 };
    static vx_menu menu;            /* (static: it holds a copy of the settings) */
    char title[128];
    game_title(cart, title, sizeof title);
    int leave = 0;
    if (vx_arg(argc, argv, "--menu")) {     /* (development: the menu open on that page at the start) */
        vx_menu_open(&menu, &cfg, id, title, vx_cart() == VX_CART_UVMC2);
        menu.page = atoi(vx_arg(argc, argv, "--menu"));
        app.hold_cancel = 1;
    }
    double dt;
    while (!leave && vx_app_poll(&app, &dt)) {
        vx_controls_read(&ctl, app.in, &cfg, dt, &p1, &p2);
        if (!menu.open && !vp_input_held(app.in, "ui_cancel")) app.hold_cancel = 0;   /* (Esc let go after the menu) */
        if (!menu.open && ctl.menu) {
            vx_menu_open(&menu, &cfg, id, title, vx_cart() == VX_CART_UVMC2);
            app.hold_cancel = 1;    /* Esc is the menu's now */
        } else if (menu.open) {
            switch (vx_menu_update(&menu, &cfg, &all, app.in, &p1, app.clock)) {
            case VX_MENU_CHANGED:
                if (app.on_board) app.frame_hz = cfg.board_hz;
                break;
            case VX_MENU_RESTART:
                vx_power_on(cfg.skip_bios);
                menu.open = 0;
                break;
            case VX_MENU_CLOSE:
                vx_menu_save(&menu, &cfg, &all, app.in);
                menu.open = 0;
                break;
            case VX_MENU_QUIT:
                vx_menu_save(&menu, &cfg, &all, app.in);
                menu.open = 0;
                leave = 1;
                break;
            }
        }
        if (menu.open) {            /* the game stands still: no cycles owed, no sound */
            owed = 0.0;
            const vx_line *l;
            int n = vx_lines(&l);
            vx_app_begin_frame(&app);
            if (vx_menu_on_picture(&menu)) vx_draw_lines(app.dvg, &cfg, l, n);
            vx_menu_draw(&menu, &cfg, app.in, app.dvg);
            if (vx_app_end_frame(&app) != 0) break;
            if (max_frames && ++sent >= max_frames) break;
            continue;
        }
        /* the Vectrex at its own pace: whole frames, as many as the time owes (late: at most 3, the
         * rest forgiven, so a stall does not make it race after) */
        owed += dt * VX_CPU_HZ;
        if (owed > 3 * 30000.0) owed = 3 * 30000.0;
        int ran = 0;
        while (owed > 0.0 && ran < 3) {
            vx_set_input(&p1, &p2);
            owed -= (double)vx_run_frame();
            int nf, ch;
            const int16_t *s = vx_audio(&nf, &ch);
            vx_audio_feed(s, nf, ch, cfg.volume);
            ran++;
        }
        const vx_line *l;
        int n = vx_lines(&l);
        vx_app_begin_frame(&app);
        vx_draw_lines(app.dvg, &cfg, l, n);
        if (vx_app_end_frame(&app) != 0) break;
        if (max_frames && ++sent >= max_frames) {
            if (svg) write_svg(svg, &cfg, l, n);
            break;
        }
    }
    vx_audio_close();
    vx_app_close(&app);
    vx_close();
    return 0;
}

/* ---------- the list: each game a process of its own ---------- */

/* The game's command line: this program, the game, and the options given to the list that a game
 * takes too (the output, the shape, --skip-bios; --child-frames N as --frames N). */
static int game_argv(int argc, char **argv, const char *cart, const char **out, int max)
{
    static char exe[1024], frames[32];
#ifdef _WIN32
    vp_asset_path("vectrex.exe", exe, sizeof exe);
#else
    vp_asset_path("vectrex", exe, sizeof exe);
#endif
    int n = 0;
    out[n++] = exe;
    if (cart) { out[n++] = "-cart"; out[n++] = cart; }
    else out[n++] = "--minestorm";
    for (int i = 1; i < argc && n < max - 3; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--dvg")) {
            out[n++] = a;
            if (i + 1 < argc && argv[i + 1][0] != '-') out[n++] = argv[++i];
        } else if (!strcmp(a, "--window") || !strcmp(a, "--windowed") || !strcmp(a, "--maximized") ||
                   !strcmp(a, "--vertical") || !strcmp(a, "--skip-bios")) {
            out[n++] = a;
        } else if (!strcmp(a, "--hz") && i + 1 < argc) {
            out[n++] = a; out[n++] = argv[++i];
        } else if (!strcmp(a, "--child-frames") && i + 1 < argc) {
            snprintf(frames, sizeof frames, "%s", argv[++i]);
            out[n++] = "--frames"; out[n++] = frames;
        } else if (takes_value(argc, argv, i)) {
            i++;
        }
    }
    out[n] = NULL;
    return n;
}

static int spawn_wait(const char **args)
{
#ifdef _WIN32
    /* (_spawnv joins the arguments into one command line: those with spaces need quotes) */
    static char q[32][1100];
    const char *qa[33];
    int k = 0;
    for (; args[k] && k < 32; k++) {
        if (strchr(args[k], ' ')) { snprintf(q[k], sizeof q[k], "\"%s\"", args[k]); qa[k] = q[k]; }
        else qa[k] = args[k];
    }
    qa[k] = NULL;
    intptr_t r = _spawnv(_P_WAIT, args[0], qa);
    return r < 0 ? -1 : (int)r;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execv(args[0], (char *const *)args);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0) { }
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

static void last_game(const char *set, char *get, int size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/browser.ini", vp_save_dir());
    if (set) {
        FILE *f = fopen(path, "w");
        if (f) { fprintf(f, "last = %s\n", set); fclose(f); }
        return;
    }
    get[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256], id[64];
    if (fgets(line, sizeof line, f) && sscanf(line, "last = %63s", id) == 1) snprintf(get, size, "%s", id);
    fclose(f);
}

static int run_browser(int argc, char **argv)
{
    vx_config all, cfg;
    load_config(&all, &cfg, NULL);
    vx_rom_list roms;
    if (vx_roms_scan(&roms, cfg.rom_dir)) { fprintf(stderr, "vectrex: no memory\n"); return 1; }
    if (vx_flag(argc, argv, "--list")) {
        printf("Mine Storm (built in)\n");
        for (int i = 0; i < roms.n; i++) printf("%-40s %s\n", roms.rom[i].name, roms.rom[i].path);
        printf("%d cartridges in:", roms.n);
        for (int d = 0; d < roms.ndirs; d++) printf(" %s", roms.dirs[d]);
        printf("\n");
        vx_roms_free(&roms);
        return 0;
    }
    char last[64];
    last_game(NULL, last, sizeof last);
    vx_browser br;
    vx_browser_init(&br, &roms, last);
    const char *pk = vx_arg(argc, argv, "--pick"), *fr = vx_arg(argc, argv, "--frames");
    int pick = pk ? atoi(pk) : -1;
    long max_frames = fr ? atol(fr) : 0, sent = 0;

    for (;;) {
        vx_app app;
        if (vx_app_open(&app, "Vectrex", argc, argv, cfg.board_hz, cfg.monitor == VX_MONITOR_VERTICAL)) {
            vx_roms_free(&roms);
            return 1;
        }
        app.hold_cancel = 1;            /* (until Esc is seen up: the game just left with it) */
        int play = -1;
        double dt;
        while (vx_app_poll(&app, &dt)) {
            if (!vp_input_held(app.in, "ui_cancel")) app.hold_cancel = 0;
            play = vx_browser_update(&br, &roms, app.in, app.clock);
            if (pick >= 0) { play = br.sel = pick < roms.n + 1 ? pick : 0; pick = -1; }
            vx_app_begin_frame(&app);
            vx_browser_draw(&br, &roms, app.dvg, app.clock);
            if (vx_app_end_frame(&app) != 0) { app.quit = 1; break; }
            if (play >= 0) break;
            if (max_frames && ++sent >= max_frames) { app.quit = 1; break; }
        }
        int quit = app.quit && play < 0;
        vx_app_close(&app);             /* the board, the screen and the controls to the game */
        if (quit) break;

        last_game(vx_browser_id(&roms, play), NULL, 0);
        const char *args[40];
        game_argv(argc, argv, play > 0 ? roms.rom[play - 1].path : NULL, args, 40);
        printf("vectrex: playing %s\n", vx_browser_name(&roms, play));
        fflush(stdout);
        int r = spawn_wait(args);
        if (r != 0) fprintf(stderr, "vectrex: the game ended with %d\n", r);
    }
    vx_roms_free(&roms);
    return 0;
}

int main(int argc, char **argv)
{
    if (vx_flag(argc, argv, "--minestorm")) return run_game(NULL, argc, argv);
    const char *c = cart_arg(argc, argv);
    if (!c) return run_browser(argc, argv);
    vx_config all, cfg;
    load_config(&all, &cfg, NULL);
    char path[VX_ROM_PATH];
    if (vx_rom_resolve(c, cfg.rom_dir, path, sizeof path)) {
        fprintf(stderr, "vectrex: no cartridge %s (a file, or a name in the cartridge folders)\n", c);
        return 1;
    }
    return run_game(path, argc, argv);
}
