#include "platform/roms.h"

#include "platform/config.h"

#include <vectorpie/vp_paths.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define vx_mkdir(p) _mkdir(p)
#else
#define vx_mkdir(p) mkdir(p, 0755)
#endif

static const char *const EXT[] = { ".bin", ".vec", ".gam", ".rom", ".um2", ".pit" };
#define NEXT (int)(sizeof EXT / sizeof EXT[0])

static int ext_index(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return -1;
    for (int i = 0; i < NEXT; i++) {
        const char *a = dot, *b = EXT[i];
        while (*a && *b && tolower((unsigned char)*a) == *b) a++, b++;
        if (!*a && !*b) return i;
    }
    return -1;
}

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static int is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

static int dirs(const char *rom_dir, char out[4][VX_ROM_PATH])
{
    int n = 0;
    if (rom_dir && *rom_dir) snprintf(out[n++], VX_ROM_PATH, "%s", rom_dir);
    if (is_dir("/persistent/games")) {
        vx_mkdir("/persistent/games/rom");
        vx_mkdir("/persistent/games/rom/vectrex");
        snprintf(out[n++], VX_ROM_PATH, "/persistent/games/rom/vectrex");
    }
    snprintf(out[n++], VX_ROM_PATH, "/usr/local/share/advance/image/vectrex");
    vp_asset_path("roms", out[n++], VX_ROM_PATH);
    return n;
}

void vx_rom_title(const unsigned char *c, long n, char *out, int size)
{
    out[0] = '\0';
    /* the header: "g GCE 1982", $80, the music's address (2), then each title line: height, width,
     * y, x (4), its text, $80; a 0 ends it (the BIOS's Print_Str_d lines) */
    if (n < 16 || memcmp(c, "g GCE", 5) != 0) return;
    long i = 5;
    while (i < n && i < 32 && c[i] != 0x80) i++;
    i += 3;                                     /* the $80, the music */
    /* the first title line, and the next ones while the name stays short ("LAST" "HUMAN"): homebrew
     * carts often put messages in the further lines */
    int o = 0;
    for (int line = 0; line < 4 && i < n && c[i] != 0; line++) {
        i += 4;
        char t[64];
        int k = 0;
        for (; i < n && c[i] != 0x80 && c[i] != 0; i++) {
            unsigned char ch = c[i];
            if (ch >= 0x20 && ch < 0x7f && k < (int)sizeof t - 1) t[k++] = (char)toupper(ch);
        }
        t[k] = '\0';
        if (i < n && c[i] == 0x80) i++;
        if (line > 0 && (strstr(t, "...") || o + 1 + k > 20)) break;
        if (o > 0 && o < size - 1) out[o++] = ' ';
        for (int j = 0; j < k && o < size - 1; j++) out[o++] = t[j];
    }
    out[o] = '\0';
    /* tidy: no double spaces, none at the ends; nothing printable: none */
    char *r = out, *w = out;
    int sp = 1;
    for (; *r; r++) {
        if (*r == ' ') { if (!sp) *w++ = ' '; sp = 1; }
        else { *w++ = *r; sp = 0; }
    }
    while (w > out && w[-1] == ' ') w--;
    *w = '\0';
    /* not a name: a message ("IT WAS WORTH THE WAIT ...") or too long for one */
    if (strstr(out, "...") || strlen(out) > 28) out[0] = '\0';
}

/* names.ini: "id = Name" lines, the package's then the owner's (save/names.ini) */
typedef struct { char id[64]; char name[VX_ROM_NAME]; } name_entry;
static name_entry *g_names;
static int g_nnames, g_capnames;

static void load_names(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (!*p || *p == ';' || *p == '#') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *v = eq + 1;
        while (isspace((unsigned char)*v)) v++;
        char *e = v + strlen(v);
        while (e > v && isspace((unsigned char)e[-1])) *--e = '\0';
        char id[64];
        vx_game_id(p, id, sizeof id);
        int k = 0;
        while (k < g_nnames && strcmp(g_names[k].id, id)) k++;
        if (k == g_nnames) {
            if (g_nnames == g_capnames) {
                int cap = g_capnames ? g_capnames * 2 : 128;
                name_entry *r = realloc(g_names, sizeof *r * cap);
                if (!r) break;
                g_names = r; g_capnames = cap;
            }
            g_nnames++;
        }
        snprintf(g_names[k].id, sizeof g_names[k].id, "%s", id);
        int o = 0;
        for (; *v && o < VX_ROM_NAME - 1; v++) g_names[k].name[o++] = (char)toupper((unsigned char)*v);
        g_names[k].name[o] = '\0';
    }
    fclose(f);
}

static const char *known_name(const char *id)
{
    for (int k = 0; k < g_nnames; k++)
        if (!strcmp(g_names[k].id, id)) return g_names[k].name;
    return NULL;
}

static void name_from_file(const char *file, char *out, int size)
{
    const char *dot = strrchr(file, '.');
    int o = 0;
    for (const char *p = file; *p && p != dot && o < size - 1; p++)
        out[o++] = (*p == '_' || *p == '-') ? ' ' : (char)toupper((unsigned char)*p);
    out[o] = '\0';
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(((const vx_rom *)a)->name, ((const vx_rom *)b)->name);
}

int vx_roms_scan(vx_rom_list *l, const char *rom_dir)
{
    memset(l, 0, sizeof *l);
    l->ndirs = dirs(rom_dir, l->dirs);
    {
        char p[VX_ROM_PATH];
        g_nnames = 0;
        load_names(vp_asset_path("names.ini", p, sizeof p));
        snprintf(p, sizeof p, "%s/names.ini", vp_save_dir());
        load_names(p);
    }
    unsigned char *buf = malloc(65536);
    if (!buf) return -1;
    for (int d = 0; d < l->ndirs; d++) {
        DIR *dir = opendir(l->dirs[d]);
        if (!dir) continue;
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            int x = ext_index(e->d_name);
            if (x < 0) continue;
            char path[VX_ROM_PATH];
            snprintf(path, sizeof path, "%s/%s", l->dirs[d], e->d_name);
            if (!is_file(path)) continue;
            char id[64];
            vx_game_id(e->d_name, id, sizeof id);
            int dup = 0;
            for (int k = 0; k < l->n && !dup; k++) dup = !strcmp(l->rom[k].id, id);
            if (dup) continue;
            if (l->n == l->cap) {
                int cap = l->cap ? l->cap * 2 : 64;
                vx_rom *r = realloc(l->rom, sizeof *r * cap);
                if (!r) { closedir(dir); free(buf); return -1; }
                l->rom = r; l->cap = cap;
            }
            vx_rom *r = &l->rom[l->n++];
            memset(r, 0, sizeof *r);
            snprintf(r->path, sizeof r->path, "%s", path);
            snprintf(r->id, sizeof r->id, "%s", id);
            r->uvmc2 = !strcmp(EXT[x], ".um2");
            const char *known = known_name(id);
            if (known) snprintf(r->name, sizeof r->name, "%s", known);
            else if (!strcmp(EXT[x], ".bin") || !strcmp(EXT[x], ".vec") || !strcmp(EXT[x], ".gam") || !strcmp(EXT[x], ".rom")) {
                FILE *f = fopen(path, "rb");
                if (f) {
                    long n = (long)fread(buf, 1, 65536, f);
                    fclose(f);
                    vx_rom_title(buf, n, r->name, sizeof r->name);
                }
            }
            if (!r->name[0]) name_from_file(e->d_name, r->name, sizeof r->name);
        }
        closedir(dir);
    }
    free(buf);
    /* the same name twice (a hack, a second version): the file's name tells them apart */
    char *twice = calloc(l->n ? l->n : 1, 1);          /* (all found first: renaming one hides its twin) */
    if (!twice) return -1;
    for (int a = 0; a < l->n; a++)
        for (int b = 0; b < l->n && !twice[a]; b++) twice[a] = b != a && !strcmp(l->rom[a].name, l->rom[b].name);
    for (int a = 0; a < l->n; a++) {
        if (!twice[a]) continue;
        const char *f = l->rom[a].path, *s = f;
        for (const char *p = f; *p; p++) if (*p == '/' || *p == '\\') s = p + 1;
        char fn[32], base[VX_ROM_NAME];
        name_from_file(s, fn, sizeof fn);
        memcpy(base, l->rom[a].name, sizeof base);
        snprintf(l->rom[a].name, sizeof l->rom[a].name, "%.60s (%.31s)", base, fn);
    }
    free(twice);
    if (l->n > 1) qsort(l->rom, l->n, sizeof l->rom[0], cmp_name);
    return 0;
}

void vx_roms_free(vx_rom_list *l)
{
    free(l->rom);
    memset(l, 0, sizeof *l);
}

int vx_rom_resolve(const char *arg, const char *rom_dir, char *out, int size)
{
    if (is_file(arg)) { snprintf(out, size, "%s", arg); return 0; }
    char want[64];
    vx_game_id(arg, want, sizeof want);         /* "ARMOR", "armor.bin", "Armor Attack.BIN" alike */
    char d[4][VX_ROM_PATH];
    int nd = dirs(rom_dir, d);
    for (int k = 0; k < nd; k++) {
        DIR *dir = opendir(d[k]);
        if (!dir) continue;
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (ext_index(e->d_name) < 0) continue;
            char id[64];
            vx_game_id(e->d_name, id, sizeof id);
            if (strcmp(id, want)) continue;
            snprintf(out, size, "%s/%s", d[k], e->d_name);
            closedir(dir);
            return 0;
        }
        closedir(dir);
    }
    return -1;
}
