// Decorative hero background: one FastClick pipeline fed by several NIC queues.
// CPU cores (left) are tied to NIC RX queues; packets of a core's queue go through
// FromDPDKDevice -> Classifier -> FlowIPManager -> FlowIPNAT -> Counter -> ToDPDKDevice
// and leave through the matching NIC TX queue (right). Non-IPv4 packets go to Discard.
//
// Performance: nothing runs before the page is loaded and idle. The scene is then built in
// stages (skeleton, cores and queues, traffic) with traffic ramping up slowly. Packets use a
// fixed pool of elements, updates are throttled, and the loop degrades itself on slow devices.
(function () {
  var host = document.getElementById("graph");
  if (!host) return;
  var reduced = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;

  var NS = "http://www.w3.org/2000/svg";
  var W = 1260, Y = 300, BATCH = 4, SPEED = 190, CORES = 4, ROW = 48, POOL = 40;
  var FLOWS = ["#6366f1", "#06b6d4", "#f59e0b"], OTHER = "#f43f5e";
  var defs = [
    ["src", 175, 150, "FromDPDKDevice", "(0)"],
    ["cls", 365, 130, "Classifier", "(12/0800, -)"],
    ["mgr", 535, 150, "FlowIPManager", ""],
    ["nat", 725, 125, "FlowIPNAT", "(SIP 10.0.0.1)"],
    ["cnt", 890, 85, "Counter", "0"],
    ["dst", 1015, 140, "ToDPDKDevice", "(1)"]
  ];
  var N = {};
  defs.forEach(function (d) { N[d[0]] = {x: d[1], w: d[2], a: d[3], b: d[4]}; });
  var QW = 64, QH = 30, RXX = 58, TXX = 1190, CHIP = 30, CHIPX = 14;
  var qy = []; for (var i = 0; i < CORES; i++) qy.push(Y + (i - (CORES - 1) / 2) * ROW);
  var srcX = N.src.x, dstR = N.dst.x + N.dst.w, dx = N.cls.x + N.cls.w / 2;

  function el(name, attrs, parent) {
    var e = document.createElementNS(NS, name);
    for (var k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }
  function text(parent, x, y, s, cls, anchor) {
    var t = el("text", {x: x, y: y, "text-anchor": anchor || "middle", "class": cls}, parent);
    t.textContent = s;
    return t;
  }

  var svg, edgeG, cpuG, pktG, nodeG, cntText;
  var cpus = [], rxq = [], txq = [], pool = [], pkts = [];

  // ---- stage 1: skeleton (edges and elements) -------------------------------------------
  function buildSkeleton() {
    svg = el("svg", {viewBox: "0 " + (Y - 118) + " " + W + " 240", preserveAspectRatio: "xMidYMid meet", "class": "graph-svg", "aria-hidden": "true", focusable: "false"});
    cpuG = el("g", {"class": "g-stage"}, svg);
    edgeG = el("g", {}, svg);
    pktG = el("g", {}, svg);
    nodeG = el("g", {}, svg);
    var k;
    for (k = 0; k < defs.length - 1; k++) {
      var a = defs[k], b = defs[k + 1];
      el("line", {x1: a[1] + a[2], y1: Y, x2: b[1] - 6, y2: Y, "class": "g-edge"}, edgeG);
      head(b[1], Y);
    }
    defs.forEach(function (d) {
      var n = N[d[0]];
      el("rect", {x: n.x, y: Y - 24, width: n.w, height: 48, rx: 10, "class": "g-node"}, nodeG);
      text(nodeG, n.x + n.w / 2, Y + (n.b ? -3 : 5), n.a, "g-name");
      if (n.b) { var t = text(nodeG, n.x + n.w / 2, Y + 13, n.b, "g-sub"); if (d[0] === "cnt") cntText = t; }
    });
    el("path", {d: "M" + dx + " " + (Y + 26) + "V" + (Y + 59), "class": "g-edge g-dash"}, edgeG);
    el("rect", {x: dx - 45, y: Y + 62, width: 90, height: 34, rx: 8, "class": "g-node g-dim"}, nodeG);
    text(nodeG, dx, Y + 83, "Discard", "g-sub");
    host.appendChild(svg);
    // packet pool, created once and reused
    for (var p = 0; p < POOL; p++) {
      pool.push({el: el("rect", {width: 12, height: 12, rx: 3, "class": "g-pkt", visibility: "hidden"}, pktG), free: true});
    }
  }
  function head(x, y) { el("path", {d: "M" + (x - 10) + " " + (y - 4) + "L" + x + " " + y + "L" + (x - 10) + " " + (y + 4), "class": "g-edge"}, edgeG); }

  // ---- stage 2: CPU cores and NIC queues ------------------------------------------------
  function queue(g, x, y, label) {
    el("rect", {x: x, y: y - QH / 2, width: QW, height: QH, rx: 7, "class": "g-node"}, g);
    var fill = el("rect", {x: x + 1, y: y - QH / 2 + 1, width: QW - 2, height: QH - 2, rx: 6, fill: "#6366f1", opacity: 0}, g);
    text(g, x + QW / 2, y + 4, label, "g-sub");
    return {fill: fill, v: 0, shown: 0};
  }
  function buildCores() {
    var qG = el("g", {"class": "g-stage"}, svg);
    for (var c = 0; c < CORES; c++) {
      var y = qy[c], g = el("g", {}, cpuG);
      // cheap glow: stacked translucent rounded squares instead of a blur filter
      var glow = [];
      for (var r = 3; r >= 1; r--) {
        glow.push(el("rect", {x: CHIPX - r * 4, y: y - CHIP / 2 - r * 4, width: CHIP + r * 8, height: CHIP + r * 8, rx: 7 + r * 3}, g));
      }
      for (var p = 0; p < 3; p++) {
        var o = 6 + p * 9;
        el("path", {d: "M" + (CHIPX + o) + " " + (y - CHIP / 2 - 3) + "v3M" + (CHIPX + o) + " " + (y + CHIP / 2) + "v3", "class": "g-pin"}, g);
      }
      el("rect", {x: CHIPX, y: y - CHIP / 2, width: CHIP, height: CHIP, rx: 7, "class": "g-chip"}, g);
      var die = el("rect", {x: CHIPX + 8, y: y - 7, width: 14, height: 14, rx: 3}, g);
      el("line", {x1: CHIPX + CHIP, y1: y, x2: RXX, y2: y, "class": "g-edge g-link"}, edgeG);
      el("line", {x1: RXX + QW, y1: y, x2: srcX - 2, y2: Y, "class": "g-edge"}, edgeG);
      el("line", {x1: dstR + 2, y1: Y, x2: TXX, y2: y, "class": "g-edge"}, edgeG);
      cpus.push({glow: glow, die: die, load: .2 + Math.random() * .3, phase: Math.random() * 6.28, speed: .25 + Math.random() * .3, hue: -1});
      rxq.push(queue(qG, RXX, y, "RX " + c));
      txq.push(queue(qG, TXX, y, "TX " + c));
    }
    head(srcX, Y); head(TXX, qy[0]);
    var ly = Y - 1.5 * ROW - 24;
    text(cpuG, CHIPX + CHIP / 2 - 14, ly, "CPU", "g-tag", "middle");
    text(qG, RXX + QW / 2 + 12, ly, "RX", "g-tag");
    text(qG, TXX + QW / 2, ly, "TX", "g-tag");
    setTimeout(function () { cpuG.classList.add("on"); qG.classList.add("on"); }, 30);
    updateCpus(true);
  }

  // ---- simulation -------------------------------------------------------------------------
  var routes = [], count = 0, spawnIn = 1, t0 = 0, rr = 0, ramp = 0, cpuAcc = 0, thin = 1;
  function route(core) {
    var pts = [[RXX + QW, qy[core]], [srcX, Y], [dstR, Y], [TXX, qy[core]]], segs = [], tot = 0;
    for (var i = 0; i < 3; i++) {
      var l = Math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]);
      segs.push(l); tot += l;
    }
    return {pts: pts, segs: segs, tot: tot};
  }
  function at(r, s) {
    for (var i = 0; i < 3; i++) {
      if (s <= r.segs[i] || i === 2) {
        var f = Math.max(0, Math.min(1, s / r.segs[i]));
        return [r.pts[i][0] + (r.pts[i + 1][0] - r.pts[i][0]) * f, r.pts[i][1] + (r.pts[i + 1][1] - r.pts[i][1]) * f];
      }
      s -= r.segs[i];
    }
  }
  function grab() { for (var i = 0; i < POOL; i++) if (pool[i].free) { pool[i].free = false; return pool[i]; } return null; }
  function release(p) { p.slot.free = true; p.slot.el.setAttribute("visibility", "hidden"); p.dead = true; }

  function spawn() {
    var core = rr++ % CORES;
    cpus[core].load = Math.min(1, cpus[core].load + .35);
    rxq[core].v = 1;
    var flow = FLOWS[Math.floor(Math.random() * FLOWS.length)];
    for (var i = 0; i < BATCH; i++) {
      var slot = grab(); if (!slot) return;
      var other = Math.random() < .22;
      var color = other ? OTHER : (Math.random() < .6 ? flow : FLOWS[Math.floor(Math.random() * FLOWS.length)]);
      slot.el.setAttribute("fill", color);
      pkts.push({slot: slot, core: core, s: -i * 17, x: 0, y: 0, other: other, st: "go", t: 0, counted: false, shown: false});
    }
  }
  function place(p) {
    p.slot.el.setAttribute("x", (p.x - 6).toFixed(1));
    p.slot.el.setAttribute("y", (p.y - 6).toFixed(1));
  }
  function updateCpus(force) {
    cpus.forEach(function (c) {
      var h = Math.round((190 - 160 * c.load) / 6) * 6;     // quantised: fewer style updates
      if (h !== c.hue || force) {
        c.hue = h;
        var col = "hsl(" + h + " 90% 55%)";
        c.glow.forEach(function (g) { g.setAttribute("fill", col); });
        c.die.setAttribute("fill", col);
      }
      c.glow[0].setAttribute("opacity", (.04 + .08 * c.load).toFixed(2));
      c.glow[1].setAttribute("opacity", (.06 + .16 * c.load).toFixed(2));
      c.glow[2].setAttribute("opacity", (.08 + .3 * c.load).toFixed(2));
      c.die.setAttribute("opacity", (.25 + .75 * c.load).toFixed(2));
    });
    rxq.concat(txq).forEach(function (q) {
      var o = q.v > .02 ? q.v * .35 : 0;
      if (o !== q.shown) { q.shown = o; q.fill.setAttribute("opacity", o.toFixed(2)); }
    });
  }
  function step(dt) {
    t0 += dt; cpuAcc += dt;
    ramp = Math.min(1, ramp + dt / 8);                       // traffic fades in over ~8 s
    cpus.forEach(function (c) {
      var target = .25 + .35 * (Math.sin(t0 * c.speed + c.phase) + 1) / 2;
      c.load += (target - c.load) * Math.min(1, dt * 1.2);
    });
    rxq.concat(txq).forEach(function (q) { q.v = Math.max(0, q.v - dt * 1.4); });
    if (cpuAcc > .12) { cpuAcc = 0; updateCpus(false); }       // glow refresh at about 8 Hz
    spawnIn -= dt;
    if (spawnIn <= 0) { spawn(); spawnIn = (.8 + Math.random() * .8) * (2.6 - 1.6 * ramp) * thin; }
    for (var i = 0; i < pkts.length; i++) {
      var p = pkts[i];
      if (p.st === "go") {
        p.s += SPEED * dt;
        var r = routes[p.core], pos = at(r, Math.max(0, p.s));
        p.x = pos[0]; p.y = pos[1];
        if (!p.shown && p.s >= 0) { p.shown = true; p.slot.el.setAttribute("visibility", "visible"); }
        if (p.other && p.x >= dx && p.y === Y) { p.st = "drop"; p.x = dx; }
        else {
          if (!p.counted && p.x >= N.cnt.x + N.cnt.w / 2) { p.counted = true; count++; cntText.textContent = count; }
          if (p.s >= r.tot) { txq[p.core].v = 1; release(p); }
        }
      } else {
        p.y += SPEED * .6 * dt; p.t += dt;
        p.slot.el.setAttribute("opacity", Math.max(0, 1 - Math.max(0, p.t - .3) * 3).toFixed(2));
        if (p.t > .7) { p.slot.el.removeAttribute("opacity"); release(p); }
      }
      if (!p.dead && p.shown) place(p);
    }
    pkts = pkts.filter(function (p) { return !p.dead; });
  }

  // ---- loop with self-throttling ----------------------------------------------------------
  var last = null, visible = true, acc = 0, slow = 0, frames = 0, stopped = false, minDt = 1 / 30;
  function frame(t) {
    if (stopped) return;
    requestAnimationFrame(frame);
    if (last === null) { last = t; return; }
    var dt = (t - last) / 1000; last = t;
    if (!visible || document.hidden) return;
    acc += dt;
    if (acc < minDt) return;                                // cap at about 30 fps
    var t1 = performance.now();
    step(Math.min(.1, acc)); acc = 0;
    var cost = performance.now() - t1;
    // degrade: if a step is expensive or frames are long, lower fps, then thin traffic, then stop
    if (cost > 6 || dt > .06) slow++; else slow = Math.max(0, slow - 1);
    if (++frames % 30 === 0 && slow > 20) {
      slow = 0;
      if (minDt < 1 / 20) minDt = 1 / 15; else if (thin < 2) thin = 2.5; else { stopped = true; host.classList.add("still"); }
    }
  }

  function start() {
    buildSkeleton();
    for (var q = 0; q < CORES; q++) routes.push(route(q));
    host.classList.add("on");                               // CSS fade-in of the skeleton
    if (reduced) { buildCores(); return; }
    setTimeout(function () {
      buildCores();
      setTimeout(function () {                              // stage 3: traffic
        if ("IntersectionObserver" in window) new IntersectionObserver(function (e) { visible = e[0].isIntersecting; }).observe(host);
        requestAnimationFrame(frame);
      }, 500);
    }, 250);
  }

  // Wait for the page to be fully loaded and the main thread idle before touching the DOM.
  function whenIdle(fn) {
    var go = function () { (window.requestIdleCallback || function (f) { setTimeout(f, 200); })(fn, {timeout: 1500}); };
    if (document.readyState === "complete") go(); else window.addEventListener("load", go, {once: true});
  }
  whenIdle(start);
})();
