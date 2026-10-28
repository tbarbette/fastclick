// Decorative hero background: one FastClick pipeline fed by several NIC queues.
// CPU cores (left) are tied to NIC RX queues; packets of a core's queue go through
// FromDPDKDevice -> Classifier -> FlowIPManager -> FlowIPNAT -> Counter -> ToDPDKDevice
// and leave through the matching NIC TX queue (right). Non-IPv4 packets go to Discard.
(function () {
  var host = document.getElementById("graph");
  if (!host) return;
  var NS = "http://www.w3.org/2000/svg";
  var W = 1260, Y = 300, BATCH = 4, SPEED = 190, CORES = 4, ROW = 48;
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

  var svg = el("svg", {viewBox: "0 " + (Y - 118) + " " + W + " 240", preserveAspectRatio: "xMidYMid meet", "class": "graph-svg", "aria-hidden": "true", focusable: "false"});
  host.appendChild(svg);
  var filt = el("filter", {id: "g-blur", x: "-60%", y: "-60%", width: "220%", height: "220%"}, el("defs", {}, svg));
  el("feGaussianBlur", {stdDeviation: 6}, filt);
  var edgeG = el("g", {}, svg), cpuG = el("g", {}, svg), pktG = el("g", {}, svg), nodeG = el("g", {}, svg);

  // pipeline edges
  function arrow(x1, y1, x2, y2) {
    el("line", {x1: x1, y1: y1, x2: x2, y2: y2, "class": "g-edge"}, edgeG);
  }
  function head(x, y) { el("path", {d: "M" + (x - 10) + " " + (y - 4) + "L" + x + " " + y + "L" + (x - 10) + " " + (y + 4), "class": "g-edge"}, edgeG); }
  for (var k = 0; k < defs.length - 1; k++) {
    var a = defs[k], b = defs[k + 1];
    arrow(a[1] + a[2], Y, b[1] - 6, Y); head(b[1], Y);
  }
  var srcX = N.src.x, dstR = N.dst.x + N.dst.w;

  // CPUs + NIC queues
  var cpus = [], txq = [], rxq = [];
  function queue(x, y, label, tx) {
    var g = el("g", {}, nodeG);
    var bg = el("rect", {x: x, y: y - QH / 2, width: QW, height: QH, rx: 7, "class": "g-node"}, g);
    var fill = el("rect", {x: x + 1, y: y - QH / 2 + 1, width: QW - 2, height: QH - 2, rx: 6, fill: "#6366f1", opacity: 0}, g);
    text(g, x + QW / 2, y + 4, label, "g-sub");
    return {fill: fill, v: 0};
  }
  for (var c = 0; c < CORES; c++) {
    var y = qy[c], g = el("g", {}, cpuG);
    var glow = el("rect", {x: CHIPX - 4, y: y - CHIP / 2 - 4, width: CHIP + 8, height: CHIP + 8, rx: 10, filter: "url(#g-blur)"}, g);
    for (var p = 0; p < 3; p++) {
      var o = 6 + p * 9;
      el("path", {d: "M" + (CHIPX + o) + " " + (y - CHIP / 2 - 3) + "v3M" + (CHIPX + o) + " " + (y + CHIP / 2) + "v3", "class": "g-pin"}, g);
    }
    el("rect", {x: CHIPX, y: y - CHIP / 2, width: CHIP, height: CHIP, rx: 7, "class": "g-chip"}, g);
    var die = el("rect", {x: CHIPX + 8, y: y - 7, width: 14, height: 14, rx: 3}, g);
    el("line", {x1: CHIPX + CHIP, y1: y, x2: RXX, y2: y, "class": "g-edge g-link"}, edgeG);   // core <-> RX queue
    cpus.push({glow: glow, die: die, load: .2 + Math.random() * .3, phase: Math.random() * 6.28, speed: .25 + Math.random() * .3});
    rxq.push(queue(RXX, y, "RX " + c));
    txq.push(queue(TXX, y, "TX " + c));
    arrow(RXX + QW, y, srcX - 2, Y);                   // fan-in
    arrow(dstR + 2, Y, TXX, y);                         // fan-out
  }
  head(srcX, Y); head(TXX, qy[0]); // arrow tips on the first lanes only keep the drawing light
  text(cpuG, CHIPX + CHIP / 2 - 14, Y - 1.5 * ROW - 24, "CPU", "g-tag", "middle");
  text(cpuG, RXX + QW / 2 + 12, Y - 1.5 * ROW - 24, "RX", "g-tag");
  text(cpuG, TXX + QW / 2, Y - 1.5 * ROW - 24, "TX", "g-tag");

  var cntText;
  defs.forEach(function (d) {
    var n = N[d[0]];
    el("rect", {x: n.x, y: Y - 24, width: n.w, height: 48, rx: 10, "class": "g-node"}, nodeG);
    text(nodeG, n.x + n.w / 2, Y + (n.b ? -3 : 5), n.a, "g-name");
    if (n.b) { var t = text(nodeG, n.x + n.w / 2, Y + 13, n.b, "g-sub"); if (d[0] === "cnt") cntText = t; }
  });

  // Discard below the classifier
  var dx = N.cls.x + N.cls.w / 2, DY = Y + 62;
  el("path", {d: "M" + dx + " " + (Y + 26) + "V" + (DY - 3), "class": "g-edge g-dash"}, edgeG);
  el("rect", {x: dx - 45, y: DY, width: 90, height: 34, rx: 8, "class": "g-node g-dim"}, nodeG);
  text(nodeG, dx, DY + 21, "Discard", "g-sub");

  var pkts = [], count = 0, spawnIn = .3, t0 = 0, rr = 0;

  // route: RX queue -> FromDPDKDevice (fan-in), pipeline, ToDPDKDevice -> TX queue (fan-out)
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
  var routes = []; for (var q = 0; q < CORES; q++) routes.push(route(q));

  function place(p) { p.el.setAttribute("x", p.x - 6); p.el.setAttribute("y", p.y - 6); }
  function spawn() {
    var core = rr++ % CORES;
    cpus[core].load = Math.min(1, cpus[core].load + .35);
    rxq[core].v = 1;
    var flow = FLOWS[Math.floor(Math.random() * FLOWS.length)];
    for (var i = 0; i < BATCH; i++) {
      var other = Math.random() < .22;
      var color = other ? OTHER : (Math.random() < .6 ? flow : FLOWS[Math.floor(Math.random() * FLOWS.length)]);
      var r = el("rect", {width: 12, height: 12, rx: 3, fill: color, "class": "g-pkt", opacity: 0}, pktG);
      pkts.push({el: r, core: core, s: -i * 17, x: 0, y: 0, other: other, st: "go", t: 0, counted: false});
    }
  }
  function hue(l) { return Math.round(190 - 160 * l); } // cyan (idle) to amber (busy)
  function cpuStep(dt) {
    t0 += dt;
    cpus.forEach(function (c) {
      var target = .25 + .35 * (Math.sin(t0 * c.speed + c.phase) + 1) / 2;
      c.load += (target - c.load) * Math.min(1, dt * 1.2);
      var col = "hsl(" + hue(c.load) + " 90% 55%)";
      c.glow.setAttribute("fill", col); c.glow.setAttribute("opacity", (.1 + .9 * c.load).toFixed(2));
      c.die.setAttribute("fill", col); c.die.setAttribute("opacity", (.25 + .75 * c.load).toFixed(2));
    });
    rxq.concat(txq).forEach(function (q) {
      q.v = Math.max(0, q.v - dt * 1.4);
      q.fill.setAttribute("opacity", (q.v * .35).toFixed(2));
    });
  }
  function step(dt) {
    cpuStep(dt);
    spawnIn -= dt;
    if (spawnIn <= 0) { spawn(); spawnIn = .8 + Math.random() * .8; }
    pkts.forEach(function (p) {
      if (p.st === "go") {
        p.s += SPEED * dt;
        var r = routes[p.core], pos = at(r, Math.max(0, p.s));
        p.x = pos[0]; p.y = pos[1];
        p.el.setAttribute("opacity", p.s < 0 ? 0 : 1);
        if (p.other && p.x >= dx && p.y === Y) { p.st = "drop"; p.x = dx; }
        else {
          if (!p.counted && p.x >= N.cnt.x + N.cnt.w / 2) { p.counted = true; count++; cntText.textContent = count; }
          if (p.s >= r.tot) { txq[p.core].v = 1; p.dead = true; }
        }
      } else {
        p.y += SPEED * .6 * dt; p.t += dt;
        p.el.setAttribute("opacity", Math.max(0, 1 - Math.max(0, p.t - .3) * 3));
        if (p.t > .7) p.dead = true;
      }
      if (p.dead) p.el.parentNode.removeChild(p.el); else place(p);
    });
    pkts = pkts.filter(function (p) { return !p.dead; });
  }

  for (var w = 0; w < 90; w++) step(.1); // warm up
  if (window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches) return;
  var last = null, visible = true;
  if ("IntersectionObserver" in window) new IntersectionObserver(function (e) { visible = e[0].isIntersecting; }).observe(host);
  function frame(t) {
    if (last !== null && visible) step(Math.min(.05, (t - last) / 1000));
    last = t;
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
})();
