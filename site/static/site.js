// Client-side search over search-index.json (no dependencies).
(function () {
  var root = document.documentElement.dataset.root || "";
  var input = document.getElementById("q");
  var list = document.getElementById("results");
  if (!input || !list) return;
  var index = null, sel = -1, loading = false;

  function load(cb) {
    if (index) return cb();
    if (loading) return;
    loading = true;
    fetch(root + "search-index.json").then(function (r) { return r.json(); })
      .then(function (j) { index = j; cb(); })
      .catch(function () { loading = false; });
  }

  function score(e, terms) {
    var t = e.t.toLowerCase(), d = (e.d || "").toLowerCase(), s = 0;
    for (var i = 0; i < terms.length; i++) {
      var w = terms[i];
      if (t === w) s += 100;
      else if (t.indexOf(w) === 0) s += 60;
      else if (t.indexOf(w) > -1) s += 35;
      else if (d.indexOf(w) > -1) s += 8;
      else return 0;
    }
    return s;
  }

  function esc(s) { return s.replace(/[&<>"]/g, function (c) { return {"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;"}[c]; }); }

  function render() {
    var q = input.value.trim().toLowerCase();
    if (!q || !index) { list.hidden = true; return; }
    var terms = q.split(/\s+/);
    var hits = index.map(function (e) { return {e: e, s: score(e, terms)}; })
      .filter(function (h) { return h.s > 0; })
      .sort(function (a, b) { return b.s - a.s || a.e.t.length - b.e.t.length; })
      .slice(0, 12);
    sel = -1;
    list.innerHTML = hits.length ? hits.map(function (h) {
      var d = h.e.d ? esc(h.e.d.slice(0, 110)) : "";
      return '<li><a href="' + root + h.e.u + '"><span class="kind">' + esc(h.e.k) + '</span><b>' + esc(h.e.t) + '</b><small>' + d + '</small></a></li>';
    }).join("") : '<li><a><small>No result</small></a></li>';
    list.hidden = false;
  }

  input.addEventListener("focus", function () { load(render); });
  input.addEventListener("input", function () { load(render); });
  input.addEventListener("keydown", function (ev) {
    var items = list.querySelectorAll("a[href]");
    if (ev.key === "ArrowDown" || ev.key === "ArrowUp") {
      ev.preventDefault();
      if (!items.length) return;
      sel = (sel + (ev.key === "ArrowDown" ? 1 : -1) + items.length) % items.length;
      items.forEach(function (a, i) { a.classList.toggle("sel", i === sel); });
    } else if (ev.key === "Enter") {
      var a = items[Math.max(sel, 0)];
      if (a) window.location = a.href;
    } else if (ev.key === "Escape") {
      list.hidden = true; input.blur();
    }
  });
  document.addEventListener("click", function (ev) {
    if (!ev.target.closest(".search")) list.hidden = true;
  });
  document.addEventListener("keydown", function (ev) {
    if (ev.key === "/" && document.activeElement !== input && !/INPUT|TEXTAREA/.test(document.activeElement.tagName)) {
      ev.preventDefault(); input.focus();
    }
  });
})();
