// Pebblefocus pkjs — phone storage + Clay config
console.log("Pebblefocus index.js LOADED - version 2026-07-16-b (step 3)");

var Clay = require('pebble-clay');

var HARD_MAX = 50;   // watch RAM arrays are sized to this; cap slider <= this

// Commands (must match main.c)
var CMD_ITEM = 1, CMD_SYNC_COMPLETE = 2, CMD_SYNC_START = 3, CMD_SET_CAP = 4;
var CMD_CHECKOFF = 10, CMD_TOGGLE_DOT = 11, CMD_TOGGLE_REAPP = 12,
    CMD_ADD = 13, CMD_COPY_BACK = 14, CMD_CLEAR_DONE = 15;
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
function getCap()    { return load("pf_cap", HARD_MAX); }
function setActive(a) { save("pf_active", a); }
function setDone(d)   { save("pf_done", d); }
function setCap(c)    { save("pf_cap", c); }

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
        defaultValue: "Color prefixes stick until the next prefix: " +
          "y: yellow &middot; g: green &middot; u: purple &middot; b: blue &middot; " +
          "a: aqua &middot; p: pink &middot; r: red &middot; w: white"
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
      { type: "heading", defaultValue: "Settings", size: 4 },
      {
        type: "slider",
        messageKey: "CAP",
        label: "Item cap",
        defaultValue: 50, min: 5, max: 50, step: 5
      },
      {
        type: "toggle",
        messageKey: "RESET",
        label: "RESET: wipe both lists",
        defaultValue: false
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
  syncQueue.push({ CMD: CMD_SET_CAP, INDEX: getCap() });
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
    console.log("webviewclosed fired; response present: " + !!(e && e.response));
    if (!e || !e.response) return;
    console.log("raw response (first 200): " +
      String(e.response).slice(0, 200));
    var s = clay.getSettings(e.response, false);
    console.log("parsed settings keys: " + Object.keys(s).join(","));
    var val = function (k, d) {
      return (s[k] !== undefined && s[k] !== null) ?
        (s[k].value !== undefined ? s[k].value : s[k]) : d;
    };

    var cap = parseInt(val("CAP", HARD_MAX), 10) || HARD_MAX;
    if (cap > HARD_MAX) cap = HARD_MAX;
    if (cap < 1) cap = 1;
    setCap(cap);
    console.log("cap set: " + cap);

    if (val("RESET", false)) {
      setActive([]); setDone([]);
      console.log("RESET: both lists wiped");
      Pebble.showSimpleNotificationOnPebble("Pebblefocus", "Lists wiped.");
      resync();
      return;
    }

    var pasteText = String(val("PASTE", "") || "");
    console.log("paste length: " + pasteText.length);
    if (pasteText.trim()) {
      var newItems = parsePaste(pasteText);
      console.log("parsed items: " + newItems.length);
      var replace = !!val("MODE", false);
      var active = replace ? [] : getActive();
      var room = cap - active.length;
      if (room < 0) room = 0;
      var accepted = newItems.slice(0, room);
      var dropped = newItems.length - accepted.length;
      setActive(active.concat(accepted));
      console.log("stored active count: " + getActive().length +
        ", dropped: " + dropped);
      if (dropped > 0) {
        Pebble.showSimpleNotificationOnPebble("Pebblefocus",
          "List full: " + dropped + " line" + (dropped === 1 ? "" : "s") +
          " dropped (cap " + cap + ").");
      }
    }
    console.log("resyncing");
    resync();
  } catch (err) {
    console.log("webviewclosed ERROR: " + err.message + " | " + err.stack);
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
    if (item.r) {
      item.d = false;
      // reappend flag deliberately retained: recurring until untoggled
      if (active.length < getCap()) active.push(item);
    } else {
      item.d = false;
      done.push(item);
      while (done.length > HARD_MAX) done.shift(); // rolling: drop oldest
      setDone(done);
    }
    setActive(active);

  } else if (cmd === CMD_TOGGLE_DOT) {
    if (active[idx]) { active[idx].d = !active[idx].d; setActive(active); }

  } else if (cmd === CMD_TOGGLE_REAPP) {
    if (active[idx]) { active[idx].r = !active[idx].r; setActive(active); }

  } else if (cmd === CMD_ADD) {
    if (active.length < getCap() && p.TEXT) {
      active.push({ t: p.TEXT, c: 7, d: false, r: false }); // white
      setActive(active);
    }

  } else if (cmd === CMD_COPY_BACK) {
    if (done[idx] && active.length < getCap()) {
      active.push({ t: done[idx].t, c: done[idx].c, d: false, r: false });
      setActive(active);                            // stays in Done
    }

  } else if (cmd === CMD_CLEAR_DONE) {
    setDone([]);
  }
});
