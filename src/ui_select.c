/* ui_select.c: the fighter select (#s-select). Thirteen fighters and RANDOM in a grid of
 * portraits, a cursor for each side, a panel for each side and the two fighters standing large
 * in the scene behind (game.c). Each side picks in steps: FIGHTER, WEAPON, (the CPU's LEVEL),
 * READY; B steps back. In one player mode P1 picks their own side and then the CPU's.
 *
 * Input comes tagged by player (input.h menu events from the pads and the phone pads, the
 * keyboard's keys mapped here by mode, the mouse). What a side shows is written into the game's
 * selection (ck_sel) as it changes; game.c loads a body only once the cursor rests on a fighter
 * or a pick is confirmed (ui_cb.pick).
 *
 * The portraits are the game's own bodies, rendered by tools/bake-portraits.mjs into
 * assets/portraits/<key>.png and drawn at half their size. The grid serves the tourney setup and
 * the online screen too, as a modal (select_modal_open). */
#include "ui_int.h"
#include "ui_kit.h"
#include "ui_hint.h"
#include "ck.h"
#include "audio.h"
#include "input.h"
#include "roster.h"
#include "weapons.h"
#include "special.h"
#include "three.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------- the layout (1280 x 720) */
#define COLS 7
#define CELLS 14                 /* the roster, then RANDOM */
#define RANDOM_CELL (CELLS - 1)
#define CELL 76
#define GAP 8
static float GRID_X = 350, GRID_Y = 492;   /* the screen's; the modal centres it */
#define PANEL_W 310
#define PANEL_Y 482
#define PANEL_H 226
static const float PANEL_X[2] = {24, 946};
static const float BODY_X[2] = {270, 1010};   /* where game.c stands each side's fighter (screen x) */
#define PLATE_BASE 452                         /* the name's baseline under each fighter */
#define PORTRAIT 140                           /* baked portraits: drawn at 70 */

static const char *LEVELS[3] = {"ROOKIE", "JOURNEYMAN", "EXTREME"};
static uint32_t slot_col(int g) { return g ? C_CYAN : C_PINK; }

static float cell_x(int i) { return GRID_X + (i % COLS) * (CELL + GAP); }
static float cell_y(int i) { return GRID_Y + (i / COLS) * (CELL + GAP); }
/* roster order fills the grid, RANDOM last */
static int cell_of(int fighter) { return fighter >= 0 && fighter < ROSTER_N ? fighter : RANDOM_CELL; }

/* ---------------------------------------------------------------- portraits */
static struct { uint8_t *px; int w, h; } PORT[32];
static void portraits_load(void) {
  for (int i = 0; i < ROSTER_N && i < 32; i++) {
    char n[48]; snprintf(n, sizeof n, "portrait_%s.png", ROSTER[i].key);
    size_t len; uint8_t *d = ck_asset(n, &len);
    if (!d) continue;
    t3_texture *T = t3_texture_load_memory(d, len);
    free(d);
    if (T && T->pixels) {
      PORT[i].w = T->width; PORT[i].h = T->height;
      PORT[i].px = malloc((size_t)T->width * T->height * 4);
      memcpy(PORT[i].px, T->pixels, (size_t)T->width * T->height * 4);
    }
    if (T) t3_release(T);
  }
}
/* a fighter's face in a w x h box: the portrait, or (before any are baked) initials */
static void portrait_paint(cv *c, int f, float x, float y, float w, float h) {
  if (f >= 0 && f < 32 && PORT[f].px) { cv_draw_image(c, PORT[f].px, PORT[f].w, PORT[f].h, x, y, w, h); return; }
  const char *nm = roster_get(f)->name, *sp = strchr(nm, ' ');
  char in[3] = {nm[0], sp && sp[1] ? sp[1] : nm[1], 0};
  float fs = h * 0.42f, tw = t_width(F_MARKER, fs, 0, in);
  t_draw(c, F_MARKER, fs, 0, x + (w - tw) / 2, y + h / 2 + fs * 0.36f, in, COL(C_INK));
}

void select_face(cv *c, int fighter, float x, float y, float w, float h) { portrait_paint(c, fighter, x, y, w, h); }

/* ---------------------------------------------------------------- state */
enum { SS_FIGHTER, SS_WEAPON, SS_LEVEL, SS_READY };
typedef struct {
  int step;              /* SS_* */
  int cell;              /* the cursor: a roster index or RANDOM_CELL */
  int fighter, weapon;   /* what this side shows (the hovered or picked fighter) */
  int colour;            /* the colour this side chose (colour_of: the one it wears) */
  int seq;               /* when it confirmed its fighter (0: not yet): the first keeps its colour */
  int flips; float flip_t;   /* RANDOM is choosing: flips left, time to the next */
  trans_t cx, cy;        /* the cursor ring, eased */
} pick_t;
static pick_t PS[2];
static int seq_n;
static int hist[16], nhist;    /* which side took each step, for the shared Escape */
static float go_t = -1;        /* both are READY: the beat before the stage select */
static bool dirty_grid, dirty_panel[2], dirty_plate[2];
static layer GRID_L, PANEL_L[2], PLATE_L[2], TAB_L[2];
static char tab_s[2][16];
static hint_line HINT[2];
static int hover_cell = -1;    /* the mouse over a cell */
static bool modal;             /* picking for the tourney or the online screen (select_modal_open) */
static void (*modal_done)(int fighter);
static char modal_title[48];

static bool cpu_side(int g) { return scr_mode == 1 && g == 1; }
static bool solo(void) { return scr_mode == 1; }
/* the side a player drives: in one player P1 drives their side, then the CPU's */
static int side_of(int player) {
  if (modal) return 0;
  if (!solo()) return player & 1;
  return PS[0].step == SS_READY ? 1 : 0;
}
static bool side_active(int g) { return modal ? g == 0 : solo() ? side_of(0) == g : true; }

static void dirty_all(void) { dirty_grid = true; for (int g = 0; g < 2; g++) dirty_panel[g] = dirty_plate[g] = true; }
/* the mirror rule: when both sides show the same fighter in the same colour, the side that
 * confirmed it later (or P2, when neither has) wears the next colour */
static bool keeps(int g) {
  int o = 1 - g;
  if (PS[g].seq && PS[o].seq) return PS[g].seq < PS[o].seq;
  return PS[g].seq ? true : PS[o].seq ? false : g == 0;
}
static int colour_of(int g) {
  if (modal || PS[0].fighter != PS[1].fighter || keeps(g)) return PS[g].colour;
  return PS[g].colour == PS[1 - g].colour ? (PS[g].colour + 1) % ROSTER_COLOURS : PS[g].colour;
}
static void write_slot(int g, bool now) {
  pick_t *p = &PS[g];
  snprintf(scr_sel->slots[g].ch, sizeof scr_sel->slots[g].ch, "%s", ROSTER[p->fighter].key);
  snprintf(scr_sel->slots[g].weapon, sizeof scr_sel->slots[g].weapon, "%s", WEAPONS[p->weapon].key);
  scr_sel->slots[g].colour = colour_of(g);
  if (ui_cb.pick) ui_cb.pick(g, now);
}
static void write_sel(int g, bool now) {
  if (modal || !scr_sel) return;
  write_slot(g, now);
  int o = 1 - g;   /* the other side's colour may follow from this one's */
  if (scr_sel->slots[o].colour != colour_of(o)) { write_slot(o, false); dirty_panel[o] = true; }
}
static void cursor_to(int g, int cell, bool jump) {
  pick_t *p = &PS[g];
  p->cell = cell;
  if (jump) { tr_jump(&p->cx, cell_x(cell)); tr_jump(&p->cy, cell_y(cell)); }
  else { tr_set(&p->cx, cell_x(cell), ui_now, 0.08f, 0); tr_set(&p->cy, cell_y(cell), ui_now, 0.08f, 0); }
  dirty_grid = true;
}
/* the cursor moved onto a cell: the side shows that fighter at once (the body follows when it rests) */
static void hover(int g, int cell) {
  pick_t *p = &PS[g];
  cursor_to(g, cell, false);
  if (cell != RANDOM_CELL && cell != p->fighter) {
    p->fighter = cell; p->weapon = weapon_index(ROSTER[cell].weapon); if (p->weapon < 0) p->weapon = 0;
    write_sel(g, false);
  }
  dirty_panel[g] = dirty_plate[g] = true;
}

static void push_hist(int g) { if (nhist < 16) hist[nhist++] = g; }
static void check_done(void);
static void land(int g, int f) {
  pick_t *p = &PS[g];
  p->fighter = f; p->weapon = weapon_index(ROSTER[f].weapon); if (p->weapon < 0) p->weapon = 0;
  cursor_to(g, f, false);
  if (modal) { p->step = SS_READY; check_done(); return; }
  p->step = SS_WEAPON; p->seq = ++seq_n; push_hist(g);
  write_sel(g, true);
  if (ui_cb.cheer) ui_cb.cheer(g);
  dirty_all();
}
static void random_pick(int g) {
  sfx_ui_ok();
  PS[g].flips = 6; PS[g].flip_t = 0;
  dirty_panel[g] = dirty_plate[g] = true;
}

static void ok(int g) {
  pick_t *p = &PS[g];
  if (p->flips) return;
  switch (p->step) {
    case SS_FIGHTER:
      if (p->cell == RANDOM_CELL) { random_pick(g); return; }
      sfx_ui_ok(); land(g, p->cell); return;
    case SS_WEAPON:
      sfx_ui_ok(); push_hist(g);
      p->step = cpu_side(g) ? SS_LEVEL : SS_READY;
      write_sel(g, true);
      break;
    case SS_LEVEL:
      sfx_ui_ok(); push_hist(g); p->step = SS_READY; break;
    default: return;
  }
  dirty_all();
  check_done();
}
static void to_title(void) { if (ui_cb.act) { sfx_ui(); ui_cb.act("back"); } }
static void back(int g) {
  pick_t *p = &PS[g];
  if (p->flips) return;
  if (modal) { if (modal_done) modal_done(-1); return; }
  go_t = -1;
  switch (p->step) {
    case SS_READY: p->step = cpu_side(g) ? SS_LEVEL : SS_WEAPON; break;
    case SS_LEVEL: p->step = SS_WEAPON; break;
    case SS_WEAPON: p->step = SS_FIGHTER; p->seq = 0; write_sel(g, false); break;
    default:
      /* the CPU's first step backs into P1's weapon; P1's leaves for the title when P2 has not begun */
      if (cpu_side(g)) { PS[0].step = SS_WEAPON; break; }
      if (g == 0 && PS[1].step == SS_FIGHTER) { to_title(); return; }
      return;
  }
  if (nhist) nhist--;
  sfx_ui();
  dirty_all();
}
/* the shared Escape: undo the last step anyone took (or leave, when nobody has taken one) */
static void back_last(void) {
  if (modal) { back(0); return; }
  int g = nhist ? hist[nhist - 1] : solo() ? side_of(0) : 0;
  if (!nhist && PS[0].step == SS_FIGHTER && PS[1].step == SS_FIGHTER) { to_title(); return; }
  back(g);
}

static void move(int g, int ev) {
  pick_t *p = &PS[g];
  if (p->flips) return;
  if (p->step == SS_FIGHTER) {
    int c = p->cell, col = c % COLS, row = c / COLS;
    if (ev == ME_LEFT) c = (c + CELLS - 1) % CELLS;
    else if (ev == ME_RIGHT) c = (c + 1) % CELLS;
    else { row ^= 1; c = row * COLS + col; }
    sfx_ui(); hover(g, c);
  } else if (p->step == SS_WEAPON && (ev == ME_LEFT || ev == ME_RIGHT)) {
    p->weapon = (p->weapon + (ev == ME_RIGHT ? 1 : WEAPON_N - 1)) % WEAPON_N;
    sfx_ui(); write_sel(g, true); dirty_panel[g] = true;
  } else if (p->step == SS_LEVEL && (ev == ME_LEFT || ev == ME_RIGHT) && scr_sel) {
    scr_sel->level = (scr_sel->level + (ev == ME_RIGHT ? 1 : 2)) % 3;
    if (ui_cb.diff) ui_cb.diff(scr_sel->level);
    sfx_ui(); dirty_panel[g] = true;
  }
}
static void colour(int g, int d) {
  pick_t *p = &PS[g];
  if (modal || p->flips || p->step == SS_READY) return;
  int o = 1 - g, c = colour_of(g);
  for (int k = 0; k < ROSTER_COLOURS; k++) {
    c = (c + d + ROSTER_COLOURS) % ROSTER_COLOURS;
    if (!(PS[o].fighter == p->fighter && colour_of(o) == c)) break;
  }
  p->colour = c;
  sfx_ui(); write_sel(g, true); dirty_panel[g] = true;
}
static void swap(void) {
  if (modal || PS[0].flips || PS[1].flips) return;
  pick_t t = PS[0]; PS[0] = PS[1]; PS[1] = t;
  /* the CPU has no READY of P1's kind: a swap in one player mode starts the CPU's picks over */
  if (solo()) { if (PS[1].step == SS_READY && PS[0].step != SS_READY) PS[1].step = SS_WEAPON; if (PS[0].step == SS_LEVEL) PS[0].step = SS_READY; }
  for (int g = 0; g < 2; g++) { cursor_to(g, PS[g].cell, false); write_sel(g, true); }
  nhist = 0; go_t = -1;
  sfx_ui(); dirty_all();
}
/* START: take what each side shows; a CPU nobody picked for fights as RANDOM */
static void start_all(void) {
  if (modal) { ok(0); return; }
  /* asked before the loop below makes P1 READY, which would hand P1 the CPU's turn */
  bool cpu_unset = solo() && side_of(0) == 0;
  for (int g = 0; g < 2; g++) {
    pick_t *p = &PS[g];
    if (p->flips) { p->flips = 1; p->flip_t = 0; }
    if (p->step == SS_FIGHTER && (p->cell == RANDOM_CELL || (cpu_side(g) && cpu_unset))) {
      int f; do f = (int)(ck_rand() * ROSTER_N); while (f == PS[1 - g].fighter && ROSTER_N > 1);
      p->fighter = f; p->weapon = weapon_index(ROSTER[f].weapon); if (p->weapon < 0) p->weapon = 0;
      cursor_to(g, f, false);
    }
    if (!p->seq) p->seq = ++seq_n;
    p->flips = 0; p->step = SS_READY;
    write_sel(g, true);
  }
  sfx_ui_ok(); dirty_all(); check_done();
}
static void check_done(void) {
  if (modal) { if (PS[0].step == SS_READY && modal_done) modal_done(PS[0].fighter); return; }
  if (PS[0].step == SS_READY && PS[1].step == SS_READY && go_t < 0) {
    go_t = 0.4f;
    if (ui_cb.cheer) { ui_cb.cheer(0); ui_cb.cheer(1); }
  }
}

/* one event from one player */
static void sel_event(int player, int ev) {
  if (go_t >= 0 && ev != ME_BACK) return;
  if (player < 0) { if (ev == ME_BACK) back_last(); return; }
  int g = side_of(player);
  switch (ev) {
    case ME_UP: case ME_DOWN: case ME_LEFT: case ME_RIGHT: move(g, ev); break;
    case ME_OK: ok(g); break;
    case ME_BACK: back(g); break;
    case ME_SWAP: swap(); break;
    case ME_PREV: colour(g, -1); break;
    case ME_NEXT: colour(g, 1); break;
    case ME_START: start_all(); break;
  }
}

/* the keyboard by mode: two players split it (P1 WASD Q R E, P2 the arrows , / .), one player
 * has all of it; Escape is the shared back, Tab swaps */
void select_key(int k) {
  if (k == WC_KEY_ESCAPE) { input_note_dev(DEV_KEYS); sel_event(-1, ME_BACK); return; }
  if (k == WC_KEY_TAB) { sel_event(0, ME_SWAP); return; }
  int p = 0, ev = -1;
  if (solo() || modal) {
    switch (k) {
      case WC_KEY_UP: case WC_KEY_W: ev = ME_UP; break;
      case WC_KEY_DOWN: case WC_KEY_S: ev = ME_DOWN; break;
      case WC_KEY_LEFT: case WC_KEY_A: ev = ME_LEFT; break;
      case WC_KEY_RIGHT: case WC_KEY_D: ev = ME_RIGHT; break;
      case WC_KEY_ENTER: case WC_KEY_KP_ENTER: case WC_KEY_SPACE: case WC_KEY_Q: ev = ME_OK; break;
      case WC_KEY_R: case WC_KEY_BACKSPACE: ev = ME_BACK; break;
      case WC_KEY_E: ev = ME_NEXT; break;
    }
  } else {
    switch (k) {
      case WC_KEY_W: ev = ME_UP; break;
      case WC_KEY_S: ev = ME_DOWN; break;
      case WC_KEY_A: ev = ME_LEFT; break;
      case WC_KEY_D: ev = ME_RIGHT; break;
      case WC_KEY_Q: case WC_KEY_SPACE: ev = ME_OK; break;
      case WC_KEY_R: ev = ME_BACK; break;
      case WC_KEY_E: ev = ME_NEXT; break;
      case WC_KEY_UP: p = 1; ev = ME_UP; break;
      case WC_KEY_DOWN: p = 1; ev = ME_DOWN; break;
      case WC_KEY_LEFT: p = 1; ev = ME_LEFT; break;
      case WC_KEY_RIGHT: p = 1; ev = ME_RIGHT; break;
      case WC_KEY_COMMA: case WC_KEY_ENTER: case WC_KEY_KP_ENTER: p = 1; ev = ME_OK; break;
      case WC_KEY_SLASH: p = 1; ev = ME_BACK; break;
      case WC_KEY_PERIOD: p = 1; ev = ME_NEXT; break;
    }
  }
  if (ev >= 0) sel_event(p, ev);
}

/* ---------------------------------------------------------------- the mouse */
static float PANEL_HIT_W[2][2];   /* the weapon row's arrows: their x, per side */
static int cell_at(float x, float y) {
  for (int i = 0; i < CELLS; i++) if (x >= cell_x(i) - 4 && x < cell_x(i) + CELL + 4 && y >= cell_y(i) - 4 && y < cell_y(i) + CELL + 4) return i;
  return -1;
}
/* the mouse drives the first side a person picks for that is not READY */
static int mouse_side(void) {
  if (modal) return 0;
  if (solo()) return side_of(0);
  return PS[0].step != SS_READY ? 0 : PS[1].step != SS_READY ? 1 : 0;
}
#define WROW_Y 90   /* the weapon row, from the panel's top */
#define SWATCH_Y 10
static float swatch_x(int g, int k) { return PANEL_X[g] + PANEL_W - 16 - (ROSTER_COLOURS - k) * 24 + 6; }
void select_pointer(float x, float y, bool pressed) {
  const ck_pointer *P = input_pointer();
  int c = cell_at(x, y);
  if (c != hover_cell) { hover_cell = c; dirty_grid = true; }
  bool on_back = btn_hit(&back_b, x, y);
  if (on_back != back_b.sel) btn_set_sel(&back_b, on_back);
  int g = mouse_side();
  if (P->moved && c >= 0 && PS[g].step == SS_FIGHTER && !PS[g].flips && c != PS[g].cell && go_t < 0) hover(g, c);
  if (P->rpressed) { back(g); return; }
  if (!pressed) return;
  if (on_back) { if (modal) back(0); else to_title(); return; }
  if (c >= 0 && go_t < 0) {
    if (PS[g].step != SS_FIGHTER && !modal) {   /* a click on the grid picks again */
      while (PS[g].step != SS_FIGHTER) back(g);
    }
    if (PS[g].step == SS_FIGHTER) { if (c != PS[g].cell) hover(g, c); ok(g); }
    return;
  }
  if (modal) return;
  /* the panels: the weapon arrows and name, the colour swatches, the level chips */
  for (int s = 0; s < 2; s++) {
    float px = PANEL_X[s], py = PANEL_Y;
    if (x < px || x >= px + PANEL_W || y < py || y >= py + PANEL_H) continue;
    if (!side_active(s) || PS[s].step == SS_READY) return;
    if (y >= py + SWATCH_Y - 4 && y < py + SWATCH_Y + 26) {
      for (int k = 0; k < ROSTER_COLOURS; k++) if (x >= swatch_x(s, k) - 3 && x < swatch_x(s, k) + 23) {
        if (k != colour_of(s) && !(PS[1 - s].fighter == PS[s].fighter && colour_of(1 - s) == k)) { PS[s].colour = k; sfx_ui(); write_sel(s, true); dirty_panel[s] = true; }
        return;
      }
    }
    if (PS[s].step == SS_WEAPON && y >= py + WROW_Y - 6 && y < py + WROW_Y + 30) {
      if (x < PANEL_HIT_W[s][0]) move(s, ME_LEFT);
      else if (x >= PANEL_HIT_W[s][1]) move(s, ME_RIGHT);
      else ok(s);
      return;
    }
    if (PS[s].step == SS_LEVEL && y >= py + PANEL_H - 36) {
      float cx = px + 16;
      for (int i = 0; i < 3; i++) {
        float w = t_width(F_TYPE, 14, 0, LEVELS[i]) + 18;
        if (x >= cx && x < cx + w) { if (scr_sel) { scr_sel->level = i; if (ui_cb.diff) ui_cb.diff(i); } ok(s); return; }
        cx += w + 6;
      }
    }
    if (PS[s].step == SS_WEAPON || PS[s].step == SS_LEVEL) ok(s);
    return;
  }
}

/* ---------------------------------------------------------------- painting */
/* a cell: paper, the portrait, the fighter's stripe, an ink border, a hard shadow; lifted when a
 * cursor or the mouse is on it, washed in a side's colour once that side is READY on it */
static void cell_paint(cv *c, int i, float ox, float oy) {
  float x = cell_x(i) - ox, y = cell_y(i) - oy;
  bool lift = i == hover_cell;
  for (int g = 0; g < 2; g++) if (PS[g].cell == i && !modal) lift = true;
  if (modal && PS[0].cell == i) lift = true;
  if (lift) { x -= 3; y -= 3; }
  float sh = lift ? 7 : 4;
  c_rect(c, x + sh, y + sh, CELL, CELL, COL(C_INK));
  c_rect(c, x, y, CELL, CELL, COL(C_PAPER));
  if (i == RANDOM_CELL) {
    cv_save(c);
    cv_begin(c); cv_rect(c, x + 3, y + 3, CELL - 6, CELL - 6); cv_clip(c);
    for (float k = -CELL; k < CELL * 2; k += 16) {
      cv_begin(c); cv_move(c, x + k, y + CELL); cv_line(c, x + k + 7, y + CELL); cv_line(c, x + k + 7 + CELL, y); cv_line(c, x + k + CELL, y); cv_close(c);
      cv_fill_hex(c, C_PINK, 1); cv_fill(c);
      cv_begin(c); cv_move(c, x + k + 7, y + CELL); cv_line(c, x + k + 9, y + CELL); cv_line(c, x + k + 9 + CELL, y); cv_line(c, x + k + 7 + CELL, y); cv_close(c);
      cv_fill_hex(c, C_INK, 1); cv_fill(c);
    }
    cv_restore(c);
    static const tshadow QS = {4, {{-2, -2, COL(C_INK)}, {2, -2, COL(C_INK)}, {-2, 2, COL(C_INK)}, {2, 2, COL(C_INK)}}};
    float tw = t_width(F_MARKER, 44, 0, "?");
    t_draw_sh(c, F_MARKER, 44, 0, x + (CELL - tw) / 2, y + CELL / 2 + 16, "?", COL(C_CREAM), &QS);
  } else {
    portrait_paint(c, i, x + 3, y + 3, CELL - 6, CELL - 6);
    c_rect(c, x + 3, y + CELL - 8, CELL - 6, 5, COL(ROSTER[i].color));
  }
  for (int g = 0; g < 2; g++) if (!modal && PS[g].cell == i && PS[g].step > SS_FIGHTER) c_rect(c, x + 3, y + 3, CELL - 6, CELL - 6, COLA(slot_col(g), 0.3f));
  c_border(c, x, y, CELL, CELL, 3, COL(C_INK));
}
static void grid_paint(void) {
  L_ensure(&GRID_L, 7 * (CELL + GAP) + 16, 2 * (CELL + GAP) + 16);
  cv *c = GRID_L.c;
  for (int i = 0; i < CELLS; i++) cell_paint(c, i, GRID_X - 6, GRID_Y - 6);
  dirty_grid = false;
}

static const char *step_label(int g) {
  const pick_t *p = &PS[g];
  if (p->flips) return "RANDOM";
  if (modal) return "PICK A FIGHTER";
  if (solo() && g == 1 && PS[0].step != SS_READY) return "UP NEXT";
  switch (p->step) {
    case SS_FIGHTER: return "PICK A FIGHTER";
    case SS_WEAPON: return "PICK A WEAPON";
    case SS_LEVEL: return "PICK A LEVEL";
    default: return "READY";
  }
}
/* a swatch's colour: the fighter's accent for its own colours, turned as the alternate turns the
 * top (a KEYHOLDER body), or the tint of the alternate's clothes (a page body) */
static uint32_t hsv_hex(float h, float s, float v) {
  h = fmodf(h / 60.f + 12, 6); s = ck_clamp(s, 0, 1); v = ck_clamp(v, 0, 1);
  float c = v * s, x = c * (1 - fabsf(fmodf(h, 2) - 1)), m = v - c, o[3]; int k = (int)h;
  o[0] = (k == 0 || k == 5 ? c : k == 1 || k == 4 ? x : 0) + m;
  o[1] = (k == 1 || k == 2 ? c : k == 0 || k == 3 ? x : 0) + m;
  o[2] = (k == 3 || k == 4 ? c : k == 2 || k == 5 ? x : 0) + m;
  return ((uint32_t)(o[0] * 255 + .5f) << 16) | ((uint32_t)(o[1] * 255 + .5f) << 8) | (uint32_t)(o[2] * 255 + .5f);
}
static uint32_t swatch_rgb(const roster_entry *e, int k) {
  if (!k) return e->color;
  if (e->kind == BODY_PAGE) {
    const roster_swatch *l = &e->alts[k - 1][1], *d = &e->alts[k - 1][0];
    if (l->sat > 0.25f) return hsv_hex(l->hue, l->sat, 0.9f * (l->val ? l->val : 1));
    return hsv_hex(d->hue, d->sat, 0.42f * (d->val ? d->val : 1));
  }
  const roster_swatch *t = &e->alts[k - 1][1];
  if (t->tint > 0) return hsv_hex(t->hue, 0.25f + t->tint, 0.85f * (t->val ? t->val : 1));   /* a dye */
  float r = ((e->color >> 16) & 255) / 255.f, g = ((e->color >> 8) & 255) / 255.f, b = (e->color & 255) / 255.f;
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn, h = 0;
  if (d > 1e-4f) { if (mx == r) h = fmodf((g - b) / d + 6, 6); else if (mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4; }
  return hsv_hex(h * 60 + t->hue, (mx > 0 ? d / mx : 0) * (t->sat ? t->sat : 1), mx * (t->val ? t->val : 1));
}
static const char *who(int g) { return modal ? "PICK" : cpu_side(g) ? "CPU" : g ? "P2" : "P1"; }
static void arrow(cv *c, float x, float y, int dir, float s, col_t col) {
  cv_begin(c);
  cv_move(c, x - 5 * s * dir, y - 8 * s); cv_line(c, x + 6 * s * dir, y); cv_line(c, x - 5 * s * dir, y + 8 * s);
  cv_close(c); cv_fill_hex(c, col.rgb, col.a); cv_fill(c);
}
static void panel_paint(int g) {
  const pick_t *p = &PS[g];
  layer *L = &PANEL_L[g];
  L_ensure(L, PANEL_W + 16, PANEL_H + 16);
  cv *c = L->c;
  float X = 0, Y = 0, ix = 16, IW = PANEL_W - 32;
  bool act = side_active(g) && !p->flips;
  c_box(c, X, Y, PANEL_W, PANEL_H, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(slot_col(g)));
  const roster_entry *e = roster_get(p->fighter);
  const weapon_def *W = &WEAPONS[p->weapon];
  /* (a) who, the step, the colours */
  const char *w = who(g);
  float ww = t_width(F_LCD, 18, 0, w) + 14;
  c_rect(c, ix, SWATCH_Y, ww, 26, COL(C_INK));
  t_draw(c, F_LCD, 18, 0, ix + 7, SWATCH_Y + 19, w, COL(C_CREAM));
  const char *sl = step_label(g);
  t_draw(c, F_LCD, 15, 0, ix + ww + 8, SWATCH_Y + 19, sl, p->step == SS_READY ? COL(slot_col(g) == C_PINK ? 0x8a5a00 : 0x2c5c8c) : COL(C_INK));
  for (int k = 0; k < ROSTER_COLOURS; k++) {
    float sx = swatch_x(g, k) - PANEL_X[g], sy = SWATCH_Y + 3;
    bool on = k == colour_of(g), taken = !modal && PS[1 - g].fighter == p->fighter && colour_of(1 - g) == k;
    if (on) { sx -= 2; sy -= 2; c_rect(c, sx + 3, sy + 3, 18, 18, COL(C_INK)); }
    uint32_t col = swatch_rgb(e, k);
    c_rect(c, sx, sy, 18, 18, COL(col));
    c_border(c, sx, sy, 18, 18, 2, COL(C_INK));
    if (taken && !on) { cv_begin(c); cv_move(c, sx + 3, sy + 15); cv_line(c, sx + 15, sy + 3); cv_line_width(c, 2.5f); cv_stroke_hex(c, C_INK, 1); cv_stroke(c); }
  }
  /* (b) the bio */
  char bl[3][160]; int nb = t_wrap(F_TYPE, 14, 0, e->bio, IW, false, bl, 2);
  for (int k = 0; k < nb; k++) t_draw(c, F_TYPE, 14, 0, ix, 58 + k * 18, bl[k], COLA(C_INK, 0.9f));
  /* (c) the weapon: arrows round its name, its tag, the numbers, its EXTREME */
  bool kw = act && p->step == SS_WEAPON;
  float wy = WROW_Y;
  if (kw) c_dash_rect(c, ix - 8, wy - 7, IW + 16, 106, 3, COL(slot_col(g)));
  col_t ac = kw ? COL(C_INK) : COLA(C_INK, 0.25f);
  arrow(c, ix + 6, wy + 12, -1, 1.1f, ac); arrow(c, ix + IW - 6, wy + 12, 1, 1.1f, ac);
  PANEL_HIT_W[g][0] = PANEL_X[g] + ix + 30; PANEL_HIT_W[g][1] = PANEL_X[g] + ix + IW - 30;
  float fs = 20; while (fs > 15 && t_width(F_MARKER, fs, 0, W->name) > IW - 52) fs -= 1;
  float nw = t_width(F_MARKER, fs, 0, W->name);
  t_draw(c, F_MARKER, fs, 0, ix + (IW - nw) / 2, wy + 12 + fs * 0.36f, W->name, COL(C_INK));
  float cy = wy + 26, cx = ix;
  {
    float tw = t_width(F_TYPE, 14, 1.4f, W->tag) + 14;
    c_rect(c, cx, cy, tw, 22, COL(C_INK));
    t_draw(c, F_TYPE, 14, 1.4f, cx + 7, cy + 16, W->tag, COL(C_CREAM));
    cx += tw + 6;
    if (p->weapon == weapon_index(e->weapon)) {
      const char *sg = "SIGNATURE";
      float sw = t_width(F_TYPE, 14, 1.4f, sg) + 14;
      c_box(c, cx, cy, sw, 22, COL(slot_col(g)), 2, COL(C_INK), 0, 0, COL(0));
      t_draw(c, F_TYPE, 14, 1.4f, cx + 7, cy + 16, sg, COL(C_INK));
    }
  }
  float sy = cy + 28, sx = ix;
  for (int k = 0; k < 2; k++) {
    const char *lab = k ? "SPD" : "DMG";
    t_draw(c, F_TYPE, 14, 0, sx, sy + 12, lab, COL(C_INK));
    float v = k ? W->spd / 1.45f : W->dmg / 1.55f;
    float bx = sx + t_width(F_TYPE, 14, 0, lab) + 6;
    c_rect(c, bx, sy + 3, 80, 9, COLA(C_INK, 0.15f));
    c_rect(c, bx, sy + 3, roundf(fminf(1, v) * 80), 9, COL(C_INK));
    sx = bx + 80 + 18;
  }
  char ex[64]; snprintf(ex, sizeof ex, "EXTREME: %s", special_name(W->special));
  float ls = 1.4f; while (ls > 0 && t_width(F_TYPE, 14, ls, ex) > IW) ls -= 0.2f;
  if (t_width(F_TYPE, 14, 0, ex) > IW) snprintf(ex, sizeof ex, "%s", special_name(W->special));
  t_draw(c, F_TYPE, 14, ls, ix, sy + 33, ex, COL(C_INK));
  /* (d) the CPU's level */
  if (cpu_side(g)) {
    float ly = PANEL_H - 32, lx = ix;
    bool kl = act && p->step == SS_LEVEL;
    if (kl) c_dash_rect(c, ix - 8, ly - 5, IW + 16, 34, 3, COL(slot_col(g)));
    for (int i = 0; i < 3; i++) {
      bool on = scr_sel && scr_sel->level == i;
      float lw = t_width(F_TYPE, 14, 0, LEVELS[i]) + 18;
      c_box(c, lx, ly, lw, 24, on ? COL(C_INK) : COL(C_PAPER), 2, COL(C_INK), 0, 0, COL(0));
      t_draw(c, F_TYPE, 14, 0, lx + 9, ly + 17, LEVELS[i], on ? COL(C_CREAM) : COL(C_INK));
      lx += lw + 6;
    }
  }
  if (!act && !p->flips && p->step != SS_READY) c_rect(c, 4, 4, PANEL_W - 8, PANEL_H - 8, COLA(C_CREAM, 0.35f));
  dirty_panel[g] = false;
}

/* the name plate under a fighter: the title in a tilted chip, the name large */
static void plate_paint(int g) {
  const pick_t *p = &PS[g];
  layer *L = &PLATE_L[g];
  L_ensure(L, 360, 110);
  cv *c = L->c;
  bool rnd = (p->step == SS_FIGHTER && p->cell == RANDOM_CELL) || p->flips;
  const roster_entry *e = roster_get(p->flips ? p->cell : p->fighter);
  const char *title = rnd && !p->flips ? "ANYONE" : e->title, *name = rnd && !p->flips ? "RANDOM" : e->name;
  float tw = t_width(F_TYPE, 14, 4.48f, title) + 24;
  cv_save(c);
  cv_translate(c, 180, 26); cv_rotate(c, -2 * CK_PI / 180);
  c_box(c, -tw / 2, -14, tw, 28, COL(C_CREAM), 3, COL(C_INK), 5, 5, COL(slot_col(g)));
  t_draw(c, F_TYPE, 14, 4.48f, -tw / 2 + 12, 5, title, COL(C_INK));
  cv_restore(c);
  float fs = 44; while (fs > 26 && t_width(F_MARKER, fs, 0.4f, name) > 330) fs -= 1;
  float nw = t_width(F_MARKER, fs, 0.4f, name);
  tshadow sh = {5, {{5, 5, COL(slot_col(g))}, {-2, -2, COL(C_INK)}, {2, -2, COL(C_INK)}, {-2, 2, COL(C_INK)}, {2, 2, COL(C_INK)}}};
  t_draw_sh(c, F_MARKER, fs, 0.4f, 180 - nw / 2, 92, name, COL(C_CREAM), &sh);
  dirty_plate[g] = false;
}

/* the cursor's tab: P1, P2 or CPU on the side's colour; READY on ink */
static void tab_paint(int g) {
  char s[16];
  snprintf(s, sizeof s, "%s%s", who(g), !modal && PS[g].step == SS_READY ? " READY" : "");
  if (TAB_L[g].c && !strcmp(s, tab_s[g])) return;
  snprintf(tab_s[g], sizeof tab_s[g], "%s", s);
  float w = t_width(F_LCD, 16, 0, s) + 16;
  L_ensure(&TAB_L[g], (int)w + 2, 26);
  bool rd = !modal && PS[g].step == SS_READY;
  c_box(TAB_L[g].c, 0, 0, w, 24, rd ? COL(C_INK) : COL(slot_col(g)), 3, COL(C_INK), 0, 0, COL(0));
  t_draw(TAB_L[g].c, F_LCD, 16, 0, 8, 18, s, rd ? COL(C_CREAM) : COL(C_INK));
}

static void hint_update(void) {
  static const tshadow SH = {1, {{1, 1, COL(C_INK)}}};
  hint_style st = {15, 0.6f, COL(C_CREAM), &SH};
  const char *a, *b = "";
  if (modal) a = hint_pick("{A} PICK   {B} BACK", "[ENTER] PICK   [ESC] BACK", "<CLICK> PICK   <RIGHT CLICK> BACK", "<LIGHT> PICK   <KICK> BACK");
  else if (solo())
    a = hint_pick("{A} PICK  {B} BACK  {Y} SWAP  {LB}{RB} COLOUR  {START} GO",
                  "[ENTER] PICK  [ESC] BACK  [TAB] SWAP  [E] COLOUR",
                  "<CLICK> PICK   <RIGHT CLICK> BACK",
                  "<LIGHT> PICK  <KICK> BACK  <HEAVY> COLOUR  <EXTREME> GO");
  else {
    a = hint_pick("{A} PICK  {B} BACK  {Y} SWAP  {LB}{RB} COLOUR  {START} GO",
                  "P1 [Q] PICK [R] BACK [E] COLOUR   P2 [,] PICK [/] BACK [.] COLOUR",
                  "<CLICK> PICK   <RIGHT CLICK> BACK",
                  "<LIGHT> PICK  <KICK> BACK  <HEAVY> COLOUR  <EXTREME> GO");
    if (input_last_dev == DEV_KEYS) b = "P1 [W][A][S][D]   P2 ARROWS   [TAB] SWAP   [ESC] UNDO";
  }
  /* the line fits under the grid: a long one sets smaller, never under 14 */
  hint_style sa = st, sb = st;
  while (sa.fs > 14 && hint_width(&sa, a) > 7 * (CELL + GAP)) sa.fs -= 0.5f;
  while (sb.fs > 14 && hint_width(&sb, b) > 7 * (CELL + GAP)) sb.fs -= 0.5f;
  hint_line_set(&HINT[0], &sa, a);
  hint_line_set(&HINT[1], &sb, b);
}

/* ---------------------------------------------------------------- the screen */
void select_init(void) {
  portraits_load();
}
static void reset_from_sel(void) {
  memset(PS, 0, sizeof PS);
  for (int g = 0; g < 2; g++) {
    pick_t *p = &PS[g];
    int f = scr_sel ? roster_index(scr_sel->slots[g].ch) : g; if (f < 0) f = g;
    int w = scr_sel ? weapon_index(scr_sel->slots[g].weapon) : -1;
    p->fighter = f; p->weapon = w >= 0 ? w : weapon_index(ROSTER[f].weapon);
    if (p->weapon < 0) p->weapon = 0;
    p->colour = scr_sel ? scr_sel->slots[g].colour : 0;
    p->step = SS_FIGHTER;
    cursor_to(g, f, true);
  }
  nhist = 0; go_t = -1; hover_cell = -1; seq_n = 0;
  TAB_L[0].w = TAB_L[1].w = 0; tab_s[0][0] = tab_s[1][0] = 0;
  dirty_all();
}
static bool returning;
static double shown_t;   /* when the select was shown: a hold of back counts only if it began later */
void ui_select_return(void) { returning = true; }
void select_show(void) {
  modal = false; shown_t = ck_now();
  if (returning && PS[0].step == SS_READY && PS[1].step == SS_READY) {   /* back from the stage: undo the last step */
    returning = false;
    go_t = -1;
    back(nhist ? hist[nhist - 1] : 1);
    for (int g = 0; g < 2; g++) { cursor_to(g, PS[g].cell, true); write_sel(g, true); }
    tab_s[0][0] = tab_s[1][0] = 0;
    dirty_all();
    return;
  }
  returning = false;
  reset_from_sel();
  for (int g = 0; g < 2; g++) write_sel(g, true);
}
/* the grid alone, for the tourney's rows and the online screen: done(fighter) or done(-1) */
void select_modal_open(int fighter, const char *title, void (*done)(int fighter)) {
  modal = true; modal_done = done;
  snprintf(modal_title, sizeof modal_title, "%s", title ? title : "");
  GRID_X = 350; GRID_Y = 282;
  memset(PS, 0, sizeof PS);
  PS[0].fighter = fighter >= 0 && fighter < ROSTER_N ? fighter : 0;
  PS[0].weapon = weapon_index(ROSTER[PS[0].fighter].weapon); if (PS[0].weapon < 0) PS[0].weapon = 0;
  PS[1].cell = -1; PS[1].fighter = -1;
  cursor_to(0, PS[0].fighter, true);
  hover_cell = -1; tab_s[0][0] = 0;
  dirty_all();
}
#define MODAL_CARD_X 330
#define MODAL_CARD_Y 222
bool select_modal_active(void) { return modal; }
void select_modal_close(void) { modal = false; modal_done = NULL; GRID_X = 350; GRID_Y = 492; dirty_all(); }
/* the modal's card: what it picks for, the grid's place, the fighter under the cursor */
static void modal_card_paint(void) {
  layer *L = &PLATE_L[1];
  L_ensure(L, 640, 330);
  cv *c = L->c;
  c_box(c, 0, 0, 620, 312, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  t_draw(c, F_TYPE, 15, 2.4f, 20, 30, "PICK A FIGHTER", COL(C_INK));
  float tw = t_width(F_TYPE, 15, 1.5f, modal_title);
  t_draw(c, F_TYPE, 15, 1.5f, 600 - tw, 30, modal_title, COLA(C_INK, 0.75f));
  c_rect(c, 20, 40, 580, 3, COL(C_INK));
  const pick_t *p = &PS[0];
  bool rnd = p->cell == RANDOM_CELL || p->flips;
  const roster_entry *e = roster_get(p->flips ? p->cell : p->fighter);
  const char *nm = rnd && !p->flips ? "RANDOM" : e->name, *ti = rnd && !p->flips ? "ANYONE" : e->title;
  float fs = 34; while (fs > 22 && t_width(F_MARKER, fs, 0, nm) > 380) fs -= 1;
  t_draw(c, F_MARKER, fs, 0, 20, 294, nm, COL(C_INK));
  float w2 = t_width(F_TYPE, 15, 2.4f, ti);
  t_draw(c, F_TYPE, 15, 2.4f, 600 - w2, 290, ti, COLA(C_INK, 0.75f));
  dirty_plate[1] = false;
}

void select_frame(float dt) {
  /* the pads and the phone pads, by player */
  const menu_ev *e; int n = input_menu_events(&e);
  for (int i = 0; i < n; i++) sel_event(e[i].player, e[i].ev);
  /* RANDOM flips through six fighters in half a second, then lands on one the other side has not */
  for (int g = 0; g < 2; g++) {
    pick_t *p = &PS[g];
    if (!p->flips) continue;
    p->flip_t -= dt;
    if (p->flip_t > 0) continue;
    p->flip_t = 0.5f / 6;
    if (--p->flips > 0) {
      int f; do f = (int)(ck_rand() * ROSTER_N); while (f == p->cell && ROSTER_N > 1);
      cursor_to(g, f, false); sfx_ui();
      dirty_plate[g] = dirty_panel[g] = true;
    } else {
      int other = modal ? -1 : PS[1 - g].fighter, f;
      do f = (int)(ck_rand() * ROSTER_N); while ((f == other || f == p->fighter) && ROSTER_N > 2);
      sfx_ui_ok(); land(g, f);
    }
  }
  /* both READY: a beat, then the stage */
  if (go_t >= 0) {
    go_t -= dt;
    if (go_t < 0) { go_t = -1; if (ui_cb.act) ui_cb.act("stage"); return; }
  }
  /* hold back to leave */
  if (input_back_held(shown_t) >= 0.6f && !modal) { to_title(); return; }
  hint_update();
}

void select_draw(void) {
  if (dirty_grid) grid_paint();
  int ng = modal ? 1 : 2;
  for (int g = 0; g < ng; g++) {
    if (dirty_panel[g] && !modal) panel_paint(g);
    if (dirty_plate[g] && !modal) plate_paint(g);
    tab_paint(g);
  }
  if (modal) {
    if (dirty_plate[0] || dirty_plate[1] || !PLATE_L[1].c) { modal_card_paint(); dirty_plate[0] = false; }
    ov_fill(0, 0, CK_W, CK_H, 0x0a0a0a, 0.62f);
    L_upload(&PLATE_L[1]); ov_draw(PLATE_L[1].t, MODAL_CARD_X, MODAL_CARD_Y, 1);
  }
  /* the plates under the fighters */
  if (!modal) for (int g = 0; g < 2; g++) { L_upload(&PLATE_L[g]); ov_draw(PLATE_L[g].t, BODY_X[g] - 180, PLATE_BASE - 92, 1); }
  /* the grid */
  L_upload(&GRID_L); ov_draw(GRID_L.t, GRID_X - 6, GRID_Y - 6, 1);
  /* the cursors: rings outside the cells, P1's outside P2's when they share one */
  bool same = !modal && PS[0].cell == PS[1].cell;
  for (int g = ng - 1; g >= 0; g--) {
    pick_t *p = &PS[g];
    if (p->cell < 0) continue;
    float x = tr_get(&p->cx, ui_now) - 3, y = tr_get(&p->cy, ui_now) - 3;   /* the lifted cell */
    float o = 4 + (same && g == 0 ? 4 : 0), w = CELL + 2 * o, h = CELL + 2 * o;
    float a = side_active(g) || p->step == SS_READY ? 1 : 0.5f;
    uint32_t col = slot_col(g);
    ov_fill(x - o, y - o, w, 4, col, a); ov_fill(x - o, y - o + h - 4, w, 4, col, a);
    ov_fill(x - o, y - o, 4, h, col, a); ov_fill(x - o + w - 4, y - o, 4, h, col, a);
    L_upload(&TAB_L[g]);
    float tw = (float)TAB_L[g].w - 2;
    float tx = g == 0 ? x - o : x - o + w - tw, ty = y - o - 24;
    /* the tabs sit at opposite corners; a READY tab is wide, so on a shared cell P2's sits above P1's */
    if (same && g == 1 && tw + TAB_L[0].w > w) ty -= 22;
    ov_draw(TAB_L[g].t, tx, ty, a);
  }
  if (!modal) for (int g = 0; g < 2; g++) { L_upload(&PANEL_L[g]); ov_draw(PANEL_L[g].t, PANEL_X[g], PANEL_Y, 1); }
  /* the hint under the grid (the modal's, under its card) */
  float hy = modal ? MODAL_CARD_Y + 312 + 36 : GRID_Y + 2 * CELL + GAP + 22;
  if (HINT[1].s[0]) { hint_line_draw(&HINT[0], 640, hy - 1, 1); hint_line_draw(&HINT[1], 640, hy + 23, 1); }
  else hint_line_draw(&HINT[0], 640, hy + 8, 1);
  /* BACK, filling while it is held */
  float hb = input_back_held(shown_t);
  if (hb > 0.12f && !modal) ov_fill(back_b.x, back_b.y + back_b.h - 6, back_b.w * fminf(1, hb / 0.6f), 6, C_PINK, 1);
}
