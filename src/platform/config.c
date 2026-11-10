#include "platform/config.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vx_config_defaults(vx_config *c)
{
    memset(c, 0, sizeof *c);
    c->monitor = VX_MONITOR_HORIZONTAL;
    c->size = 1.0f;
    c->size_x = c->size_y = 1.0f;
    c->brightness = 1.0f;
    c->gamma = 0.8f;
    c->color_r = c->color_g = c->color_b = 255;
    c->board_hz = 50.0f;        /* the Vectrex's own frame rate */
    c->skip_bios = 0;
    c->uvmc2_fast = 1;          /* exact is too slow for UVMC2 games on a Pi 5 (STATUS.md) */
    c->dig_attack = 0.12f;
    c->dig_release = 0.08f;
    c->dig_curve = 1.0f;
    c->dig_max = 1.0f;
    c->deadzone = 0.15f;        /* the SDK's own */
    c->volume = 1.0f;
    c->speech_volume = 1.0f;
    c->color_profile = 1;
    c->color_mode = VX_COLOR_ONE;
    c->band1 = 0x2050FF; c->band2 = 0x00C8FF; c->band3 = 0xA0F0FF; c->band4 = 0xFFFFFF;   /* ("ICE") */
    c->mains_hum = 0;
    c->port2 = 0;               /* a joystick */
    c->spin_sens = 0.25f;       /* (a 1200 counts a turn spinner: 300 steps a turn) */
    c->spin_steps = 4;          /* (the spinner example reads it four times a frame) */
    c->tb_sens = 0.5f;
    c->tb_steps = 20;           /* (Fortress of Narzod's trackball patch reads it 20-27 times a frame) */
}

static const char *const PORT2_NAME[] = { "joystick", "spinner", "trackball", "atarivox", "vecvox", "vecvoice" };

const char *vx_port2_name(int d)
{
    return d >= 0 && d < (int)(sizeof PORT2_NAME / sizeof PORT2_NAME[0]) ? PORT2_NAME[d] : "joystick";
}

/* Every setting: its key, and where it is (a float or an int). */
typedef struct { const char *key; size_t off; int is_float; } field;
#define F(k) { #k, offsetof(vx_config, k), 1 }
#define I(k) { #k, offsetof(vx_config, k), 0 }
static const field FIELDS[] = {
    I(monitor), F(size), F(size_x), F(size_y), I(pos_x), I(pos_y), I(flip_x), I(flip_y),
    F(brightness), F(gamma), I(color_r), I(color_g), I(color_b), F(board_hz),
    I(skip_bios), I(uvmc2_fast),
    F(dig_attack), F(dig_release), F(dig_curve), F(dig_max), F(deadzone),
    F(volume), F(speech_volume),
    I(color_profile), I(mains_hum), I(color_mode), I(band1), I(band2), I(band3), I(band4),
    I(port2), F(spin_sens), I(spin_reverse), I(spin_steps), F(tb_sens), I(tb_reverse_x), I(tb_reverse_y), I(tb_steps),
};
#define NFIELDS (int)(sizeof FIELDS / sizeof FIELDS[0])

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static int set(vx_config *c, const char *key, const char *val)
{
    if (!strcmp(key, "monitor")) {                  /* also by name */
        if (!strcmp(val, "horizontal")) { c->monitor = VX_MONITOR_HORIZONTAL; return 0; }
        if (!strcmp(val, "vertical")) { c->monitor = VX_MONITOR_VERTICAL; return 0; }
    }
    if (!strcmp(key, "rom_dir")) { snprintf(c->rom_dir, sizeof c->rom_dir, "%s", val); return 0; }
    if (!strcmp(key, "port2") && !isdigit((unsigned char)val[0])) {      /* also by name */
        for (int d = 0; d < (int)(sizeof PORT2_NAME / sizeof PORT2_NAME[0]); d++)
            if (!strcmp(val, PORT2_NAME[d])) { c->port2 = d; return 0; }
        return -1;
    }
    for (int i = 0; i < NFIELDS; i++) {
        if (strcmp(key, FIELDS[i].key)) continue;
        char *p = (char *)c + FIELDS[i].off;
        if (FIELDS[i].is_float) *(float *)p = (float)atof(val);
        else if (!strncmp(key, "band", 4)) *(int *)p = (int)strtol(val[0] == '#' ? val + 1 : val, NULL, 16);   /* (RRGGBB) */
        else *(int *)p = atoi(val);
        return 0;
    }
    return -1;
}

int vx_config_load(vx_config *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *p = trim(line);
        if (!*p || *p == ';' || *p == '#') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(p), *val = trim(eq + 1);
        char *cm = strpbrk(val, ";#");
        if (cm) { *cm = '\0'; val = trim(val); }
        if (set(c, key, val) != 0) fprintf(stderr, "%s: setting not understood: %s = %s\n", path, key, val);
    }
    fclose(f);
    return 0;
}

static void put(FILE *f, const vx_config *c, int i)
{
    const char *p = (const char *)c + FIELDS[i].off;
    if (FIELDS[i].is_float) fprintf(f, "%s = %.3f\n", FIELDS[i].key, *(const float *)p);
    else if (!strncmp(FIELDS[i].key, "band", 4)) fprintf(f, "%s = %06X\n", FIELDS[i].key, *(const int *)p & 0xffffff);
    else fprintf(f, "%s = %d\n", FIELDS[i].key, *(const int *)p);
}

static int differs(const vx_config *a, const vx_config *b, int i)
{
    const char *p = (const char *)a + FIELDS[i].off, *q = (const char *)b + FIELDS[i].off;
    if (FIELDS[i].is_float) {
        float d = *(const float *)p - *(const float *)q;
        return d > 0.0005f || d < -0.0005f;
    }
    return *(const int *)p != *(const int *)q;
}

int vx_band_of(int z) { return z < 48 ? 0 : z < 81 ? 1 : z < 112 ? 2 : 3; }

int vx_band_color(const vx_config *c, int band)
{
    return band <= 0 ? c->band1 : band == 1 ? c->band2 : band == 2 ? c->band3 : c->band4;
}

int vx_config_nfields(void) { return NFIELDS; }

int vx_config_field(const char *key)
{
    for (int i = 0; i < NFIELDS; i++)
        if (!strcmp(FIELDS[i].key, key)) return i;
    return -1;
}

int vx_config_differs(const vx_config *a, const vx_config *b, int i)
{
    return i >= 0 && i < NFIELDS && differs(a, b, i);
}

void vx_config_copy_field(vx_config *dst, const vx_config *src, int i)
{
    if (i < 0 || i >= NFIELDS) return;
    size_t sz = FIELDS[i].is_float ? sizeof(float) : sizeof(int);
    memcpy((char *)dst + FIELDS[i].off, (const char *)src + FIELDS[i].off, sz);
}

void vx_config_take_changed(vx_config *dst, const vx_config *before, const vx_config *after)
{
    for (int i = 0; i < NFIELDS; i++) {
        if (!differs(before, after, i)) continue;
        size_t sz = FIELDS[i].is_float ? sizeof(float) : sizeof(int);
        memcpy((char *)dst + FIELDS[i].off, (const char *)after + FIELDS[i].off, sz);
    }
}

int vx_config_save(const vx_config *c, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "; Vectrex for VectorPie: the owner's settings for every game (the options menu writes them)\n");
    for (int i = 0; i < NFIELDS; i++) put(f, c, i);
    fprintf(f, "rom_dir = %s\n", c->rom_dir);     /* (every game's only: not in a game's own file) */
    fclose(f);
    return 0;
}

int vx_config_save_diff(const vx_config *c, const vx_config *base, const char *path)
{
    int any = 0;
    for (int i = 0; i < NFIELDS; i++) any |= differs(c, base, i);
    if (!any) { remove(path); return 0; }
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "; Vectrex for VectorPie: this game's own settings (only what differs from save/vectrex.ini)\n");
    for (int i = 0; i < NFIELDS; i++)
        if (differs(c, base, i)) put(f, c, i);
    fclose(f);
    return 0;
}

void vx_game_id(const char *cart_path, char *id, int size)
{
    const char *name = cart_path && *cart_path ? cart_path : "minestorm";
    const char *b = name;
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    const char *dot = strrchr(b, '.');
    int n = 0;
    for (const char *p = b; *p && p != dot && n < size - 1; p++) {
        unsigned char ch = (unsigned char)tolower((unsigned char)*p);
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) id[n++] = (char)ch;
        else if (n > 0 && id[n - 1] != '_') id[n++] = '_';
    }
    while (n > 0 && id[n - 1] == '_') n--;
    if (n == 0) { snprintf(id, size, "game"); return; }
    id[n] = '\0';
}
