// Pebblefocus pkjs — phone-side storage (source of truth)
console.log("Pebblefocus index.js LOADED - version 2026-07-16-a");

var MAX_ITEMS = 50;

// Commands (must match main.c)
var CMD_ITEM = 1, CMD_SYNC_COMPLETE = 2, CMD_SYNC_START = 3;
var CMD_CHECKOFF = 10, CMD_TOGGLE_DOT = 11, CMD_TOGGLE_REAPP = 12,
    CMD_ADD = 13, CMD_COPY_BACK = 14, CMD_CLEAR_DONE = 15;
var FLAG_DOTTED = 1, FLAG_REAPPEND = 2;

// Item shape: { t: text, c: colorIndex, d: dotted, r: reappend }
// Color indices match main.c enum:
// 0 purple, 1 blue, 2 green, 3 yellow, 4 aqua, 5 pink, 6 red, 7 white

function load(key, fallback) {
  try {
    var raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : fallback;
  } catch (e) { return fallback; }
}
function save(key, val) { localStorage.setItem(key, JSON.stringify(val)); }

function getActive() { return load("pf_active", null); }
function getDone()   { return load("pf_done", []); }
function setActive(a) { save("pf_active", a); }
function setDone(d)   { save("pf_done", d); }

function seedDefaults() {
  // Step-2 seed data; replaced by Clay paste in step 3
  setActive([
    { t: "Bible + prayer",      c: 3, d: true,  r: false },
    { t: "Laundry load",        c: 0, d: false, r: false },
    { t: "Rental registration", c: 1, d: false, r: true  },
    { t: "Cold shower",         c: 2, d: false, r: false },
    { t: "B&N return",          c: 4, d: false, r: false },
    { t: "Curriculum for J",    c: 5, d: false, r: false },
    { t: "NJ taxes mail",       c: 6, d: true,  r: false },
    { t: "Chia pudding",        c: 7, d: false, r: false }
  ]);
  setDone([]);
}

// ── Sync down: stream items one message at a time ────────────────
var syncQueue = [];

function buildSyncQueue() {
  syncQueue = [];
  syncQueue.push({ CMD: CMD_SYNC_START });
  var active = getActive(), done = getDone();
  active.forEach(function (it, i) {
    syncQueue.push({ CMD: CMD_ITEM, LIST: 0, INDEX: i, TEXT: it.t,
      COLOR: it.c, FLAGS: (it.d ? FLAG_DOTTED : 0) | (it.r ? FLAG_REAPPEND : 0) });
  });
  done.forEach(function (it, i) {
    syncQueue.push({ CMD: CMD_ITEM, LIST: 1, INDEX: i, TEXT: it.t,
      COLOR: it.c, FLAGS: 0 });
  });
  syncQueue.push({ CMD: CMD_SYNC_COMPLETE });
}

function pumpQueue() {
  if (syncQueue.length === 0) return;
  var msg = syncQueue.shift();
  Pebble.sendAppMessage(msg,
    function () { pumpQueue(); },                     // ack: send next
    function (e) {                                    // nack: retry once
      console.log("sync send failed, retrying: " + JSON.stringify(msg));
      Pebble.sendAppMessage(msg,
        function () { pumpQueue(); },
        function () { console.log("retry failed, aborting sync"); });
    });
}

Pebble.addEventListener("ready", function () {
  console.log("pkjs ready");
  if (getActive() === null) seedDefaults();
  buildSyncQueue();
  pumpQueue();
});

// ── Deltas: mirror main.c logic exactly ──────────────────────────
Pebble.addEventListener("appmessage", function (e) {
  var p = e.payload;
  var cmd = p.CMD;
  var idx = p.INDEX || 0;
  var active = getActive() || [];
  var done = getDone();

  if (cmd === CMD_CHECKOFF) {
    if (idx < 0 || idx >= active.length) return;
    var item = active.splice(idx, 1)[0];
    if (item.r) {
      item.d = false;
      // reappend flag deliberately retained: recurring until untoggled
      if (active.length < MAX_ITEMS) active.push(item);
    } else {
      item.d = false;
      done.push(item);
      while (done.length > MAX_ITEMS) done.shift(); // rolling: drop oldest
      setDone(done);
    }
    setActive(active);

  } else if (cmd === CMD_TOGGLE_DOT) {
    if (active[idx]) { active[idx].d = !active[idx].d; setActive(active); }

  } else if (cmd === CMD_TOGGLE_REAPP) {
    if (active[idx]) { active[idx].r = !active[idx].r; setActive(active); }

  } else if (cmd === CMD_ADD) {
    if (active.length < MAX_ITEMS && p.TEXT) {
      active.push({ t: p.TEXT, c: 7, d: false, r: false }); // white
      setActive(active);
    }

  } else if (cmd === CMD_COPY_BACK) {
    if (done[idx] && active.length < MAX_ITEMS) {
      var copy = { t: done[idx].t, c: done[idx].c, d: false, r: false };
      active.push(copy);                            // stays in Done
      setActive(active);
    }

  } else if (cmd === CMD_CLEAR_DONE) {
    setDone([]);
  }
});
