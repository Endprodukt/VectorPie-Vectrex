/* vxunit - checks of the settings' layering and the digital ramp, without a screen or controls.
 * Prints each check, returns 0 when all pass. */
#include "platform/config.h"
#include "platform/coloredit.h"
#include "platform/controls.h"
#include <vectorpie/vp_controls.h>
#include "platform/port2.h"
#include "render/colors.h"
#include "render/draw.h"

#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vectorpie/vp_paths.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

static int failed;
#define CHECK(c, what) do { int ok_ = (c); printf("%s  %s\n", ok_ ? "ok  " : "FAIL", what); if (!ok_) failed++; } while (0)

/* the piece a line belongs to (-1 none) */
static int piece_of_line(const vx_piece *p, int np, int line)
{
    for (int k = 0; k < np; k++) if (line >= p[k].first && line <= p[k].last) return k;
    return -1;
}

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

    /* --- one setting at a time (the menu's two levels) --- */
    {
        vx_config a, b;
        vx_config_defaults(&a);
        b = a;
        int fs = vx_config_field("size"), fc = vx_config_field("color_g"), fx = vx_config_field("nothing");
        CHECK(fs >= 0 && fc >= 0 && fx < 0 && vx_config_nfields() <= VX_CONFIG_MAX_FIELDS, "fields by key");
        b.size = 0.8f;
        CHECK(vx_config_differs(&a, &b, fs) && !vx_config_differs(&a, &b, fc), "differs: in that setting only");
        vx_config_copy_field(&a, &b, fs);
        CHECK(fabsf(a.size - 0.8f) < 1e-4f, "copy one setting");
    }

    /* --- the controls files, function by function --- */
    {
        char p[1024];
        snprintf(p, sizeof p, "%s/vxunit_controls.ini", dir);
        vx_fn_table t;
        memset(&t, 0, sizeof t);
        snprintf(t.v[4], sizeof t.v[4], "key_Space+joy1_button2");
        snprintf(t.v[9], sizeof t.v[9], "none");
        vx_fn_table_write(&t, p);
        vx_fn_table r;
        vx_fn_table_read(&r, p);
        CHECK(!strcmp(r.v[4], "key_Space+joy1_button2") && !strcmp(r.v[9], "none") && !r.v[0][0],
              "controls file: only the functions named, read back");
        CHECK(file_has(p, "pad1_b1 = key_Space+joy1_button2") && file_has(p, "pad2_down = none"),
              "controls file: the SDK's own form (function = controls)");
        memset(&t, 0, sizeof t);
        vx_fn_table_write(&t, p);
        CHECK(!exists(p), "controls file: nothing in it, removed");
    }

    /* --- the spinner on port 2, read the way C. Malcolm's spinner example (2017) reads it --- */
    {
        static const int tblSpin[4] = { 0, 3, 1, 2 };
        static const int tblPrev[4][4] = { { 0, 1, 0, -1 }, { -1, 0, 1, 0 }, { 0, -1, 0, 1 }, { 1, 0, -1, 0 } };
        vx_port2_dev d;
        memset(&d, 0, sizeof d);
        d.kind = VX_P2_SPINNER; d.gap_x = 100; d.limit = 20;
        int cur = 0, sum = 0, ok = 1;
        d.acc_x = 7;                                         /* seven steps one way */
        for (unsigned long long c = 0; c < 60000; c += 50) { /* (read far more often than it steps) */
            unsigned b = vx_port2_hook(&d, c);
            int pos = tblSpin[b & 3], dir = tblPrev[cur][pos];
            if (dir == 0 && pos != cur) ok = 0;              /* (a jump of two: no direction) */
            cur = pos; sum += dir;
        }
        CHECK(ok && sum == 7 && d.acc_x == 0, "spinner: 7 steps read as +7 by the example's tables");
        d.acc_x = -5;
        for (unsigned long long c = 60000; c < 120000; c += 50) {
            unsigned b = vx_port2_hook(&d, c);
            int pos = tblSpin[b & 3];
            sum += tblPrev[cur][pos]; cur = pos;
        }
        CHECK(sum == 2, "spinner: then 5 the other way, +2 in all");
        CHECK((vx_port2_hook(&d, 120000) & 0xc) == 0, "spinner: buttons 3 and 4 untouched");
    }

    /* --- the trackball, read the way the Fortress of Narzod patch's TB_POLL reads it --- */
    {
        vx_port2_dev d;
        memset(&d, 0, sizeof d);
        d.kind = VX_P2_TRACKBALL; d.gap_x = d.gap_y = 100; d.limit = 20;
        d.acc_x = -6; d.acc_y = 4;                          /* 6 left, 4 up */
        unsigned prev = 0;
        int x = 0, y = 0;
        for (unsigned long long c = 0; c < 60000; c += 40) {
            unsigned a = vx_port2_hook(&d, c);              /* bit 0..3 = buttons 1..4, 1 = pressed (line low) */
            unsigned ch = a ^ prev;
            prev = a;
            if (ch & 3) x += (a & 1) ? -1 : 1;              /* X pair changed: button 1 pressed = left */
            if (ch & 12) y += (a & 4) ? 1 : -1;             /* Y pair changed: button 3 pressed = up */
        }
        CHECK(x == -6 && y == 4, "trackball: 6 left and 4 up read as the Narzod patch reads them");
    }

    /* --- a colour profile --- */
    {
        char p[1024];
        snprintf(p, sizeof p, "%s/vxunit_colors.ini", dir);
        FILE *f = fopen(p, "w");
        fprintf(f, "; test\nshots = pc F2C0-F2DF u C900-C92F : FFFFFF\nship = x CB80-CBFF line : 40FF60\n"
                   "dim = bright 0-40 : 102030 *1.5\nbad = x zz : 000000\nbad2 = dot : FFFFFF *x\n");
        fclose(f);
        static vx_colors c;
        vx_colors_load_file(&c, p);
        CHECK(c.n == 3, "colours: three rules read, the bad one left out");
        vx_line l;
        memset(&l, 0, sizeof l);
        uint8_t r = 0, g = 0, b = 0;
        l.x0 = 100; l.x1 = 150; l.z = 100; l.tag.pc = 0xF2CD; l.tag.u = 0xC915;
        CHECK(vx_colors_line(&c, &l, &r, &g, &b) && r == 0xFF && g == 0xFF && b == 0xFF, "colours: a shot (pc and u)");
        l.tag.pc = 0xF425; l.tag.u = 0; l.tag.x = 0xCB90; l.x1 = 5000;
        CHECK(vx_colors_line(&c, &l, &r, &g, &b) && r == 0x40 && g == 0xFF && b == 0x60, "colours: the ship (x, a line)");
        l.x1 = 120;
        CHECK(!vx_colors_line(&c, &l, &r, &g, &b), "colours: the ship rule wants a line, not a dot");
        l.z = 30; l.tag.x = 0;
        CHECK(vx_colors_line(&c, &l, &r, &g, &b) && r == 0x10, "colours: by brightness");
        const vx_rule *u = vx_colors_find(&c, &l);
        CHECK(u && fabsf(u->gain - 1.5f) < 1e-4f && fabsf(c.rule[0].gain - 1.0f) < 1e-4f, "colours: a rule's gain (*1.5), 1 without");
        remove(p);
    }

    /* --- a colour by the line's length (Clean Sweep: money dashes and walls from one code) --- */
    {
        char p[1024];
        snprintf(p, sizeof p, "%s/vxunit_length.ini", dir);
        FILE *f = fopen(p, "w");
        fprintf(f, "money = caller 0096 length 0-820 : 82FF82\nwall = caller 0096 length 820-41000 : D2AA78\n");
        fclose(f);
        static vx_colors c;
        vx_colors_load_file(&c, p);
        vx_line l;
        memset(&l, 0, sizeof l);
        uint8_t r = 0, g = 0, b = 0;
        l.tag.caller = 0x0096; l.z = 127; l.x0 = 1000; l.y0 = 1000; l.x1 = 1000; l.y1 = 1400;
        CHECK(c.n == 2 && vx_colors_line(&c, &l, &r, &g, &b) && r == 0x82, "colours by length: a short dash");
        l.y1 = 4300;
        CHECK(vx_colors_line(&c, &l, &r, &g, &b) && r == 0xD2, "colours by length: a long line");
        l.x0 = 0; l.y0 = 0; l.x1 = 35000; l.y1 = 41000;     /* (longer than the range: no 32-bit overflow) */
        CHECK(!vx_colors_line(&c, &l, &r, &g, &b), "colours by length: the whole diagonal is longer than both");
        vx_colors_save(&c, p, "test");
        CHECK(file_has(p, "length 820-41000"), "colours by length: saved");
        f = fopen(p, "w");
        fprintf(f, "vault = caller 0096 area 15000-20000 15000-26000 : FFF050\nvault2 = caller 0096 area 15000-20000 30000-40000 : 000001\n");
        fclose(f);
        vx_colors_load_file(&c, p);
        l.x0 = 16000; l.y0 = 16000; l.x1 = 16000; l.y1 = 25000;
        CHECK(c.n == 2 && vx_colors_line(&c, &l, &r, &g, &b) && r == 0xFF && g == 0xF0, "colours by place: a line inside the box");
        l.y1 = 27000;
        CHECK(!vx_colors_line(&c, &l, &r, &g, &b), "colours by place: a line reaching out of the box");
        vx_rule other = c.rule[0];
        other.cond[1].lo2 = 30000; other.cond[1].hi2 = 40000;
        vx_colors_put(&c, &other);
        CHECK(c.n == 2, "colours by place: two boxes apart in y are two rules, not one");
        vx_colors_save(&c, p, "test");
        CHECK(file_has(p, "area 15000-20000 15000-26000"), "colours by place: saved");
        remove(p);
    }

    /* --- colours by brightness --- */
    {
        CHECK(vx_band_of(0x3F) == 1 && vx_band_of(0x5F) == 2 && vx_band_of(0x7F) == 3 && vx_band_of(20) == 0,
              "brightness bands: the BIOS's $3F, $5F, $7F apart");
        vx_config c;
        vx_config_defaults(&c);
        c.color_mode = VX_COLOR_BRIGHTNESS;
        c.band2 = 0xFF0000;
        c.brightness = 2.0f;                       /* (full level whatever the brightness) */
        vx_line l;
        memset(&l, 0, sizeof l);
        l.z = 0x3F; l.x1 = 5000;
        uint8_t r, g, b;
        CHECK(vx_line_color(&c, NULL, &l, &r, &g, &b) && r == 0xFF && g == 0 && b == 0, "brightness bands: a $3F line in band 2's colour");
        char p[1024];
        snprintf(p, sizeof p, "%s/vxunit_bands.ini", dir);
        vx_config base;
        vx_config_defaults(&base);
        vx_config_save_diff(&c, &base, p);
        CHECK(file_has(p, "band2 = FF0000") && file_has(p, "color_mode = 1"), "brightness bands: saved as RRGGBB");
        vx_config back = base;
        vx_config_load(&back, p);
        CHECK(back.band2 == 0xFF0000 && back.color_mode == 1, "brightness bands: read back");
        remove(p);
    }

    /* --- the colour editor on a real Mine Storm picture (needs the BIOS: argument 2) --- */
    if (argc > 2) {
        char err[256];
        if (vx_open(argv[2], NULL, err, sizeof err) == 0) {
            vx_power_on(1);
            vx_pad p = { 0, 0, 0 }, q = { 0, 0, 0 };
            static const int fire[] = { 60, 300, 320, 340, 360, 380, 400, 500, 520, 540, 560, 580, 597 };
            for (int f = 0; f < 600; f++) {
                p.buttons = 0;
                for (unsigned k = 0; k < sizeof fire / sizeof fire[0]; k++)
                    if (f >= fire[k] && f <= fire[k] + (k ? 2 : 4)) p.buttons = 8;
                vx_set_input(&p, &q);
                vx_run_frame();
            }
            const vx_line *l;
            int n = vx_lines(&l);
            static vx_piece pc[VX_CE_PIECES];
            int np = vx_ce_pieces(l, n, pc, VX_CE_PIECES);
            int ship = -1, mine = -1, seed = -1;
            for (int i = 0; i < np; i++) {
                if (pc[i].u == 0xCB2B && pc[i].x_lo >= 0xCB80 && ship < 0) ship = i;
                if (pc[i].x_lo >= 0xEE40 && pc[i].x_hi <= 0xEE7F && !pc[i].dots && mine < 0) mine = i;
                if (pc[i].dots && pc[i].u >= 0xC933 && pc[i].u < 0xCB2B && seed < 0) seed = i;
            }
            CHECK(np > 5 && ship >= 0 && mine >= 0 && seed >= 0, "colour editor: Mine Storm's picture in pieces (ship, mine, seed)");
            /* the truth, by the hand-made rules of package/colors/minestorm.ini */
            int n_ship = 0, n_mine_kind = 0, n_seed = 0, n_shot = 0;
            for (int i = 0; i < n; i++) {
                const vx_tag *t = &l[i].tag;
                if (!l[i].z) continue;
                n_ship += t->u == 0xCB2B;
                n_mine_kind += (t->pc & ~0x1F) == pc[mine].pc_lo && t->x >= 0xEE40 && t->x <= 0xEE7F;
                n_seed += t->pc >= 0xF2C0 && t->pc <= 0xF2DF && t->u >= 0xC930 && t->u < 0xCB2B;
                n_shot += t->pc >= 0xF2C0 && t->pc <= 0xF2DF && t->u >= 0xC900 && t->u <= 0xC92F;
            }
            vx_rule r;
            int hit, hit_other;
            vx_ce_rule(pc, np, ship, l, n, VX_CE_OBJECT, 0, 200, 255, 1.0f, &r);
            hit = 0; hit_other = 0;
            for (int i = 0; i < n; i++) if (l[i].z && vx_rule_matches(&r, &l[i])) { hit++; hit_other += l[i].tag.u != 0xCB2B; }
            CHECK(hit == n_ship && hit_other == 0, "colour editor: THIS OBJECT on the ship = exactly the ship's lines");
            vx_ce_rule(pc, np, mine, l, n, VX_CE_SHAPE, 255, 50, 50, 1.0f, &r);
            hit = 0;
            for (int i = 0; i < n; i++) if (l[i].z && vx_rule_matches(&r, &l[i])) hit++;
            CHECK(hit >= pc[mine].last - pc[mine].first + 1 && hit <= n_mine_kind, "colour editor: ALL OF THIS SHAPE on a mine = mines only");
            vx_ce_rule(pc, np, seed, l, n, VX_CE_GROUP, 255, 80, 80, 1.6f, &r);
            hit = 0; hit_other = 0;
            for (int i = 0; i < n; i++)
                if (l[i].z && vx_rule_matches(&r, &l[i])) { hit++; hit_other += l[i].tag.u >= 0xC900 && l[i].tag.u <= 0xC92F; }
            printf("      (seeds %d, shots %d, the group rule hits %d, shots among them %d)\n", n_seed, n_shot, hit, hit_other);
            CHECK(hit > 1 && hit_other == 0, "colour editor: THIS GROUP on a seed = seeds, no shots");
            /* THIS LINE: one stroke of the mine's list, in every mine of its kind, nothing else */
            {
                int at = pc[mine].first;
                vx_ce_rule_line(l, n, at, 255, 255, 0, 1.0f, &r);
                int hit2 = 0, wrong = 0;
                for (int i = 0; i < n; i++)
                    if (l[i].z && vx_rule_matches(&r, &l[i])) { hit2++; wrong += l[i].tag.x != l[at].tag.x; }
                CHECK(hit2 >= 1 && wrong == 0 && hit2 < n_mine_kind, "colour editor: THIS LINE = one stroke of the shape");
            }
            CHECK(vx_ce_palette_count() >= 50, "colour editor: a large palette (50 colours or more)");

            /* the rules into a profile, written and read back */
            static vx_colors c;
            memset(&c, 0, sizeof c);
            vx_ce_rule(pc, np, ship, l, n, VX_CE_OBJECT, 0, 200, 255, 1.0f, &r); vx_colors_put(&c, &r);
            vx_ce_rule(pc, np, seed, l, n, VX_CE_GROUP, 255, 80, 80, 1.6f, &r); vx_colors_put(&c, &r);
            vx_ce_rule(pc, np, ship, l, n, VX_CE_OBJECT, 0, 255, 0, 1.0f, &r); vx_colors_put(&c, &r);   /* (again: replaced) */
            char path[1024];
            snprintf(path, sizeof path, "%s/vxunit_editor.ini", dir);
            vx_colors_save(&c, path, "TEST");
            static vx_colors back;
            vx_colors_load_file(&back, path);
            uint8_t rr = 0, gg = 0, bb = 0;
            int ok = back.n == 2 && vx_colors_line(&back, &l[pc[ship].first], &rr, &gg, &bb) && gg == 255 && rr == 0 &&
                     fabsf(back.rule[1].gain - 1.6f) < 0.01f;
            CHECK(ok, "colour editor: rules put, replaced, saved and read back");
            CHECK(vx_colors_remove_matching(&back, &l[pc[ship].first]) == 1 && back.n == 1, "colour editor: NONE removes the rule");
            remove(path);

            /* a game's profiles: own ones 1 and 3, no ORIGINAL (the save folder's colors/) */
            {
                const char *g = "vxunit_profiles";
                char p1[1100], p3[1100], p2[1100];
                vx_colors_profile_path(g, 1, p1, sizeof p1);
                vx_colors_profile_path(g, 3, p3, sizeof p3);
                vx_colors_profile_path(g, 2, p2, sizeof p2);
                char d0[1100];
                snprintf(d0, sizeof d0, "%s", vp_save_dir()); MKDIR(d0);
                snprintf(d0, sizeof d0, "%s/colors", vp_save_dir()); MKDIR(d0);
                if (vx_colors_save(&c, p1, "TEST") != 0 || vx_colors_save(&c, p3, "TEST") != 0)
                    printf("      (cannot write %s)\n", p1);
                int nw = 0, t;
                CHECK(vx_colors_own_count(g) == 3, "profiles: the own ones counted (the highest there)");
                CHECK(vx_colors_setting_used(g, 1) == 2, "profiles: ORIGINAL missing, its PROFILE 1 used");
                CHECK(vx_colors_setting_used(g, 3) == 2, "profiles: PROFILE 2 missing, as ORIGINAL: PROFILE 1 used");
                int s1 = vx_colors_setting_step(g, 0, 1), s2 = vx_colors_setting_step(g, s1, 1), s3 = vx_colors_setting_step(g, s2, 1);
                CHECK(s1 == 2 && s2 == 4 && s3 == 0 && vx_colors_setting_step(g, 0, -1) == 4,
                      "profiles: the choice steps OFF, PROFILE 1, PROFILE 3, OFF (only those there)");
                t = vx_coloredit_target(g, 0, 0, &nw);
                CHECK(t == 4 && nw, "profiles: the editor on OFF makes a new one (number 4)");
                t = vx_coloredit_target(g, 4, 0, &nw);
                CHECK(t == 3 && !nw, "profiles: the editor on PROFILE 3 changes it");
                t = vx_coloredit_target(g, 4, 1, &nw);
                CHECK(t == 4 && nw, "profiles: asked for a new one, PROFILE 3 stays");
                static vx_colors pc3;
                CHECK(vx_colors_load_setting(&pc3, g, 4) == 0 && pc3.n == 2 && vx_colors_load_setting(&pc3, g, 0) == -1 && pc3.n == 0,
                      "profiles: loaded by the setting (OFF: none)");
                remove(p1); remove(p3); (void)p2;
                char po[1100];                  /* (Mine Storm has an ORIGINAL in the package: colors/) */
                if (vx_colors_profile_path("minestorm", 0, po, sizeof po) == 0 && vx_colors_own_count("minestorm") == 0)
                    CHECK(vx_colors_setting_step("minestorm", 0, 1) == 1 && vx_colors_setting_step("minestorm", 1, 1) == 0 &&
                          vx_coloredit_target("minestorm", 1, 0, &nw) == 1 && nw,
                          "profiles: OFF, ORIGINAL; the editor on ORIGINAL makes PROFILE 1, ORIGINAL stays");
            }

            /* checksums: padding left out; saved into a profile; a profile found by it under another name;
             * one of the name for another version flagged */
            {
                char ca[1100], cb[1100], pa[1100], pn[1100];
                snprintf(ca, sizeof ca, "%s/vxunit_cart_a.bin", dir);
                snprintf(cb, sizeof cb, "%s/vxunit_cart_b.bin", dir);
                unsigned char img[64];
                for (int i = 0; i < 64; i++) img[i] = (unsigned char)(i * 7 + 1);
                FILE *f = fopen(ca, "wb"); fwrite(img, 1, 48, f); fclose(f);               /* the game */
                f = fopen(cb, "wb"); fwrite(img, 1, 48, f); for (int i = 0; i < 16; i++) fputc(0xFF, f); fclose(f);   /* padded */
                uint32_t a = vx_cart_crc(ca), b = vx_cart_crc(cb);
                CHECK(a != 0 && a == b, "checksum: the same with the padding at the end");
                vx_colors_game(ca);
                vp_asset_path("colors/vxunit_crc_game.ini", pa, sizeof pa);
                static vx_colors cs, cl;
                memset(&cs, 0, sizeof cs);
                vx_ce_rule(pc, np, ship, l, n, VX_CE_OBJECT, 0, 200, 255, 1.0f, &r); vx_colors_put(&cs, &r);
                vx_colors_save(&cs, pa, "TEST");
                vx_colors_load_file(&cl, pa);
                CHECK(cl.ncrc == 1 && cl.crc[0] == a && cl.n == 1, "checksum: written into a profile saved, read back");
                CHECK(vx_colors_profile_path("vxunit_renamed", 0, pn, sizeof pn) == 0 && !strcmp(pn, pa),
                      "checksum: a renamed cartridge finds its ORIGINAL by it");
                {   /* an own profile follows its cartridge renamed: found by the checksum, new ones beside it */
                    char po[1100], pf[1100];
                    vx_colors_profile_path("vxunit_own", 1, po, sizeof po);
                    vx_colors_save(&cs, po, "TEST");
                    int k = vx_colors_own_count("vxunit_moved");
                    int got = vx_colors_profile_path("vxunit_moved", 1, pf, sizeof pf) == 0 && !strcmp(pf, po);
                    vx_colors_profile_path("vxunit_moved", 2, pf, sizeof pf);
                    CHECK(k == 1 && got && strstr(pf, "vxunit_own.2.ini") != NULL,
                          "checksum: a renamed cartridge finds its own profiles, a new one goes beside them");
                    remove(po);
                }
                img[0] ^= 0x55;                          /* another version of the game */
                f = fopen(cb, "wb"); fwrite(img, 1, 48, f); fclose(f);
                vx_colors_game(cb);
                CHECK(!vx_colors_file_fits(pa) && vx_colors_profile_path("vxunit_renamed", 0, pn, sizeof pn) != 0,
                      "checksum: another version is flagged, and does not take the profile by checksum");
                vx_colors_game(NULL);
                CHECK(vx_colors_file_fits(pa), "checksum: without a cartridge (Mine Storm) every profile fits");
                remove(ca); remove(cb); remove(pa);
            }

            /* the mouse: a box over the whole screen, a click on the ship, Ctrl+click */
            {
                static vx_coloredit ce;
                vx_config cc;
                vx_config_defaults(&cc);
                if (vx_coloredit_open(&ce, l, n) == 0) {
                    int inside = 0;
                    for (int i = 0; i < n; i++) {
                        if (!l[i].z) continue;
                        float a, b2, c2, d2;
                        vx_map(&cc, l[i].x0, l[i].y0, &a, &b2); vx_map(&cc, l[i].x1, l[i].y1, &c2, &d2);
                        inside += a >= 0 && a <= 4095 && c2 >= 0 && c2 <= 4095 && b2 >= 0 && b2 <= 4095 && d2 >= 0 && d2 <= 4095;
                    }
                    vx_ce_mouse_pick(&ce, &c, &cc, l, n, 0, 0, 4095, 4095, 1, 0);
                    CHECK(ce.panel && ce.nmsel == inside && inside > 0 && ce.ncand >= 2 && ce.ncand <= np,
                          "mouse: a box over the screen picks every line on it, one rule per shape");
                    int i = pc[ship].first;
                    while (i <= pc[ship].last && !l[i].z) i++;
                    float x0, y0, x1, y1;
                    vx_map(&cc, l[i].x0, l[i].y0, &x0, &y0); vx_map(&cc, l[i].x1, l[i].y1, &x1, &y1);
                    int mx = (int)((x0 + x1) / 2), my = (int)((y0 + y1) / 2);
                    vx_ce_mouse_pick(&ce, &c, &cc, l, n, mx, my, mx, my, 0, 0);
                    i = ce.line;                    /* (the line under the mouse: one of the ship's, some lie on each other) */
                    CHECK(ce.nmsel == 0 && ce.sel == ship && ce.ncand == 1 && i >= pc[ship].first && i <= pc[ship].last,
                          "mouse: a click picks the shape under it (and its line)");
                    ce.scope = VX_CE_LINE;
                    int j = pc[mine].first;
                    while (j <= pc[mine].last && !l[j].z) j++;
                    vx_map(&cc, l[j].x0, l[j].y0, &x0, &y0); vx_map(&cc, l[j].x1, l[j].y1, &x1, &y1);
                    int nx = (int)((x0 + x1) / 2), ny = (int)((y0 + y1) / 2);
                    vx_ce_mouse_pick(&ce, &c, &cc, l, n, nx, ny, nx, ny, 0, 1);
                    CHECK(ce.nmsel == 2 && ce.msel[i] && ce.msel[j] && ce.ncand == 2 && ce.scope == VX_CE_LINE,
                          "mouse: Ctrl+click adds a line (THIS LINE: a rule for each)");
                    /* SMART: a click on the ship picks what is connected to its line: closed (no line left out
                     * touches one picked), the clicked line in it; a shape rule exactly when a piece is picked whole */
                    ce.scope = VX_CE_SMART;
                    vx_ce_mouse_pick(&ce, &c, &cc, l, n, mx, my, mx, my, 0, 0);
                    int closed = 1, shape_rule = 0, full_piece = 0;
#define TOUCH(a, b) (((a).tag.u == (b).tag.u || (a).piece == (b).piece) && ((labs((a).x0 - (b).x0) <= 350 && labs((a).y0 - (b).y0) <= 350) || (labs((a).x0 - (b).x1) <= 350 && labs((a).y0 - (b).y1) <= 350) || \
                     (labs((a).x1 - (b).x0) <= 350 && labs((a).y1 - (b).y0) <= 350) || (labs((a).x1 - (b).x1) <= 350 && labs((a).y1 - (b).y1) <= 350)))
                    for (int a = 0; a < n && closed; a++) if (ce.msel[a])
                        for (int b = 0; b < n; b++) if (!ce.msel[b] && l[b].z && TOUCH(l[a], l[b])) { closed = 0; break; }
#undef TOUCH
                    for (int p = 0; p < np; p++) {
                        int lit = 0, in = 0;
                        for (int k = pc[p].first; k <= pc[p].last; k++) if (l[k].z) { lit++; in += ce.msel[k] != 0; }
                        full_piece |= lit > 0 && in == lit;
                    }
                    for (int k = 0; k < ce.ncand; k++) shape_rule |= !strncmp(ce.cands[k].name, "shape_", 6);
                    CHECK(ce.msel[ce.line] && ce.nmsel >= 2 && closed && shape_rule == full_piece && ce.scope == VX_CE_SMART,
                          "SMART: a click picks the connected figure (closed); a shape rule just for shapes picked whole");
                    int before = ce.nmsel;
                    vx_ce_mouse_pick(&ce, &c, &cc, l, n, nx, ny, nx, ny, 0, 1);
                    CHECK(ce.msel[j] && ce.nmsel == before + 1, "SMART: Shift+click adds one single line");
                    /* SAME KIND on a ROM shape (a mine): the same lines as ALL OF THIS SHAPE, nothing narrowed */
                    ce.scope = VX_CE_KIND;
                    vx_ce_mouse_pick(&ce, &c, &cc, l, n, nx, ny, nx, ny, 0, 0);
                    vx_rule sh;
                    vx_ce_rule(pc, np, piece_of_line(pc, np, ce.line), l, n, VX_CE_SHAPE, 255, 255, 255, 1.0f, &sh);
                    int same = ce.ncand == 1;
                    for (int k = 0; k < n && same; k++) if (l[k].z) same = !vx_rule_matches(&sh, &l[k]) == !vx_ce_matches(&ce, &l[k]);
                    CHECK(same && ce.scope == VX_CE_KIND, "SAME KIND: on a mine the same lines as its shape");
                }
            }
            vx_close();
        } else {
            CHECK(0, "colour editor: the BIOS (argument 2) could not be opened");
        }

        /* SAME KIND on Clean Sweep (when DIR/roms/sweep.vec is there): a coin takes the coins, a wall the
         * walls drawn in the same go, the vacuum and a robber (one list in RAM) apart */
        char cart[1100];
        snprintf(cart, sizeof cart, "%s/roms/sweep.vec", dir);
        FILE *cf = fopen(cart, "rb");
        if (cf) fclose(cf);
        if (cf && vx_open(argv[2], cart, err, sizeof err) == 0) {
            vx_power_on(1);
            vx_pad p = { 0, 0, 0 }, q = { 0, 0, 0 };
            for (int f = 0; f < 2200; f++) { vx_set_input(&p, &q); vx_run_frame(); }
            const vx_line *l;
            int n = vx_lines(&l);
            static vx_coloredit ce;
            static vx_colors nocol;
            vx_config cc;
            vx_config_defaults(&cc);
            if (vx_coloredit_open(&ce, l, n) == 0) {
                ce.scope = VX_CE_KIND;
                ce.panel = 1;                        /* (open, as in the Studio: a click keeps the mode) */
                int field = 0;                       /* (the piece with the most parts: coins and walls) */
                for (int k = 1; k < ce.npieces; k++) if (ce.piece[k].parts > ce.piece[field].parts) field = k;
                int coin = -1, wall = -1;
                for (int i = ce.piece[field].first; i <= ce.piece[field].last; i++) {
                    double d = hypot((double)(l[i].x1 - l[i].x0), (double)(l[i].y1 - l[i].y0));
                    if (d < 0.02 * VX_CANVAS_H && coin < 0) coin = i;
                    if (d > 0.06 * VX_CANVAS_H && wall < 0) wall = i;
                }
                int ok = coin >= 0 && wall >= 0, coins = 0, walls = 0, cross = 0;
                for (int pass = 0; pass < 2 && ok; pass++) {
                    int at = pass ? wall : coin;
                    float x0, y0, x1, y1;
                    vx_map(&cc, l[at].x0, l[at].y0, &x0, &y0); vx_map(&cc, l[at].x1, l[at].y1, &x1, &y1);
                    int mx = (int)((x0 + x1) / 2), my = (int)((y0 + y1) / 2);
                    vx_ce_mouse_pick(&ce, &nocol, &cc, l, n, mx, my, mx, my, 0, 0);
                    for (int i = ce.piece[field].first; i <= ce.piece[field].last; i++) {
                        if (!l[i].z || !vx_ce_matches(&ce, &l[i])) continue;
                        double d = hypot((double)(l[i].x1 - l[i].x0), (double)(l[i].y1 - l[i].y0));
                        int small = d < 0.015 * VX_CANVAS_H;
                        if (pass) { walls++; cross += small; } else { coins++; cross += !small; }
                    }
                }
                CHECK(ok && coins >= 100 && walls >= 10 && cross == 0, "SAME KIND: Clean Sweep's coins and walls apart");
                int vac = -1, rob = -1;                /* (the figures drawn from the list in RAM: dots, or not) */
                for (int k = 0; k < ce.npieces; k++) {
                    const vx_piece *t = &ce.piece[k];
                    if (t->x_lo < 0xC800 || t->x_hi > 0xCBFF || t->parts < 5) continue;
                    if (t->dots && vac < 0) vac = k; else if (!t->dots && rob < 0) rob = k;
                }
                int apart = vac >= 0 && rob >= 0;
                if (apart) {
                    int i = ce.piece[vac].first;
                    float x0, y0, x1, y1;
                    vx_map(&cc, l[i].x0, l[i].y0, &x0, &y0); vx_map(&cc, l[i].x1, l[i].y1, &x1, &y1);
                    vx_ce_mouse_pick(&ce, &nocol, &cc, l, n, (int)x0, (int)y0, (int)x0, (int)y0, 0, 0);
                    for (int k = ce.piece[rob].first; k <= ce.piece[rob].last && apart; k++) apart = !vx_ce_matches(&ce, &l[k]);
                    apart = apart && vx_ce_matches(&ce, &l[ce.piece[vac].first]);
                }
                CHECK(apart, "SAME KIND: Clean Sweep's vacuum without the robbers");
            }
            vx_close();
        }
    }

    /* --- the menus' controls: as the VectorPie menu's; where MAME's settings give a ui_ action nothing (no
     * advmame.rc here), the VectorPie menu's own keys --- */
    {
        vp_input *in = NULL;
        if (vp_input_open(&in) == 0 && in) {
            vx_controls ctl;
            vx_controls_init(&ctl, in, NULL);
            char b[256];
            int ok = 1;
            static const struct { const char *fn, *want; } W[] = {
                { "menu_up", "UP" }, { "menu_down", "DOWN" }, { "menu_left", "LEFT" }, { "menu_right", "RIGHT" },
                { "menu_select", "RETURN" }, { "menu_back", "ESC" }, { "menu_options", "TAB" },
            };
            for (int k = 0; k < 7; k++) {
                vp_input_spelled(in, W[k].fn, vp_input_controls(in, W[k].fn), b, sizeof b);
                for (char *q = b; *q; q++) *q = (char)toupper((unsigned char)*q);
                if (!strstr(b, W[k].want)) { ok = 0; printf("      %s: %s\n", W[k].fn, b); }
            }
            CHECK(ok, "menus: every menu function has a control (the VectorPie menu's keys where MAME's settings give none)");
            vp_input_close(in);
        }
    }

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

    /* --- the speech: the AtariVox finds the module from the game's bytes (the VecVoice for Verzerk (Voice), the
     * VecVox for Berzerk Ultimate), on a channel of its own (its volume); when DIR/roms/... and DIR/system/
     * sp0256-al2.bin are there --- */
    if (argc > 2) {
        static const struct { const char *file; int want; const char *what; } SPEAK[] = {
            { "verzerk.vec", 1, "Verzerk (Voice): the VecVoice" },
            { "berzerk_ultimate.bin", 2, "Berzerk Ultimate: the VecVox" },
        };
        extern int vt_vecvoice;
        char rom[1100];
        snprintf(rom, sizeof rom, "%s/system/sp0256-al2.bin", dir);
        for (int t = 0; t < 2; t++) {
            char cart[1100], err[256];
            snprintf(cart, sizeof cart, "%s/roms/%s", dir, SPEAK[t].file);
            FILE *cf = fopen(cart, "rb"), *rf = fopen(rom, "rb");
            if (cf) fclose(cf);
            if (rf) fclose(rf);
            if (!cf || !rf || vx_load_speech_rom(rom) != 0 || vx_open(argv[2], cart, err, sizeof err) != 0) continue;
            vx_set_port2_device(VX_P2_ATARIVOX);
            vx_power_on(1);
            vx_pad p = { 0, 0, 0 }, q = { 0, 0, 0 };
            int two = 1, spoke = 0;
            for (int f = 0; f < 2400; f++) {
                p.buttons = t == 1 && f >= 1250 && f < 1260 ? 8u : 0u;    /* (Ultimate: a game started) */
                vx_set_input(&p, &q);
                vx_run_frame();
                int nf, ch;
                const int16_t *au = vx_audio(&nf, &ch);
                if (ch != 2) { two = 0; continue; }
                long e = 0;
                for (int i = 0; i < nf; i++) e += labs((long)au[2 * i + 1]);
                if (nf && e / nf > 200) spoke++;
            }
            int dev = vt_vecvoice;
            vx_close();
            vx_set_port2_device(VX_P2_JOYSTICK);
            char m[160];
            snprintf(m, sizeof m, "AtariVox: %s found (%d), speaks on a channel of its own (%d frames)", SPEAK[t].what, dev, spoke);
            CHECK(two && spoke > 20 && dev == SPEAK[t].want, m);
        }
    }

    printf("%s\n", failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}
