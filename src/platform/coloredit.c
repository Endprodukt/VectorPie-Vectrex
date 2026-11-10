/* coloredit.c - see coloredit.h. */
#include "platform/coloredit.h"

#include "render/draw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; int r, g, b; } pal_entry;
static const pal_entry PALETTE[] = { VX_PALETTE_ENTRIES };
#define NPAL (int)(sizeof PALETTE / sizeof PALETTE[0])

int vx_ce_palette_count(void) { return NPAL; }

const char *vx_ce_palette(int i, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (i < 0 || i >= NPAL) return "NONE";
    *r = (uint8_t)PALETTE[i].r; *g = (uint8_t)PALETTE[i].g; *b = (uint8_t)PALETTE[i].b;
    return PALETTE[i].name;
}

/* ---------- the pieces of a picture ---------- */

static int is_dot(const vx_line *l)
{
    long dx = l->x1 - l->x0, dy = l->y1 - l->y0;
    return dx * dx + dy * dy <= VX_DOT_LEN * VX_DOT_LEN;
}

int vx_ce_pieces(const vx_line *l, int n, vx_piece *out, int max)
{
    int np = 0;
    vx_piece *p = NULL;
    double sx = 0, sy = 0;
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        if (l[i].z == 0) continue;
        uint16_t pc = l[i].tag.pc, x = l[i].tag.x;
        uint16_t lo = (uint16_t)(pc & ~0x1F);
        /* a new piece where the core's numbering says (vx_line piece: another routine, list, object or
         * caller) */
        int fresh = !p || l[i].piece != l[p->last].piece;
        if (fresh) {
            if (p && cnt) { p->cx = (float)(sx / cnt); p->cy = (float)(sy / cnt); }
            if (np >= max) break;
            p = &out[np++];
            memset(p, 0, sizeof *p);
            p->first = p->last = i;
            p->pc_lo = lo; p->pc_hi = (uint16_t)(pc | 0x1F);
            p->x_lo = p->x_hi = x;
            p->u = l[i].tag.u;
            p->caller = l[i].tag.caller;
            p->dots = 1;
            sx = sy = 0; cnt = 0;
        }
        p->last = i;
        if (lo < p->pc_lo) p->pc_lo = lo;                  /* (a piece may use several routines: move, draw) */
        if ((pc | 0x1F) > p->pc_hi) p->pc_hi = (uint16_t)(pc | 0x1F);
        if (l[i].part + 1 > p->parts) p->parts = l[i].part + 1;
        if (x < p->x_lo) p->x_lo = x;
        if (x > p->x_hi) p->x_hi = x;
        if (!is_dot(&l[i])) p->dots = 0;
        sx += (l[i].x0 + l[i].x1) * 0.5; sy += (l[i].y0 + l[i].y1) * 0.5; cnt++;
    }
    if (p && cnt) { p->cx = (float)(sx / cnt); p->cy = (float)(sy / cnt); }
    return np;
}

/* the vector list of a shape: the selected piece's list and every list of its routine overlapping it
 * (the other mines of its kind drawn from the same list), a few bytes either side */
static void shape_range(const vx_piece *p, int np, int sel, uint16_t *lo, uint16_t *hi)
{
    int a = p[sel].x_lo, b = p[sel].x_hi;
    /* a list in RAM is a game's own buffer (Hyperchase turns its objects into lists there, one after
     * the other): the lists next to it are other objects, not the same shape - only its own range */
    int in_ram = a >= 0xC800 && b <= 0xCBFF;
    for (int grew = !in_ram; grew;) {
        grew = 0;
        for (int i = 0; i < np; i++) {
            if (p[i].pc_hi < p[sel].pc_lo || p[i].pc_lo > p[sel].pc_hi) continue;   /* (its routines) */
            if (p[i].x_hi + 4 < a || p[i].x_lo > b + 4) continue;
            if (p[i].x_lo < a) { a = p[i].x_lo; grew = 1; }
            if (p[i].x_hi > b) { b = p[i].x_hi; grew = 1; }
        }
    }
    a -= 3;                     /* (X points past a vector: the list starts a vector earlier) */
    b += 1;
    *lo = (uint16_t)(a < 0 ? 0 : a);
    *hi = (uint16_t)(b > 0xffff ? 0xffff : b);
}

static int gcd_int(int a, int b)
{
    while (b) { int t = a % b; a = b; b = t; }
    return a < 0 ? -a : a;
}

static int cmp_u16(const void *a, const void *b)
{
    return (int)*(const uint16_t *)a - (int)*(const uint16_t *)b;
}

static void add_range(vx_rule *r, int kind, uint16_t lo, uint16_t hi)
{
    if (r->ncond >= VX_RULE_CONDS) return;
    vx_cond *c = &r->cond[r->ncond++];
    memset(c, 0, sizeof *c);
    c->kind = kind; c->lo = lo; c->hi = hi;
}

void vx_ce_rule(const vx_piece *p, int np, int sel, const vx_line *l, int n, int scope,
                uint8_t r, uint8_t g, uint8_t b, float gain, vx_rule *out)
{
    memset(out, 0, sizeof *out);
    out->r = r; out->g = g; out->b = b; out->gain = gain; out->ram_ok = 1;
    const vx_piece *s = &p[sel];
    if (scope == VX_CE_OBJECT && s->u == 0) scope = VX_CE_SHAPE;    /* (no object to go by) */
    uint16_t xlo, xhi;
    shape_range(p, np, sel, &xlo, &xhi);
    switch (scope) {
    case VX_CE_OBJECT:
        add_range(out, VX_C_U, s->u, s->u);
        snprintf(out->name, sizeof out->name, "object_%04X", s->u);
        break;
    case VX_CE_CODE:
        if (s->caller) {
            add_range(out, VX_C_CALLER, s->caller, s->caller);
            snprintf(out->name, sizeof out->name, "code_%04X", s->caller);
            break;
        }
        /* (no caller: the routine) */
        /* fall through */
    case VX_CE_ROUTINE:
        add_range(out, VX_C_PC, s->pc_lo, s->pc_hi);
        if (s->dots && out->ncond < VX_RULE_CONDS) { memset(&out->cond[out->ncond], 0, sizeof out->cond[0]); out->cond[out->ncond++].kind = VX_C_DOT; }
        snprintf(out->name, sizeof out->name, "routine_%04X", s->pc_lo);
        break;
    case VX_CE_GROUP: {
        /* the objects drawn by this routine near the chosen one in their table (the U values, while
         * the gaps between them are whole steps of the table): a game may draw one kind of object
         * from several lists (Mine Storm's seeds), so the list is not asked for */
        uint16_t us[VX_CE_PIECES];
        int nu = 0;
        for (int i = 0; i < n && nu < VX_CE_PIECES; i++) {
            const vx_tag *t = &l[i].tag;
            if (l[i].z == 0 || t->pc < s->pc_lo || t->pc > s->pc_hi) continue;
            int seen = 0;
            for (int k = 0; k < nu && !seen; k++) seen = us[k] == t->u;
            if (!seen) us[nu++] = t->u;
        }
        qsort(us, (size_t)nu, sizeof us[0], cmp_u16);
        int at = 0;
        while (at < nu && us[at] != s->u) at++;
        uint16_t ulo = s->u, uhi = s->u;
        if (at < nu && nu > 1) {
            /* the table's step: the smallest one between them; objects not drawn now leave gaps of
             * whole steps, anything else (another table) ends the group */
            int step = 0x7fffffff;         /* (the step next to the chosen one, made finer only to a common
                                              divisor of at least 8 bytes: a table's entries are no smaller) */
            if (at > 0) step = us[at] - us[at - 1];
            if (at + 1 < nu && us[at + 1] - us[at] < step) step = us[at + 1] - us[at];
            int a = at, z = at;
            /* a game's own code walking a table of points in its ROM (Spike's cut scenes: 3 bytes a
             * point) has smaller steps than a table of objects */
            int gmin = s->u < 0x8000 && s->pc_hi < 0x8000 ? 3 : 8;
            for (int more = 1; more;) {
                more = 0;
                if (a > 0) {
                    int gap = us[a] - us[a - 1], g = gcd_int(step, gap);
                    if (g >= gmin && gap <= 8 * g) { step = g; a--; more = 1; }
                }
                if (z + 1 < nu) {
                    int gap = us[z + 1] - us[z], g = gcd_int(step, gap);
                    if (g >= gmin && gap <= 8 * g) { step = g; z++; more = 1; }
                }
            }
            ulo = us[a]; uhi = us[z];
        }
        add_range(out, VX_C_PC, s->pc_lo, s->pc_hi);
        add_range(out, VX_C_U, ulo, uhi);
        snprintf(out->name, sizeof out->name, "group_%04X", ulo);
        break;
    }
    default:
        if (s->parts == 1 && s->pc_hi < 0x8000 && s->x_lo >= 0xC800 && s->x_hi <= 0xCBFF) {
            /* the game's own code, X fixed on its data in RAM (Spike: a row of platforms, drawn by
             * several routines): that X is the shape, whichever routine draws it */
            add_range(out, VX_C_X, s->x_lo, s->x_hi);
            snprintf(out->name, sizeof out->name, "shape_%04X", s->x_lo);
            break;
        }
        add_range(out, VX_C_PC, s->pc_lo, s->pc_hi);
        add_range(out, VX_C_X, xlo, xhi);
        snprintf(out->name, sizeof out->name, "shape_%04X", xlo);
        break;
    }
}

void vx_ce_rule_line(const vx_line *l, int n, int line, uint8_t r, uint8_t g, uint8_t b, float gain, vx_rule *out)
{
    memset(out, 0, sizeof *out);
    out->r = r; out->g = g; out->b = b; out->gain = gain; out->ram_ok = 1;
    const vx_tag *t = &l[line].tag;
    /* its piece: the routines it is drawn with, and whether it has parts (a list, a table) */
    uint16_t lo = 0xffff, hi = 0;
    int parts = 0;
    for (int i = 0; i < n; i++) {
        if (l[i].piece != l[line].piece) continue;
        uint16_t b0 = (uint16_t)(l[i].tag.pc & ~0x1F), b1 = (uint16_t)(l[i].tag.pc | 0x1F);
        if (b0 < lo) lo = b0;
        if (b1 > hi) hi = b1;
        if (l[i].part + 1 > parts) parts = l[i].part + 1;
    }
    add_range(out, VX_C_PC, lo, hi);
    if (parts > 1 && t->x >= 0xC800 && t->x <= 0xCBFF && hi < 0x8000) {
        /* a list the game's own code works out in RAM each frame (Pole Position's road edges): where its
         * vectors lie changes with the picture, the k-th vector is the same one */
        add_range(out, VX_C_PART, l[line].part, l[line].part);
        if (t->caller) add_range(out, VX_C_CALLER, t->caller, t->caller);  /* (not what else it draws) */
        snprintf(out->name, sizeof out->name, "part%u_%04X", (unsigned)l[line].part, lo);
        return;
    }
    add_range(out, VX_C_X, t->x, t->x);
    if (parts > 1) {
        /* one part: X points just past a vector of the list (or at an entry of the game's table), the same
         * X in every copy of the shape */
        snprintf(out->name, sizeof out->name, "line_%04X", t->x);
    } else {
        /* drawn without a list: one stroke of what this code draws */
        if (t->caller) add_range(out, VX_C_CALLER, t->caller, t->caller);
        add_range(out, VX_C_STROKE, l[line].stroke, l[line].stroke);
        snprintf(out->name, sizeof out->name, "stroke%u_%04X", (unsigned)l[line].stroke, t->caller ? t->caller : t->x);
    }
}

/* ---------- the editor ---------- */

int vx_coloredit_target(const char *game_id, int setting, int new_one, int *is_new)
{
    int used = vx_colors_setting_used(game_id, setting);
    int nw = new_one || used < 2;
    if (is_new) *is_new = nw;
    if (!nw) return used - 1;
    int k = vx_colors_own_count(game_id) + 1;
    return k > VX_PROFILES_OWN ? VX_PROFILES_OWN : k;   /* (all used: the last one again) */
}

int vx_coloredit_open(vx_coloredit *e, const vx_line *l, int n)
{
    memset(e, 0, sizeof *e);
    e->npieces = vx_ce_pieces(l, n, e->piece, VX_CE_PIECES);
    int tagged = 0;
    for (int i = 0; i < n && !tagged; i++) tagged = l[i].tag.pc != 0;
    if (!tagged || e->npieces == 0) return -1;
    /* start on the piece nearest the middle */
    double best = 1e30;
    for (int i = 0; i < e->npieces; i++) {
        double dx = e->piece[i].cx - VX_CANVAS_W / 2.0, dy = e->piece[i].cy - VX_CANVAS_H / 2.0;
        if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; e->sel = i; }
    }
    e->gain = 1.0f;
    e->pick_line = -1;
    e->open = 1;
    return 0;
}

/* The piece a line is in (-1 none). */
static int piece_of(const vx_coloredit *e, int line)
{
    for (int p = 0; p < e->npieces; p++)
        if (line >= e->piece[p].first && line <= e->piece[p].last) return p;
    return -1;
}

/* SMART: the lines connected to the seed lines (mark 1) marked too: every lit line of the same object (U)
 * or piece with an end near an end of a marked one, and so on (a whole figure, drawn by one routine or
 * several). Near: about 1 % of the screen - the Vectrex draws with analog integrators, a figure's corners
 * do not meet exactly (Pole Position's car: up to ~300 canvas units); the same object keeps a figure
 * apart from what it touches (the car's nose on the road's centre line). */
static int connect_lines(const vx_line *l, int n, unsigned char *mark)
{
    static int queue[VX_CE_MAXLINES];
    const long T = 350;                         /* (canvas units: about 1 % of the screen) */
    int qh = 0, qn = 0, k = 0;
    if (n > VX_CE_MAXLINES) n = VX_CE_MAXLINES;
    for (int i = 0; i < n; i++) if (mark[i]) { if (l[i].z) { queue[qn++] = i; k++; } else mark[i] = 0; }
#define CE_NEAR(ax, ay, bx, by) (labs((ax) - (bx)) <= T && labs((ay) - (by)) <= T)
    while (qh < qn) {
        const vx_line *a = &l[queue[qh++]];
        for (int j = 0; j < n; j++) {
            if (mark[j] || !l[j].z) continue;
            const vx_line *b = &l[j];
            if (b->tag.u != a->tag.u && b->piece != a->piece) continue;
            if (CE_NEAR(a->x0, a->y0, b->x0, b->y0) || CE_NEAR(a->x0, a->y0, b->x1, b->y1) ||
                CE_NEAR(a->x1, a->y1, b->x0, b->y0) || CE_NEAR(a->x1, a->y1, b->x1, b->y1)) {
                mark[j] = 1; queue[qn++] = j; k++;
            }
        }
    }
#undef CE_NEAR
    return k;
}

int vx_ce_figures(const vx_line *l, int n, int *fig)
{
    static unsigned char mark[VX_CE_MAXLINES];
    if (n > VX_CE_MAXLINES) n = VX_CE_MAXLINES;
    for (int i = 0; i < n; i++) fig[i] = -1;
    int nf = 0;
    for (int i = 0; i < n; i++) {
        if (!l[i].z || fig[i] >= 0) continue;
        memset(mark, 0, (size_t)n);
        mark[i] = 1;
        connect_lines(l, n, mark);
        for (int j = 0; j < n; j++) if (mark[j] && fig[j] < 0) fig[j] = nf;
        nf++;
    }
    return nf;
}

/* SMART's rules for the lines marked: a piece marked whole gets one ALL OF THIS SHAPE rule (all its copies
 * then), the other lines one THIS LINE rule each. */
static void smart_rules(vx_coloredit *e, const unsigned char *mark, const vx_line *l, int n,
                        uint8_t r, uint8_t g, uint8_t b)
{
    e->ncand = 0;
    if (n > VX_CE_MAXLINES) n = VX_CE_MAXLINES;
    for (int p = 0; p < e->npieces && e->ncand < VX_CE_CANDS; p++) {
        const vx_piece *s = &e->piece[p];
        int lit = 0, in = 0;
        for (int i = s->first; i <= s->last && i < n; i++) if (l[i].z) { lit++; in += mark[i] != 0; }
        if (!in) continue;
        vx_rule t;
        if (in == lit) {                        /* (whole: the shape) */
            vx_ce_rule(e->piece, e->npieces, p, l, n, VX_CE_SHAPE, r, g, b, e->gain, &t);
            int dup = 0;
            for (int k = 0; k < e->ncand && !dup; k++) dup = !memcmp(&e->cands[k], &t, sizeof t);
            if (!dup) e->cands[e->ncand++] = t;
            continue;
        }
        for (int i = s->first; i <= s->last && i < n && e->ncand < VX_CE_CANDS; i++) {
            if (!l[i].z || !mark[i]) continue;
            vx_ce_rule_line(l, n, i, r, g, b, e->gain, &t);
            int dup = 0;
            for (int k = 0; k < e->ncand && !dup; k++) dup = !memcmp(&e->cands[k], &t, sizeof t);
            if (!dup) e->cands[e->ncand++] = t;
        }
    }
    if (e->ncand) e->cand = e->cands[0];
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

/* SAME KIND's rule for one line: its piece's shape, then narrowed to what looks like the line. */
static void kind_rule(const vx_coloredit *e, const vx_line *l, int n, int line, uint8_t r, uint8_t g, uint8_t b, vx_rule *out)
{
    int p = piece_of(e, line);
    if (p < 0) { vx_ce_rule_line(l, n, line, r, g, b, e->gain, out); return; }
    vx_ce_rule(e->piece, e->npieces, p, l, n, VX_CE_SHAPE, r, g, b, e->gain, out);
    const vx_piece *me = &e->piece[p];
    size_t L;

    /* figures of another build from the same shape (a list the game fills in RAM for every object: Clean
     * Sweep's vacuum, 10 parts of dots, and its robbers, 8 parts): told apart by their objects. When the
     * others' objects all lie on one side, the rule takes everything from the clicked kind's on (the
     * robbers that come later in further slots too). Without objects: dots or lines, if that differs. */
    unsigned mlo = 0xffff, mhi = 0, olo = 0xffff, ohi = 0;
    int others = 0, no_u = 0, ram = 0;
    for (int k = 0; k < out->ncond; k++)        /* (only a list in RAM: a ROM list is one look, its parts may vary) */
        ram |= out->cond[k].kind == VX_C_X && out->cond[k].lo >= 0xC800 && out->cond[k].hi <= 0xCBFF;
    for (int q = 0; q < e->npieces && ram; q++) {
        const vx_piece *t = &e->piece[q];
        int hit = 0;
        for (int i = t->first; i <= t->last && i < n && !hit; i++) hit = l[i].z && vx_rule_matches(out, &l[i]);
        if (!hit) continue;
        if (!t->u) no_u = 1;
        if (t->parts == me->parts && !t->dots == !me->dots) { if (t->u < mlo) mlo = t->u; if (t->u > mhi) mhi = t->u; }
        else { others++; if (t->u < olo) olo = t->u; if (t->u > ohi) ohi = t->u; }
    }
    if (others && out->ncond < VX_RULE_CONDS) {
        L = strlen(out->name);
        if (!no_u) {
            unsigned lo = mlo, hi = mhi;
            if (ohi < mlo) hi = 0xffff;         /* (the others before: from the clicked kind's on) */
            else if (olo > mhi) lo = 0;         /* (the others after: up to it) */
            add_range(out, VX_C_U, (uint16_t)lo, (uint16_t)hi);
            snprintf(out->name + L, sizeof out->name - L, "_u%04X", mlo);
        } else if (me->dots || me->parts) {
            memset(&out->cond[out->ncond], 0, sizeof out->cond[0]);
            out->cond[out->ncond++].kind = me->dots ? VX_C_DOT : VX_C_LINE;
            snprintf(out->name + L, sizeof out->name - L, me->dots ? "_dots" : "_lines");
        }
    }

    /* many small marks of one size drawn in one go with bigger lines (a maze's coins or pellets with its
     * walls): the smallest strokes, at least 8 of them within 1.3 times the smallest, the next one at least
     * twice as long. A click on such a mark takes the marks, a click on anything else all but the marks.
     * Only for a piece of single strokes (one line a part), not for a figure of several (a robber). */
    if (is_dot(&l[line]) || out->ncond >= VX_RULE_CONDS) return;
    int lit = 0;
    for (int i = me->first; i <= me->last && i < n; i++) lit += l[i].z != 0;
    if (me->parts < 2 || lit != me->parts) return;
    static float len[VX_CE_MAXLINES];
    int m = 0;
    for (int i = 0; i < n && m < VX_CE_MAXLINES; i++)
        if (l[i].z && !is_dot(&l[i]) && vx_rule_matches(out, &l[i])) len[m++] = (float)hypot((double)(l[i].x1 - l[i].x0), (double)(l[i].y1 - l[i].y0));
    if (m < 9) return;
    qsort(len, (size_t)m, sizeof len[0], cmp_float);
    int k = 0;
    while (k + 1 < m && len[k + 1] <= 1.3f * len[0] + 1.0f) k++;
    if (k + 1 < 8 || k + 1 >= m || len[k + 1] < 2.0f * len[k]) return;
    float mine = (float)hypot((double)(l[line].x1 - l[line].x0), (double)(l[line].y1 - l[line].y0));
    int mark = mine <= len[k] * 1.1f;
    double lo = mark ? 0 : len[k + 1] * 0.8, hi = mark ? len[k] * 1.25 : 65535;
    add_range(out, VX_C_LENGTH, (uint16_t)lo, (uint16_t)hi);
    L = strlen(out->name);
    snprintf(out->name + L, sizeof out->name - L, mark ? "_marks" : "_big");
}

void vx_ce_refresh(vx_coloredit *e, const vx_line *l, int n)
{
    uint8_t r = 255, g = 255, b = 255;
    vx_ce_palette(e->color, &r, &g, &b);
    if (e->scope == VX_CE_KIND) {               /* the lines picked (each its kind), else the line */
        e->ncand = 0;
        for (int i = 0; i < n && i < VX_CE_MAXLINES && e->ncand < VX_CE_CANDS; i++) {
            if (e->nmsel > 0 ? (!e->msel[i] || !l[i].z) : i != e->line) continue;
            vx_rule t;
            kind_rule(e, l, n, i, r, g, b, &t);
            int dup = 0;
            for (int k = 0; k < e->ncand && !dup; k++) dup = !memcmp(&e->cands[k], &t, sizeof t);
            if (!dup) e->cands[e->ncand++] = t;
        }
        if (e->ncand) e->cand = e->cands[0];
        return;
    }
    if (e->scope == VX_CE_SMART) {              /* the lines picked, else the piece's figure */
        static unsigned char mark[VX_CE_MAXLINES];
        int m = n < VX_CE_MAXLINES ? n : VX_CE_MAXLINES;
        if (e->nmsel > 0) memcpy(mark, e->msel, (size_t)m);
        else {
            memset(mark, 0, (size_t)m);
            for (int i = e->piece[e->sel].first; i <= e->piece[e->sel].last && i < m; i++) mark[i] = 1;
            connect_lines(l, m, mark);
        }
        smart_rules(e, mark, l, n, r, g, b);
        return;
    }
    if (e->nmsel > 0) {                         /* lines picked with the mouse: a rule for each (WHAT of
                                                   each line: its shape, the line itself, its object ...) */
        e->ncand = 0;
        for (int i = 0; i < n && i < VX_CE_MAXLINES && e->ncand < VX_CE_CANDS; i++) {
            if (!e->msel[i] || !l[i].z) continue;
            vx_rule t;
            if (e->scope == VX_CE_LINE) vx_ce_rule_line(l, n, i, r, g, b, e->gain, &t);
            else {
                int p = piece_of(e, i);
                if (p < 0) continue;
                vx_ce_rule(e->piece, e->npieces, p, l, n, e->scope, r, g, b, e->gain, &t);
            }
            int dup = 0;                        /* (the same rule for several lines: once) */
            for (int k = 0; k < e->ncand && !dup; k++) dup = !memcmp(&e->cands[k], &t, sizeof t);
            if (!dup) e->cands[e->ncand++] = t;
        }
        if (e->ncand) e->cand = e->cands[0];
        return;
    }
    if (e->scope == VX_CE_LINE) vx_ce_rule_line(l, n, e->line, r, g, b, e->gain, &e->cand);
    else vx_ce_rule(e->piece, e->npieces, e->sel, l, n, e->scope, r, g, b, e->gain, &e->cand);
    e->cands[0] = e->cand;
    e->ncand = 1;
}

/* Whether the panel's rules would colour the line. */
int vx_ce_matches(const vx_coloredit *e, const vx_line *l)
{
    for (int k = 0; k < e->ncand; k++) if (vx_rule_matches(&e->cands[k], l)) return 1;
    return 0;
}

/* The panel's rules into the profile. 0, or -1 (it is full). */
int vx_ce_take(vx_coloredit *e, vx_colors *colors)
{
    for (int k = e->ncand - 1; k >= 0; k--)     /* (the first ends up on top) */
        if (vx_colors_put(colors, &e->cands[k]) != 0) return -1;
    return 0;
}

/* The rules colouring what is chosen removed: the lines picked, else the line / the piece. */
int vx_ce_remove(vx_coloredit *e, vx_colors *colors, const vx_line *l, int n)
{
    if (e->nmsel > 0) {
        int k = 0;
        for (int i = 0; i < n && i < VX_CE_MAXLINES; i++)
            if (e->msel[i] && l[i].z) k += vx_colors_remove_matching(colors, &l[i]);
        return k;
    }
    return vx_colors_remove_matching(colors, &l[e->scope == VX_CE_LINE ? e->line : e->piece[e->sel].first]);
}

/* The panel for the chosen piece, starting from the piece's colour now. */
static void open_panel(vx_coloredit *e, const vx_colors *colors, const vx_line *l, int n)
{
    e->panel = 1;
    e->said[0] = '\0';
    e->scope = VX_CE_SHAPE;
    const vx_piece *s = &e->piece[e->sel];
    e->line = e->pick_line >= s->first && e->pick_line <= s->last ? e->pick_line : s->first;   /* (clicked) */
    e->gain = 1.0f;
    e->color = 0;
    const vx_rule *u = vx_colors_find(colors, &l[e->line]);
    if (u) {
        e->gain = u->gain;
        for (int i = 0; i < NPAL; i++)
            if (PALETTE[i].r == u->r && PALETTE[i].g == u->g && PALETTE[i].b == u->b) e->color = i;
    }
    vx_ce_refresh(e, l, n);
}

/* A line's ends on the board. */
static void line_board(const vx_config *cfg, const vx_line *l, float *x0, float *y0, float *x1, float *y1)
{
    vx_map(cfg, (double)l->x0, (double)l->y0, x0, y0);
    vx_map(cfg, (double)l->x1, (double)l->y1, x1, y1);
}

/* The lit line nearest the point, within about 2 % of the screen (-1 none). */
static int line_at(const vx_config *cfg, const vx_line *l, int n, int px, int py)
{
    int best = -1;
    double bd = 90.0 * 90.0;
    for (int i = 0; i < n; i++) {
        if (!l[i].z) continue;
        float x0, y0, x1, y1;
        line_board(cfg, &l[i], &x0, &y0, &x1, &y1);
        double dx = x1 - x0, dy = y1 - y0, len = dx * dx + dy * dy;
        double t = len > 0 ? ((px - x0) * dx + (py - y0) * dy) / len : 0.0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        double ex = x0 + t * dx - px, ey = y0 + t * dy - py, d = ex * ex + ey * ey;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void vx_ce_mouse_pick(vx_coloredit *e, const vx_colors *colors, const vx_config *cfg, const vx_line *l, int n,
                      int ax0, int ay0, int mx, int my, int was_drag, int ctrl)
{
    int keep_scope = e->panel ? e->scope : -1, keep_color = e->color, keep_gain = e->panel ? 1 : 0;
    float keep_g = e->gain;
    int first = -1;
    if (was_drag) {                             /* the box */
        int x0 = ax0 < mx ? ax0 : mx, x1 = ax0 < mx ? mx : ax0;
        int y0 = ay0 < my ? ay0 : my, y1 = ay0 < my ? my : ay0;
        if (!ctrl) { memset(e->msel, 0, sizeof e->msel); e->nmsel = 0; }
        for (int i = 0; i < n && i < VX_CE_MAXLINES; i++) {
            if (!l[i].z || e->msel[i]) continue;
            float ax, ay, bx, by;
            line_board(cfg, &l[i], &ax, &ay, &bx, &by);
            if (ax >= x0 && ax <= x1 && bx >= x0 && bx <= x1 && ay >= y0 && ay <= y1 && by >= y0 && by <= y1) {
                e->msel[i] = 1; e->nmsel++;
                if (first < 0) first = i;
            }
        }
        if (e->nmsel == 0) { snprintf(e->said, sizeof e->said, "NO LINE IN THE BOX"); return; }
        if (first < 0) for (int i = 0; i < n && first < 0; i++) if (e->msel[i]) first = i;
    } else {                                    /* the click */
        int i = line_at(cfg, l, n, mx, my);
        if (i < 0) return;
        if (ctrl && i < VX_CE_MAXLINES) {       /* (one line more or less in the set) */
            if (e->nmsel == 0 && e->panel)      /* (the set starts with the line chosen) */
                for (int k = e->scope == VX_CE_LINE ? e->line : e->piece[e->sel].first;
                     k <= (e->scope == VX_CE_LINE ? e->line : e->piece[e->sel].last) && k < VX_CE_MAXLINES; k++)
                    if (l[k].z && !e->msel[k]) { e->msel[k] = 1; e->nmsel++; }
            e->msel[i] = !e->msel[i];
            e->nmsel += e->msel[i] ? 1 : -1;
            first = i;
            if (e->nmsel <= 0) { e->nmsel = 0; memset(e->msel, 0, sizeof e->msel); }
        } else {
            if (e->nmsel) { memset(e->msel, 0, sizeof e->msel); e->nmsel = 0; }
            first = i;
        }
    }
    int p = piece_of(e, first);
    if (p < 0) return;
    e->sel = p;
    e->pick_line = first;
    if (!e->panel) open_panel(e, colors, l, n);
    if (keep_scope >= 0) { e->scope = keep_scope; e->color = keep_color; if (keep_gain) e->gain = keep_g; }
    e->line = first;
    if (e->scope == VX_CE_SMART && !was_drag && !ctrl) {   /* SMART: a click picks the whole figure */
        memset(e->msel, 0, sizeof e->msel);
        if (first < VX_CE_MAXLINES) { e->msel[first] = 1; e->nmsel = connect_lines(l, n, e->msel); }
    }
    vx_ce_refresh(e, l, n);
    if (e->nmsel) snprintf(e->said, sizeof e->said, "%d LINE%s PICKED", e->nmsel, e->nmsel == 1 ? "" : "S");
    else e->said[0] = '\0';
}
