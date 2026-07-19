// Pebblefocus — Autofocus-style checklist for Pebble Time 2 (Emery)
// Storage lives phone-side (pkjs localStorage); watch holds a synced
// working copy in RAM. See README for the full control map.
//
// Buttons (Active list):
//   UP/DOWN click .... move focus (scrolls)
//   UP long .......... add item via dictation
//   SELECT click ..... check off focused item (records to Done;
//                      reload-flagged items also re-append)
//   SELECT long ...... toggle item's reload flag
//   DOWN long ........ open Done list
//   BACK ............. exit app
// Touch (Active list):
//   Tap item ......... focus + dot/undot
//   Swipe up/down .... scroll
//   Swipe right/left . cycle item color forward/back
// Buttons (Done list):
//   UP/DOWN click .... move focus
//   SELECT click ..... copy item back to end of Active list
//   DOWN long ........ Clear All (second hold confirms)
//   BACK ............. return to Active list

#include <pebble.h>

#define MAX_ITEMS 50
#define MAX_TEXT 40
#define ROW_H 38
#define CHECKBOX_W 34
#define USE_DICTATION_STUB 0   // 1 = emulator fake items; 0 = real Dictation API
// Touch gesture tuning (on-wrist adjustment expected)
#define TOUCH_TAP_SLOP   12    // max px of movement still counted as a tap
#define TOUCH_HSWIPE_MIN 25    // min horizontal px for a color-cycle swipe

// ── Colors (indices match swipe-cycle order) ─────────────────────
typedef enum {
  COL_PURPLE = 0, COL_BLUE, COL_GREEN, COL_YELLOW,
  COL_AQUA, COL_PINK, COL_RED, COL_WHITE, COL_COUNT
} ItemColor;

static GColor color_of(ItemColor c) {
  switch (c) {
    case COL_PURPLE: return GColorVividViolet;
    case COL_BLUE:   return GColorBlue;
    case COL_GREEN:  return GColorIslamicGreen;
    case COL_YELLOW: return GColorYellow;
    case COL_AQUA:   return GColorCyan;
    case COL_PINK:   return GColorShockingPink;
    case COL_RED:    return GColorRed;
    default:         return GColorWhite;
  }
}

// ── Data model ───────────────────────────────────────────────────
typedef struct {
  char text[MAX_TEXT];
  ItemColor color;
  bool dotted;      // selected for this pass
  bool reappend;    // check-off destination: false = Done (default)
} Item;

static Item s_active[MAX_ITEMS];
static int  s_active_count = 0;
static Item s_done[MAX_ITEMS];      // rolling: newest 50 kept
static int  s_done_count = 0;

typedef enum { VIEW_ACTIVE, VIEW_DONE } View;
static View s_view = VIEW_ACTIVE;
static int  s_focus = 0;
static int  s_scroll_top = 0;       // index of first visible row
static bool s_confirm_clear = false;
static GBitmap *s_reload_icon = NULL;
static int  s_pending_check = -1;   // row showing checkmark beat, -1 = none
static AppTimer *s_check_timer = NULL;
static bool s_synced = false;       // phone sync complete
static char s_time_buf[6] = "";     // header clock "HH:MM"

// ── AppMessage protocol ──────────────────────────────────────────
// Commands, phone → watch
#define CMD_ITEM           1   // one item: LIST, INDEX, TEXT, COLOR, FLAGS
#define CMD_SYNC_COMPLETE  2
#define CMD_SYNC_START     3   // clear watch lists; re-stream replaces
// Commands, watch → phone (semantic deltas; pkjs mirrors the same logic)
#define CMD_CHECKOFF      10   // INDEX
#define CMD_TOGGLE_DOT    11   // INDEX
#define CMD_TOGGLE_REAPP  12   // INDEX
#define CMD_ADD           13   // TEXT (arrives white)
#define CMD_COPY_BACK     14   // INDEX (done list)
#define CMD_CLEAR_DONE    15
#define CMD_SET_COLOR     16   // INDEX + COLOR (touch color cycling)
// FLAGS bits
#define FLAG_DOTTED   1
#define FLAG_REAPPEND 2

static void send_delta(int cmd, int index, const char *text) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int32(iter, MESSAGE_KEY_CMD, cmd);
  dict_write_int32(iter, MESSAGE_KEY_INDEX, index);
  if (text) dict_write_cstring(iter, MESSAGE_KEY_TEXT, text);
  app_message_outbox_send();
}

static void send_color_delta(int index, int color) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int32(iter, MESSAGE_KEY_CMD, CMD_SET_COLOR);
  dict_write_int32(iter, MESSAGE_KEY_INDEX, index);
  dict_write_int32(iter, MESSAGE_KEY_COLOR, color);
  app_message_outbox_send();
}

// ── Model operations ─────────────────────────────────────────────
static void add_active(const char *text, ItemColor c) {
  if (s_active_count >= MAX_ITEMS) return;   // cap: reject
  Item *it = &s_active[s_active_count++];
  strncpy(it->text, text, MAX_TEXT - 1);
  it->text[MAX_TEXT - 1] = '\0';
  it->color = c;
  it->dotted = false;
  it->reappend = false;
}

static void push_done(Item *src) {
  if (s_done_count >= MAX_ITEMS) {           // rolling: drop oldest
    memmove(&s_done[0], &s_done[1], sizeof(Item) * (MAX_ITEMS - 1));
    s_done_count = MAX_ITEMS - 1;
  }
  s_done[s_done_count] = *src;
  s_done[s_done_count].dotted = false;
  s_done_count++;
}

static void check_off(int idx) {
  if (idx < 0 || idx >= s_active_count) return;
  Item done_copy = s_active[idx];
  bool reappend = s_active[idx].reappend;
  // remove from active
  memmove(&s_active[idx], &s_active[idx + 1],
          sizeof(Item) * (s_active_count - idx - 1));
  s_active_count--;
  push_done(&done_copy);                     // every check-off records a pass
  if (reappend) {
    done_copy.dotted = false;
    // reappend flag deliberately retained: recurring until untoggled
    if (s_active_count < MAX_ITEMS) s_active[s_active_count++] = done_copy;
  }
  if (s_focus >= s_active_count) s_focus = s_active_count - 1;
  if (s_focus < 0) s_focus = 0;
}

static void copy_back(int idx) {
  if (idx < 0 || idx >= s_done_count) return;
  if (s_active_count >= MAX_ITEMS) return;   // cap: reject
  Item copy = s_done[idx];                   // stays in Done
  copy.dotted = false;
  copy.reappend = false;
  s_active[s_active_count++] = copy;
}

// ── Rendering ────────────────────────────────────────────────────
static Window *s_window;
static Layer  *s_list_layer;

static void clamp_scroll(int count, int visible_rows) {
  if (s_focus < s_scroll_top) s_scroll_top = s_focus;
  if (s_focus >= s_scroll_top + visible_rows)
    s_scroll_top = s_focus - visible_rows + 1;
  if (s_scroll_top < 0) s_scroll_top = 0;
  (void)count;
}

static void draw_row(GContext *ctx, GRect bounds, Item *it, int y,
                     bool focused, bool done_view, bool pending) {
  GRect row = GRect(0, y, bounds.size.w, ROW_H);

  // focus highlight: black border
  if (focused) {
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_rect(ctx, grect_inset(row, GEdgeInsets(1)));
  }

  // checkbox area: filled with item color
  GRect box_area = GRect(2, y + 2, CHECKBOX_W, ROW_H - 4);
  graphics_context_set_fill_color(ctx, color_of(it->color));
  graphics_fill_rect(ctx, box_area, 2, GCornersAll);

  // checkbox square (white with black border) centered in area
  GRect box = GRect(box_area.origin.x + (CHECKBOX_W - 20) / 2,
                    y + (ROW_H - 20) / 2, 20, 20);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, box, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_rect(ctx, box);

  // dot inside checkbox (this-pass marker); checkmark in Done view
  // and during the pending check-off beat
  if (done_view || pending) {
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, GPoint(box.origin.x + 4, box.origin.y + 10),
                            GPoint(box.origin.x + 8, box.origin.y + 15));
    graphics_draw_line(ctx, GPoint(box.origin.x + 8, box.origin.y + 15),
                            GPoint(box.origin.x + 16, box.origin.y + 4));
  } else if (it->dotted) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(box.origin.x + 10, box.origin.y + 10), 5);
  }

  // re-append flag: reload icon, vertically centered in-line
  int text_right = bounds.size.w - 4;
  if (!done_view && it->reappend && s_reload_icon) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_reload_icon,
      GRect(bounds.size.w - 20, y + (ROW_H - 16) / 2, 16, 16));
    text_right -= 20;
  }

  // item text
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, it->text,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(CHECKBOX_W + 6, y + 4, text_right - (CHECKBOX_W + 6), ROW_H - 8),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void list_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  bool done_view = (s_view == VIEW_DONE);
  Item *items = done_view ? s_done : s_active;
  int count   = done_view ? s_done_count : s_active_count;

  // header bar
  int header_h = 22;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, header_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  char header[32];
  if (s_confirm_clear) {
    snprintf(header, sizeof(header), "CLEAR ALL? DN-hold=yes");
  } else if (done_view) {
    snprintf(header, sizeof(header), "DONE %d", count);
  } else {
    int dotted = 0;
    for (int i = 0; i < count; i++) if (s_active[i].dotted) dotted++;
    snprintf(header, sizeof(header), "\xE2\x80\xA2%d/%d", dotted, count);
  }
  graphics_draw_text(ctx, header,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(4, 0, bounds.size.w - 8, header_h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // clock, right-aligned
  char right[16];
  snprintf(right, sizeof(right), "%s", s_time_buf);
  graphics_draw_text(ctx, right,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(4, 0, bounds.size.w - 8, header_h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  if (count == 0) {
    graphics_context_set_text_color(ctx, GColorBlack);
    const char *msg = !s_synced ? "Syncing..." :
      (done_view ? "Done list empty" : "List empty\nUP-hold: dictate");
    graphics_draw_text(ctx, msg,
      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(8, 60, bounds.size.w - 16, 80),
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  int visible_rows = (bounds.size.h - header_h) / ROW_H;
  clamp_scroll(count, visible_rows);

  for (int i = 0; i < visible_rows; i++) {
    int idx = s_scroll_top + i;
    if (idx >= count) break;
    draw_row(ctx, bounds, &items[idx], header_h + i * ROW_H,
             idx == s_focus, done_view,
             (!done_view && idx == s_pending_check));
  }
}

// ── Dictation (real device; emulator falls back to stub) ─────────
static void refresh(void);
static DictationSession *s_dictation = NULL;

static void dictation_cb(DictationSession *session,
                         DictationSessionStatus status,
                         char *transcription, void *ctx) {
  if (status != DictationSessionStatusSuccess || !transcription) return;
  int before = s_active_count;
  add_active(transcription, COL_WHITE);    // dictated items arrive white
  if (s_active_count == before) {
    vibes_short_pulse();                   // cap hit: reject
  } else {
    send_delta(CMD_ADD, 0, s_active[s_active_count - 1].text);
    s_focus = s_active_count - 1;          // jump focus to new item
  }
  refresh();
}

// ── Input ────────────────────────────────────────────────────────
static void refresh(void) { layer_mark_dirty(s_list_layer); }

// Complete a pending check-off immediately (called by timer, or by any
// button press during the beat — prevents lockout if the timer stalls).
static void complete_pending_check(void) {
  if (s_pending_check < 0) return;
  if (s_check_timer) { app_timer_cancel(s_check_timer); s_check_timer = NULL; }
  int idx = s_pending_check;
  s_pending_check = -1;
  send_delta(CMD_CHECKOFF, idx, NULL);   // before local mutation shifts indices
  check_off(idx);
}

static void check_beat_fire(void *data) {
  s_check_timer = NULL;
  complete_pending_check();
  refresh();
}

static int current_count(void) {
  return (s_view == VIEW_DONE) ? s_done_count : s_active_count;
}

static void up_click(ClickRecognizerRef r, void *ctx) {
  complete_pending_check();
  s_confirm_clear = false;
  if (s_focus > 0) s_focus--;
  refresh();
}

static void down_click(ClickRecognizerRef r, void *ctx) {
  complete_pending_check();
  s_confirm_clear = false;
  if (s_focus < current_count() - 1) s_focus++;
  refresh();
}

static void select_click(ClickRecognizerRef r, void *ctx) {
  complete_pending_check();
  s_confirm_clear = false;
  if (s_view == VIEW_ACTIVE) {
    if (s_focus < s_active_count) {
      s_pending_check = s_focus;           // show checkmark beat
      s_check_timer = app_timer_register(500, check_beat_fire, NULL);
    }
  } else {
    if (s_focus < s_done_count && s_active_count < MAX_ITEMS)
      send_delta(CMD_COPY_BACK, s_focus, NULL);
    copy_back(s_focus);
  }
  refresh();
}

static void select_long(ClickRecognizerRef r, void *ctx) {
  complete_pending_check();
  if (s_view == VIEW_ACTIVE && s_focus < s_active_count) {
    s_active[s_focus].reappend = !s_active[s_focus].reappend;
    send_delta(CMD_TOGGLE_REAPP, s_focus, NULL);
    refresh();
  }
}

static void up_long(ClickRecognizerRef r, void *ctx) {
  complete_pending_check();
  if (s_view != VIEW_ACTIVE) return;
#if !USE_DICTATION_STUB
  if (s_dictation) dictation_session_start(s_dictation);
#else
  // EMULATOR STUB — QEMU has no microphone. Set USE_DICTATION_STUB 1.
  static int dict_counter = 0;
  char buf[MAX_TEXT];
  snprintf(buf, sizeof(buf), "Dictated item %d", ++dict_counter);
  int before = s_active_count;
  add_active(buf, COL_WHITE);              // dictated items arrive white
  if (s_active_count == before) {
    vibes_short_pulse();                   // cap hit: reject
  } else {
    send_delta(CMD_ADD, 0, buf);
    s_focus = s_active_count - 1;          // jump focus to new item;
  }                                        // clamp_scroll brings it into view
  refresh();
#endif
}

static void down_long(ClickRecognizerRef r, void *ctx) {
  complete_pending_check();
  if (s_view == VIEW_ACTIVE) {
    s_view = VIEW_DONE;
    s_focus = 0; s_scroll_top = 0;
  } else if (!s_confirm_clear) {
    s_confirm_clear = true;                // first hold: arm confirm
  } else {
    s_done_count = 0;                      // second hold: clear
    send_delta(CMD_CLEAR_DONE, 0, NULL);
    s_confirm_clear = false;
    s_focus = 0; s_scroll_top = 0;
  }
  refresh();
}

static void back_click(ClickRecognizerRef r, void *ctx) {
  complete_pending_check();
  if (s_view == VIEW_DONE) {
    s_view = VIEW_ACTIVE;
    s_confirm_clear = false;
    s_focus = 0; s_scroll_top = 0;
    refresh();
  } else {
    window_stack_pop(true);                // exit app
  }
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long, NULL);
  window_long_click_subscribe(BUTTON_ID_UP, 500, up_long, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, down_long, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ── Touch (PBL_TOUCH platforms; gestures built on raw events) ────
#ifdef PBL_TOUCH
#define LIST_HEADER_H 22
static bool s_touch_down = false;
static int16_t s_touch_sx, s_touch_sy;   // touchdown position
static int16_t s_touch_cx, s_touch_cy;   // latest position

static int row_at_y(int16_t y, int count) {
  if (y < LIST_HEADER_H) return -1;
  int idx = s_scroll_top + (y - LIST_HEADER_H) / ROW_H;
  return (idx >= 0 && idx < count) ? idx : -1;
}

static void touch_handler(const TouchEvent *event, void *context) {
  if (event->type == TouchEvent_Touchdown) {
    s_touch_down = true;
    s_touch_sx = s_touch_cx = event->x;
    s_touch_sy = s_touch_cy = event->y;
    return;
  }
  if (event->type == TouchEvent_PositionUpdate) {
    if (s_touch_down) { s_touch_cx = event->x; s_touch_cy = event->y; }
    return;
  }
  if (event->type != TouchEvent_Liftoff || !s_touch_down) return;
  s_touch_down = false;

  int dx = s_touch_cx - s_touch_sx;
  int dy = s_touch_cy - s_touch_sy;
  int adx = dx < 0 ? -dx : dx;
  int ady = dy < 0 ? -dy : dy;
  int count = current_count();

  complete_pending_check();
  s_confirm_clear = false;

  if (adx <= TOUCH_TAP_SLOP && ady <= TOUCH_TAP_SLOP) {
    // TAP: focus the row; in Active also toggle its dot. Inert in Done.
    int idx = row_at_y(s_touch_sy, count);
    if (idx < 0) return;
    if (s_view == VIEW_ACTIVE) {
      s_focus = idx;
      s_active[idx].dotted = !s_active[idx].dotted;
      send_delta(CMD_TOGGLE_DOT, idx, NULL);
      refresh();
    }
    return;
  }

  if (ady >= adx) {
    // VERTICAL SWIPE: discrete scroll by rows moved; focus follows.
    int rows = dy / ROW_H;
    if (rows == 0) rows = (dy > 0) ? 1 : -1;
    // finger down (dy>0) reveals earlier items
    s_focus -= rows;
    if (s_focus < 0) s_focus = 0;
    if (s_focus > count - 1) s_focus = count - 1;
    refresh();
    return;
  }

  // HORIZONTAL SWIPE: cycle color of the row where the touch began.
  if (adx < TOUCH_HSWIPE_MIN || s_view != VIEW_ACTIVE) return;
  int idx = row_at_y(s_touch_sy, count);
  if (idx < 0) return;
  s_focus = idx;
  int c = (int)s_active[idx].color;
  c = (dx > 0) ? (c + 1) % COL_COUNT : (c + COL_COUNT - 1) % COL_COUNT;
  s_active[idx].color = (ItemColor)c;
  send_color_delta(idx, c);
  refresh();
}
#endif

// ── AppMessage inbox (phone → watch) ─────────────────────────────
static void inbox_received(DictionaryIterator *iter, void *ctx) {
  Tuple *cmd_t = dict_find(iter, MESSAGE_KEY_CMD);
  if (!cmd_t) return;
  int cmd = cmd_t->value->int32;

  if (cmd == CMD_SYNC_START) {
    s_active_count = 0;
    s_done_count = 0;
    s_focus = 0; s_scroll_top = 0;
    s_synced = false;
    refresh();
  } else if (cmd == CMD_ITEM) {
    Tuple *list_t  = dict_find(iter, MESSAGE_KEY_LIST);
    Tuple *text_t  = dict_find(iter, MESSAGE_KEY_TEXT);
    Tuple *color_t = dict_find(iter, MESSAGE_KEY_COLOR);
    Tuple *flags_t = dict_find(iter, MESSAGE_KEY_FLAGS);
    if (!text_t) return;
    bool to_done = list_t && list_t->value->int32 == 1;
    Item *arr = to_done ? s_done : s_active;
    int  *cnt = to_done ? &s_done_count : &s_active_count;
    if (*cnt >= MAX_ITEMS) return;
    Item *it = &arr[(*cnt)++];
    strncpy(it->text, text_t->value->cstring, MAX_TEXT - 1);
    it->text[MAX_TEXT - 1] = '\0';
    it->color = color_t ? (ItemColor)color_t->value->int32 : COL_WHITE;
    int flags = flags_t ? flags_t->value->int32 : 0;
    it->dotted   = flags & FLAG_DOTTED;
    it->reappend = flags & FLAG_REAPPEND;
    refresh();
  } else if (cmd == CMD_SYNC_COMPLETE) {
    s_synced = true;
    refresh();
  }
}

// ── App lifecycle ────────────────────────────────────────────────
static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_reload_icon = gbitmap_create_with_resource(RESOURCE_ID_RELOAD_ICON);
  s_dictation = dictation_session_create(MAX_TEXT, dictation_cb, NULL);
  if (s_dictation) dictation_session_enable_confirmation(s_dictation, true);
  s_list_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_list_layer, list_update_proc);
  layer_add_child(root, s_list_layer);
}

static void window_appear(Window *w) {
#ifdef PBL_TOUCH
  touch_service_subscribe(touch_handler, NULL);   // sensor draws power:
#endif                                            // only while app visible
}

static void window_disappear(Window *w) {
#ifdef PBL_TOUCH
  touch_service_unsubscribe();
#endif
}

static void window_unload(Window *w) {
  layer_destroy(s_list_layer);
  gbitmap_destroy(s_reload_icon);
  if (s_dictation) dictation_session_destroy(s_dictation);
}

// ── Clock ────────────────────────────────────────────────────────
static void update_time(struct tm *t) {
  strftime(s_time_buf, sizeof(s_time_buf),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  if (!clock_is_24h_style() && s_time_buf[0] == '0')
    memmove(s_time_buf, s_time_buf + 1, strlen(s_time_buf)); // "07:45"→"7:45"
  if (s_list_layer) layer_mark_dirty(s_list_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  update_time(tick_time);
}

int main(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload,
    .appear = window_appear, .disappear = window_disappear });
  app_message_register_inbox_received(inbox_received);
  app_message_open(128, 128);
  time_t now = time(NULL);
  update_time(localtime(&now));
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  window_stack_push(s_window, true);
  app_event_loop();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
  return 0;
}