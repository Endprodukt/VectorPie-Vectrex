#include "render/colors.h"

#include <vectorpie/vp_paths.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex(const char *s, unsigned *v)
{
    char *e;
    unsigned long x = strtoul(s, &e, 16);
    if (e == s) return -1;
    *v = (unsigned)x;
    return (int)(e - s);
}

/* "AAAA-BBBB" (hex) or, for bright, "LO-HI" (decimal) */
static int range(const char *s, int decimal, uint16_t *lo, uint16_t *hi)
{
    char *e;
    unsigned long a = strtoul(s, &e, decimal ? 10 : 16);
    if (e == s) return -1;
    unsigned long b = a;
    if (*e == '-') {
        const char *t = e + 1;
        b = strtoul(t, &e, decimal ? 10 : 16);
        if (e == t) return -1;
    }
    if (*e && !isspace((unsigned char)*e)) return -1;
    *lo = (uint16_t)(a < b ? a : b);
    *hi = (uint16_t)(a < b ? b : a);
    return 0;
}

/* one rule: "name = CONDITIONS : RRGGBB" */
static int parse_rule(vx_rule *r, char *line)
{
    memset(r, 0, sizeof *r);
    char *eq = strchr(line, '='), *col = strrchr(line, ':');
    if (!eq || !col || col < eq) return -1;
    *eq = '\0';
    *col = '\0';
    char *name = line;
    while (isspace((unsigned char)*name)) name++;
    char *ne = name + strlen(name);
    while (ne > name && isspace((unsigned char)ne[-1])) *--ne = '\0';
    snprintf(r->name, sizeof r->name, "%s", name);
    unsigned rgb;
    const char *cs = col + 1;
    while (isspace((unsigned char)*cs)) cs++;
    if (*cs == '#') cs++;
    if (hex(cs, &rgb) != 6) return -1;
    r->r = (uint8_t)(rgb >> 16); r->g = (uint8_t)(rgb >> 8); r->b = (uint8_t)rgb;
    r->gain = 1.0f;
    cs += 6;                                    /* "*1.6": brighter (or dimmer) than the Vectrex draws it */
    while (isspace((unsigned char)*cs)) cs++;
    if (*cs == '*') {
        char *e;
        double g = strtod(cs + 1, &e);
        if (e == cs + 1 || g <= 0.0 || g > 8.0) return -1;
        r->gain = (float)g;
        cs = e;
        while (isspace((unsigned char)*cs)) cs++;
    }
    if (*cs) return -1;

    char *tok = strtok(eq + 1, " \t");
    while (tok) {
        if (r->ncond >= VX_RULE_CONDS) return -1;
        vx_cond *c = &r->cond[r->ncond];
        memset(c, 0, sizeof *c);
        if (!strcmp(tok, "dot")) c->kind = VX_C_DOT;
        else if (!strcmp(tok, "line")) c->kind = VX_C_LINE;
        else if (!strcmp(tok, "x") || !strcmp(tok, "pc") || !strcmp(tok, "u") || !strcmp(tok, "bright") ||
                 !strcmp(tok, "caller") || !strcmp(tok, "stroke") || !strcmp(tok, "part") || !strcmp(tok, "length")) {
            c->kind = !strcmp(tok, "x") ? VX_C_X : !strcmp(tok, "pc") ? VX_C_PC : !strcmp(tok, "u") ? VX_C_U
                    : !strcmp(tok, "caller") ? VX_C_CALLER : !strcmp(tok, "stroke") ? VX_C_STROKE : !strcmp(tok, "part") ? VX_C_PART
                    : !strcmp(tok, "length") ? VX_C_LENGTH : VX_C_BRIGHT;
            char *v = strtok(NULL, " \t");
            if (!v || range(v, c->kind == VX_C_BRIGHT || c->kind == VX_C_STROKE || c->kind == VX_C_PART || c->kind == VX_C_LENGTH, &c->lo, &c->hi)) return -1;
        } else if (!strcmp(tok, "area")) {
            /* X0-X1 Y0-Y1 (decimal) */
            char *v = strtok(NULL, " \t");
            c->kind = VX_C_AREA;
            if (!v || range(v, 1, &c->lo, &c->hi)) return -1;
            v = strtok(NULL, " \t");
            if (!v || range(v, 1, &c->lo2, &c->hi2)) return -1;
        } else if (!strcmp(tok, "ram")) {
            /* AAAA=VV, AAAA&MM=VV, AAAA!=VV */
            char *v = strtok(NULL, " \t");
            unsigned a, m = 0xff, x;
            int k;
            c->kind = VX_C_RAM;
            if (!v || (k = hex(v, &a)) <= 0) return -1;
            v += k;
            if (*v == '&') { if ((k = hex(v + 1, &m)) <= 0) return -1; v += 1 + k; }
            if (v[0] == '!' && v[1] == '=') { c->neg = 1; v += 2; }
            else if (v[0] == '=') v += 1;
            else return -1;
            if (hex(v, &x) <= 0) return -1;
            if (a < 0xC800 || a > 0xCBFF) return -1;    /* (RAM only: reading the VIA has side effects) */
            c->addr = (uint16_t)a; c->mask = (uint8_t)m; c->val = (uint8_t)(x & m);
        } else return -1;
        r->ncond++;
        tok = strtok(NULL, " \t");
    }
    r->ram_ok = 1;
    return 0;
}

int vx_colors_load_file(vx_colors *c, const char *path)
{
    memset(c, 0, sizeof *c);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    snprintf(c->file, sizeof c->file, "%s", path);
    char line[512];
    int no = 0;
    while (fgets(line, sizeof line, f)) {
        no++;
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        char *cm = strchr(p, ';');
        if (cm) *cm = '\0';
        if (!*p || *p == '#') continue;
        char *e = p + strlen(p);
        while (e > p && isspace((unsigned char)e[-1])) *--e = '\0';
        if (!*p) continue;
        if (!strncmp(p, "crc", 3) && (isspace((unsigned char)p[3]) || p[3] == '=')) {   /* the cartridges it is for */
            const char *q = strchr(p, '=');
            while (q && *++q && c->ncrc < VX_COLOR_CRCS) {
                while (isspace((unsigned char)*q) || *q == ',') q++;
                if (!isxdigit((unsigned char)*q)) break;
                c->crc[c->ncrc++] = (uint32_t)strtoul(q, (char **)&q, 16);
                q--;
            }
            continue;
        }
        if (c->n >= VX_COLOR_RULES) { fprintf(stderr, "%s: more than %d rules\n", path, VX_COLOR_RULES); break; }
        if (parse_rule(&c->rule[c->n], p) == 0) c->n++;
        else fprintf(stderr, "%s:%d: rule not understood\n", path, no);
    }
    fclose(f);
    return 0;
}

int vx_colors_load(vx_colors *c, const char *id)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/colors/%s.ini", vp_save_dir(), id);
    if (vx_colors_load_file(c, path) == 0) return 0;
    char rel[200];
    snprintf(rel, sizeof rel, "colors/%s.ini", id);
    vp_asset_path(rel, path, sizeof path);
    if (vx_colors_load_file(c, path) == 0) return 0;
    memset(c, 0, sizeof *c);
    return -1;
}

uint32_t vx_cart_crc(const char *path)
{
    if (!path || !*path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static unsigned char buf[4 << 20];          /* (a cartridge: 4 MiB at most) */
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    while (n > 0 && (buf[n - 1] == 0xFF || buf[n - 1] == 0x00)) n--;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= buf[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return n ? ~c : 0;
}

static uint32_t g_game_crc;
void vx_colors_game(const char *cart_path) { g_game_crc = vx_cart_crc(cart_path); }
uint32_t vx_colors_game_crc(void) { return g_game_crc; }

/* the checksums a profile file lists (its "crc =" line), without reading its rules */
static int file_crcs(const char *path, uint32_t *crc, int max)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (strncmp(p, "crc", 3) || !(isspace((unsigned char)p[3]) || p[3] == '=')) continue;
        const char *q = strchr(p, '=');
        while (q && *++q && n < max) {
            while (isspace((unsigned char)*q) || *q == ',') q++;
            if (!isxdigit((unsigned char)*q)) break;
            crc[n++] = (uint32_t)strtoul(q, (char **)&q, 16);
            q--;
        }
        break;
    }
    fclose(f);
    return n;
}

int vx_colors_file_fits(const char *path)
{
    uint32_t crc[VX_COLOR_CRCS];
    int n = file_crcs(path, crc, VX_COLOR_CRCS);
    if (n <= 0 || !g_game_crc) return 1;        /* (no checksums in it, or none known for the game) */
    for (int i = 0; i < n; i++) if (crc[i] == g_game_crc) return 1;
    return 0;
}

/* The name the game's own profiles are kept under: its id, unless it has none of its own there and
 * an own profile elsewhere lists its checksum (the cartridge renamed): that one's name, so the game
 * finds its own profiles again and saves new ones beside them. (Kept per game: the menu asks every
 * frame.) */
static const char *own_id(const char *id)
{
    static char res[96], last[96];
    static uint32_t last_crc;
    static int have;
    if (have && last_crc == g_game_crc && !strcmp(last, id)) return res;
    have = 1; last_crc = g_game_crc;
    snprintf(last, sizeof last, "%s", id);
    snprintf(res, sizeof res, "%s", id);
    if (!g_game_crc) return res;
    char p[1500];
    for (int k = 1; k <= VX_PROFILES_OWN; k++) {           /* (own ones of the name: they stay) */
        if (k == 1) snprintf(p, sizeof p, "%s/colors/%s.ini", vp_save_dir(), id);
        else snprintf(p, sizeof p, "%s/colors/%s.%d.ini", vp_save_dir(), id, k);
        FILE *t = fopen(p, "r");
        if (t) { fclose(t); return res; }
    }
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/colors", vp_save_dir());
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        size_t L = strlen(e->d_name);
        if (L < 5 || L >= sizeof res + 4 || strcmp(e->d_name + L - 4, ".ini")) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        uint32_t crc[VX_COLOR_CRCS];
        int m = file_crcs(p, crc, VX_COLOR_CRCS), hit = 0;
        for (int i = 0; i < m; i++) hit |= crc[i] == g_game_crc;
        if (!hit) continue;
        char base[96];
        snprintf(base, sizeof base, "%.*s", (int)(L - 4), e->d_name);    /* "berzerk.2" -> "berzerk" */
        char *dot = strrchr(base, '.');
        if (dot && dot[1] && strspn(dot + 1, "0123456789") == strlen(dot + 1)) *dot = '\0';
        snprintf(res, sizeof res, "%s", base);
        break;
    }
    if (d) closedir(d);
    return res;
}

int vx_colors_profile_path(const char *id, int k, char *out, size_t size)
{
    if (k >= 1) id = own_id(id);                /* (own profiles: by the checksum too) */
    if (k <= 0) {
        char rel[200];
        snprintf(rel, sizeof rel, "colors/%s.ini", id);
        vp_asset_path(rel, out, size);
        if (g_game_crc) {                       /* by the checksum: the one of the name if it fits, else
                                                   any listing it (another name, a related game) */
            FILE *t = fopen(out, "r");
            int named = t != NULL;
            if (t) fclose(t);
            uint32_t crc[VX_COLOR_CRCS];
            int nc = named ? file_crcs(out, crc, VX_COLOR_CRCS) : 0, fits = !named ? 0 : nc <= 0;
            for (int i = 0; i < nc; i++) fits |= crc[i] == g_game_crc;
            if (!fits || nc <= 0) {
                char dir[1100];
                vp_asset_path("colors", dir, sizeof dir);
                DIR *d = opendir(dir);
                struct dirent *e;
                while (d && (e = readdir(d)) != NULL) {
                    size_t L = strlen(e->d_name);
                    if (L < 5 || strcmp(e->d_name + L - 4, ".ini")) continue;
                    char p[1400];
                    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
                    int m = file_crcs(p, crc, VX_COLOR_CRCS), hit = 0;
                    for (int i = 0; i < m; i++) hit |= crc[i] == g_game_crc;
                    if (hit) { snprintf(out, size, "%s", p); closedir(d); return 0; }
                }
                if (d) closedir(d);
            }
            if (named) return 0;
            return -1;
        }
    } else if (k == 1) snprintf(out, size, "%s/colors/%s.ini", vp_save_dir(), id);
    else snprintf(out, size, "%s/colors/%s.%d.ini", vp_save_dir(), id, k);
    FILE *f = fopen(out, "r");
    if (!f) return -1;
    fclose(f);
    return 0;
}

int vx_colors_own_count(const char *id)
{
    char p[1100];
    int n = 0;
    for (int k = 1; k <= VX_PROFILES_OWN; k++)
        if (vx_colors_profile_path(id, k, p, sizeof p) == 0) n = k;
    return n;
}

static int setting_there(const char *id, int s)
{
    char p[1100];
    return s == 0 || vx_colors_profile_path(id, s - 1, p, sizeof p) == 0;
}

int vx_colors_setting_used(const char *id, int s)
{
    if (s < 0) s = 0;
    if (setting_there(id, s)) return s;
    if (s == 1) return setting_there(id, 2) ? 2 : 0;     /* (no ORIGINAL: the own one) */
    return vx_colors_setting_used(id, 1);                /* (an own profile gone: as ORIGINAL) */
}

int vx_colors_setting_step(const char *id, int s, int d)
{
    int top = vx_colors_own_count(id) + 1;               /* the last setting there can be */
    s = vx_colors_setting_used(id, s);
    for (int i = 0; i <= top; i++) {
        s = (s + d + top + 1) % (top + 1);
        if (setting_there(id, s)) return s;
    }
    return 0;
}

int vx_colors_load_setting(vx_colors *c, const char *id, int s)
{
    char p[1100];
    memset(c, 0, sizeof *c);
    s = vx_colors_setting_used(id, s);
    if (s == 0 || vx_colors_profile_path(id, s - 1, p, sizeof p) != 0) return -1;
    return vx_colors_load_file(c, p);
}

void vx_colors_frame(vx_colors *c)
{
    for (int i = 0; i < c->n; i++) {
        vx_rule *r = &c->rule[i];
        r->ram_ok = 1;
        for (int k = 0; k < r->ncond && r->ram_ok; k++) {
            const vx_cond *q = &r->cond[k];
            if (q->kind != VX_C_RAM) continue;
            int is = (vx_peek_ram(q->addr) & q->mask) == q->val;
            if (is == q->neg) r->ram_ok = 0;
        }
    }
}

int vx_colors_line(const vx_colors *c, const vx_line *l, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const vx_rule *u = vx_colors_find(c, l);
    if (!u) return 0;
    *r = u->r; *g = u->g; *b = u->b;
    return 1;
}

int vx_rule_matches(const vx_rule *u, const vx_line *l)
{
    /* a dot: the beam barely moves while it is on (a BIOS dot drifts a little: no exact 0 length) */
    long dx = l->x1 - l->x0, dy = l->y1 - l->y0;
    int dot = dx * dx + dy * dy <= VX_DOT_LEN * VX_DOT_LEN;
    {
        int ok = 1;
        for (int k = 0; k < u->ncond && ok; k++) {
            const vx_cond *q = &u->cond[k];
            switch (q->kind) {
            case VX_C_X:      ok = l->tag.x >= q->lo && l->tag.x <= q->hi; break;
            case VX_C_PC:     ok = l->tag.pc >= q->lo && l->tag.pc <= q->hi; break;
            case VX_C_U:      ok = l->tag.u >= q->lo && l->tag.u <= q->hi; break;
            case VX_C_DOT:    ok = dot; break;
            case VX_C_LINE:   ok = !dot; break;
            case VX_C_BRIGHT: ok = l->z >= q->lo && l->z <= q->hi; break;
            case VX_C_CALLER: ok = l->tag.caller >= q->lo && l->tag.caller <= q->hi; break;
            case VX_C_STROKE: ok = l->stroke >= q->lo && l->stroke <= q->hi; break;
            case VX_C_PART:   ok = l->part >= q->lo && l->part <= q->hi; break;
            case VX_C_LENGTH: { long long d2 = (long long)dx * dx + (long long)dy * dy;   /* (the line's length, canvas units) */
                              ok = d2 >= (long long)q->lo * q->lo && d2 <= (long long)q->hi * q->hi; } break;
            case VX_C_AREA:   ok = l->x0 >= q->lo && l->x0 <= q->hi && l->x1 >= q->lo && l->x1 <= q->hi &&
                                   l->y0 >= q->lo2 && l->y0 <= q->hi2 && l->y1 >= q->lo2 && l->y1 <= q->hi2; break;
            default:          break;      /* (ram: per frame, vx_colors_frame) */
            }
        }
        return ok;
    }
}

const vx_rule *vx_colors_find(const vx_colors *c, const vx_line *l)
{
    for (int i = 0; i < c->n; i++)
        if (c->rule[i].ram_ok && vx_rule_matches(&c->rule[i], l)) return &c->rule[i];
    return NULL;
}

/* ---------- changing a profile (the colour editor, platform/coloredit.h) ---------- */

static int same_conds(const vx_rule *a, const vx_rule *b)
{
    if (a->ncond != b->ncond) return 0;
    for (int i = 0; i < a->ncond; i++) {
        const vx_cond *p = &a->cond[i], *q = &b->cond[i];
        if (p->kind != q->kind || p->lo != q->lo || p->hi != q->hi || p->addr != q->addr ||
            p->mask != q->mask || p->val != q->val || p->neg != q->neg || p->lo2 != q->lo2 || p->hi2 != q->hi2) return 0;
    }
    return 1;
}

int vx_colors_put(vx_colors *c, const vx_rule *r)
{
    for (int i = 0; i < c->n; i++)              /* the same conditions: its colour replaced, moved to the top */
        if (same_conds(&c->rule[i], r)) {
            memmove(&c->rule[1], &c->rule[0], sizeof c->rule[0] * (size_t)i);
            c->rule[0] = *r;
            return 0;
        }
    if (c->n >= VX_COLOR_RULES) return -1;
    memmove(&c->rule[1], &c->rule[0], sizeof c->rule[0] * (size_t)c->n);
    c->rule[0] = *r;
    c->n++;
    return 0;
}

int vx_colors_remove_matching(vx_colors *c, const vx_line *l)
{
    int removed = 0;
    for (;;) {
        const vx_rule *u = vx_colors_find(c, l);
        if (!u) break;
        int i = (int)(u - c->rule);
        memmove(&c->rule[i], &c->rule[i + 1], sizeof c->rule[0] * (size_t)(c->n - i - 1));
        c->n--;
        removed++;
    }
    return removed;
}

static void cond_text(const vx_cond *q, char *out, size_t size)
{
    switch (q->kind) {
    case VX_C_X:      snprintf(out, size, "x %04X-%04X", q->lo, q->hi); break;
    case VX_C_PC:     snprintf(out, size, "pc %04X-%04X", q->lo, q->hi); break;
    case VX_C_U:      snprintf(out, size, "u %04X-%04X", q->lo, q->hi); break;
    case VX_C_DOT:    snprintf(out, size, "dot"); break;
    case VX_C_LINE:   snprintf(out, size, "line"); break;
    case VX_C_BRIGHT: snprintf(out, size, "bright %u-%u", q->lo, q->hi); break;
    case VX_C_CALLER: snprintf(out, size, "caller %04X-%04X", q->lo, q->hi); break;
    case VX_C_STROKE: snprintf(out, size, "stroke %u-%u", q->lo, q->hi); break;
    case VX_C_PART:   snprintf(out, size, "part %u-%u", q->lo, q->hi); break;
    case VX_C_LENGTH: snprintf(out, size, "length %u-%u", q->lo, q->hi); break;
    case VX_C_AREA:   snprintf(out, size, "area %u-%u %u-%u", q->lo, q->hi, q->lo2, q->hi2); break;
    case VX_C_RAM:
        if (q->mask != 0xff) snprintf(out, size, "ram %04X&%02X%s%02X", q->addr, q->mask, q->neg ? "!=" : "=", q->val);
        else snprintf(out, size, "ram %04X%s%02X", q->addr, q->neg ? "!=" : "=", q->val);
        break;
    default:          out[0] = '\0'; break;
    }
}

int vx_colors_save(const vx_colors *c, const char *path, const char *title)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "; %s: colour profile (src/render/colors.h), written by the colour editor.\n"
               "; name = CONDITIONS : RRGGBB [*GAIN], the first rule that matches a line gives its colour.\n\n",
            title && *title ? title : "a game");
    {   /* the cartridges it is made for: its own, and the game's being coloured */
        uint32_t crc[VX_COLOR_CRCS + 1];
        int n = 0;
        for (int i = 0; i < c->ncrc; i++) crc[n++] = c->crc[i];
        uint32_t g = vx_colors_game_crc();
        int have = 0;
        for (int i = 0; i < n; i++) have |= crc[i] == g;
        if (g && !have && n < VX_COLOR_CRCS) crc[n++] = g;
        if (n) {
            fprintf(f, "; the cartridges it is made for (CRC32 without the padding at the end):\ncrc =");
            for (int i = 0; i < n; i++) fprintf(f, " %08X", (unsigned)crc[i]);
            fprintf(f, "\n\n");
        }
    }
    for (int i = 0; i < c->n; i++) {
        const vx_rule *r = &c->rule[i];
        fprintf(f, "%-10s =", r->name[0] ? r->name : "rule");
        for (int k = 0; k < r->ncond; k++) {
            char t[64];
            cond_text(&r->cond[k], t, sizeof t);
            fprintf(f, " %s", t);
        }
        fprintf(f, " : %02X%02X%02X", r->r, r->g, r->b);
        if (r->gain < 0.999f || r->gain > 1.001f) fprintf(f, " *%.2f", r->gain);
        fprintf(f, "\n");
    }
    fclose(f);
    return 0;
}
