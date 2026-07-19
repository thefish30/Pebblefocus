// Pebblefocus pkjs — phone storage + Clay config
console.log("Pebblefocus index.js LOADED - release 1.0");

var Clay = require('pebble-clay');

var HARD_MAX = 50;   // watch RAM arrays are sized to this; cap slider <= this

// Commands (must match main.c)
var CMD_ITEM = 1, CMD_SYNC_COMPLETE = 2, CMD_SYNC_START = 3;
var CMD_CHECKOFF = 10, CMD_TOGGLE_DOT = 11, CMD_TOGGLE_REAPP = 12,
    CMD_ADD = 13, CMD_COPY_BACK = 14, CMD_CLEAR_DONE = 15, CMD_SET_COLOR = 16;
var FLAG_DOTTED = 1, FLAG_REAPPEND = 2;

// Color indices match main.c enum:
// 0 purple, 1 blue, 2 green, 3 yellow, 4 aqua, 5 pink, 6 red, 7 white
var COLOR_PREFIX = { u: 0, b: 1, g: 2, y: 3, a: 4, p: 5, r: 6, w: 7 };

// ── Storage ──────────────────────────────────────────────────────
function load(key, fallback) {
  try {
    var raw = localStorage.getItem(key);
    return raw !== null ? JSON.parse(raw) : fallback;
  } catch (e) { return fallback; }
}
function save(key, val) { localStorage.setItem(key, JSON.stringify(val)); }

function getActive() { return load("pf_active", []); }
function getDone()   { return load("pf_done", []); }
function setActive(a) { save("pf_active", a); }
function setDone(d)   { save("pf_done", d); }

// ── Clay config ──────────────────────────────────────────────────
var clayConfig = [
  { type: "heading", defaultValue: "Pebblefocus" },
  {
    type: "section",
    items: [
      { type: "heading", defaultValue: "List entry", size: 4 },
      {
        type: "textarea",          // custom component below
        messageKey: "PASTE",
        label: "Paste list (one item per line)",
        defaultValue: ""
      },
      {
        type: "text",
       defaultValue: "Color prefixes stick until the next prefix:" +
          "<br>y: yellow" +
          "<br>g: green" +
          "<br>u: purple" +
          "<br>b: blue" +
          "<br>a: aqua" +
          "<br>p: pink" +
          "<br>r: red" +
          "<br>w: white"
      },
      {
        type: "toggle",
        messageKey: "MODE",
        label: "Replace current list (off = append)",
        defaultValue: false
      }
    ]
  },
  {
    type: "section",
    items: [
      { type: "heading", defaultValue: "Watch controls", size: 4 },
      {
        type: "text",
        defaultValue:
        "ACTIVE LIST:" +
          "<br>Tap item: dot / undot" +
          "<br>Swipe up/down: scroll" +
          "<br>Swipe right/left on item: change color" +
          "<br>UP/DOWN: move focus" +
          "<br>UP hold: add by voice" +
          "<br>SELECT: check off" +
          "<br>SELECT hold: toggle repeat (&#8635;)" +
          "<br>DOWN hold: open Done list" +
          "<br>BACK: exit" +
        "<br><br>DONE LIST:" +
          "<br>SELECT: copy item back to list" +
          "<br>DOWN hold &times;2: clear all" +
          "<br>BACK: return"
      }
    ]
  },
  { type: "submit", defaultValue: "Save" }
];

// Custom textarea component (Clay has no built-in multi-line input)
var textareaComponent = {
  name: "textarea",
  template:
    '<div class="component component-textarea">' +
    '<label class="label">{{{label}}}</label>' +
    '<textarea class="value" data-manipulator-target rows="10"></textarea>' +
    '</div>',
  style:
    '.component-textarea textarea {' +
    ' width: 100%; box-sizing: border-box; margin-top: 0.5rem;' +
    ' background: #333; color: #fff; border: 1px solid #666;' +
    ' border-radius: 4px; font-size: 1em; padding: 0.4rem; }',
  manipulator: "val",
  defaults: { label: "", defaultValue: "" }
};

function customClayFn(minified) {
  // no dynamic behavior needed; component registration happens below
}

var clay = new Clay(clayConfig, customClayFn, { autoHandleEvents: false });
clay.registerComponent(textareaComponent);

// ── Paste parsing: sticky prefixes ───────────────────────────────
// Returns array of items {t,c,d,r}. Prefix colors its own line and all
// following lines until the next prefix. Unprefixed leading lines: white.
// A prefix-only line starts a run without adding an item.
function parsePaste(text) {
  var items = [];
  var current = 7; // white
  text.split(/\r?\n/).forEach(function (lineRaw) {
    var line = lineRaw.trim();
    if (!line) return;
    var m = line.match(/^([ygubaprw]):\s*(.*)$/i);
    if (m) {
      current = COLOR_PREFIX[m[1].toLowerCase()];
      line = m[2].trim();
      if (!line) return;               // prefix-only line: just sets color
    }
    items.push({ t: line.slice(0, 39), c: current, d: false, r: false });
  });
  return items;
}

// ── Sync down: stream items one message at a time ────────────────
var syncQueue = [];

function buildSyncQueue() {
  syncQueue = [];
  syncQueue.push({ CMD: CMD_SYNC_START });
  getActive().forEach(function (it, i) {
    syncQueue.push({ CMD: CMD_ITEM, LIST: 0, INDEX: i, TEXT: it.t,
      COLOR: it.c, FLAGS: (it.d ? FLAG_DOTTED : 0) | (it.r ? FLAG_REAPPEND : 0) });
  });
  getDone().forEach(function (it, i) {
    syncQueue.push({ CMD: CMD_ITEM, LIST: 1, INDEX: i, TEXT: it.t,
      COLOR: it.c, FLAGS: 0 });
  });
  syncQueue.push({ CMD: CMD_SYNC_COMPLETE });
}

function pumpQueue() {
  if (syncQueue.length === 0) return;
  var msg = syncQueue.shift();
  Pebble.sendAppMessage(msg,
    function () { pumpQueue(); },
    function () {
      console.log("sync send failed, retrying");
      Pebble.sendAppMessage(msg,
        function () { pumpQueue(); },
        function () { console.log("retry failed, aborting sync"); });
    });
}

function resync() { buildSyncQueue(); pumpQueue(); }

Pebble.addEventListener("ready", function () {
  console.log("pkjs ready");
  resync();
});

// ── Clay lifecycle ───────────────────────────────────────────────
Pebble.addEventListener("showConfiguration", function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener("webviewclosed", function (e) {
  try {
    if (!e || !e.response) return;
    var s = clay.getSettings(e.response, false);
    var val = function (k, d) {
      return (s[k] !== undefined && s[k] !== null) ?
        (s[k].value !== undefined ? s[k].value : s[k]) : d;
    };

    var pasteText = String(val("PASTE", "") || "");
    if (pasteText.trim()) {
      var newItems = parsePaste(pasteText);
      var replace = !!val("MODE", false);
      var active = replace ? [] : getActive();
      var room = HARD_MAX - active.length;
      if (room < 0) room = 0;
      var accepted = newItems.slice(0, room);
      var dropped = newItems.length - accepted.length;
      setActive(active.concat(accepted));
      if (dropped > 0) {
        Pebble.showSimpleNotificationOnPebble("Pebblefocus",
          "List full: " + dropped + " line" + (dropped === 1 ? "" : "s") +
          " dropped (max " + HARD_MAX + ").");
      }
    }
    resync();
  } catch (err) {
    console.log("webviewclosed ERROR: " + err.message);
  }
});

// ── Deltas: mirror main.c logic exactly ──────────────────────────
Pebble.addEventListener("appmessage", function (e) {
  var p = e.payload;
  var cmd = p.CMD;
  var idx = p.INDEX || 0;
  var active = getActive();
  var done = getDone();

  if (cmd === CMD_CHECKOFF) {
    if (idx < 0 || idx >= active.length) return;
    var item = active.splice(idx, 1)[0];
    item.d = false;
    done.push(item);                              // every check-off records a pass
    while (done.length > HARD_MAX) done.shift();  // rolling: drop oldest
    setDone(done);
    if (item.r) {
      // reappend flag deliberately retained: recurring until untoggled
      if (active.length < HARD_MAX)
        active.push({ t: item.t, c: item.c, d: false, r: true });
    }
    setActive(active);

  } else if (cmd === CMD_TOGGLE_DOT) {
    if (active[idx]) { active[idx].d = !active[idx].d; setActive(active); }

  } else if (cmd === CMD_TOGGLE_REAPP) {
    if (active[idx]) { active[idx].r = !active[idx].r; setActive(active); }

  } else if (cmd === CMD_ADD) {
    if (active.length < HARD_MAX && p.TEXT) {
      active.push({ t: p.TEXT, c: 7, d: false, r: false }); // white
      setActive(active);
    }

  } else if (cmd === CMD_COPY_BACK) {
    if (done[idx] && active.length < HARD_MAX) {
      active.push({ t: done[idx].t, c: done[idx].c, d: false, r: false });
      setActive(active);                            // stays in Done
    }

  } else if (cmd === CMD_CLEAR_DONE) {
    setDone([]);

  } else if (cmd === CMD_SET_COLOR) {
    if (active[idx] && typeof p.COLOR === "number") {
      active[idx].c = p.COLOR;
      setActive(active);
    }
  }
});