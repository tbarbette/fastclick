#!/usr/bin/env python3
"""Static site generator for www.fastclick.dev.

Inputs
  --wiki      checkout of the GitHub wiki (Markdown pages + images/)
  --elements  output of `make -C doc install-man-markdown O=...` (one .md per element)
  --repo      repository root (README-like files, element sources for categories)
  --api       optional Doxygen HTML output, copied to <out>/api
  --out       destination directory

Only needs the `markdown` and `pygments` Python packages.
"""
import argparse
import html
import json
import os
import re
import shutil
import sys
from urllib.parse import unquote

import markdown

GITHUB = "https://github.com/tbarbette/fastclick"
SITE_DIR = os.path.dirname(os.path.abspath(__file__))

# Navigation groups for the wiki pages (title shown in the sidebar, page slug).
DOC_GROUPS = [
    ("Getting started", [
        ("Installation", "@Installation"),
        ("Tutorial", "Tutorial"),
        ("Click language", "Introduction-to-Click-Language"),
        ("High-speed I/O", "High-speed-I-O"),
        ("Docker", "Docker"),
        ("Example configurations", "@Configurations"),
    ]),
    ("Using FastClick", [
        ("Handlers", "Handlers"),
        ("Traffic generation", "FastClick-for-traffic-generation"),
        ("Visualising graphs", "Visualisation-of-Click-graphs"),
        ("Measuring CPU load", "How-to-get-CPU-load-when-using-DPDK-polling?"),
        ("Batching", "Batching"),
        ("Flow subsystem", "Flow-subsystem"),
    ]),
    ("Systems built on FastClick", [
        ("MiddleClick", "@MiddleClick"),
        ("PacketMill", "@PacketMill"),
        ("Projects", "Projects"),
        ("Community contributions", "Community-Contributions"),
    ]),
    ("Internals", [
        ("Click under the hood", "Click-Under-the-Hood"),
        ("Differences with Click", "Differences-between-FastClick-and-Click"),
        ("Extending FastClick", "Extending-FastClick"),
        ("Create your own element", "Create-your-own-element"),
        ("Click data structures", "Click-data-structures"),
        ("Continuous integration", "Continuous-Integration"),
    ]),
]

# Pages generated from files of the repository itself.
REPO_PAGES = {
    "Installation": ("INSTALL.md", "Installation"),
    "Configurations": ("conf/README.md", "Example configurations"),
    "MiddleClick": ("README.middleclick.md", "MiddleClick"),
    "PacketMill": ("README.packetmill.md", "PacketMill"),
}

# Man pages converted by `make install-man-markdown`.
REFERENCE = [
    ("Userlevel", "click", "The userlevel driver"),
    ("Linuxmodule", "click.o", "The Linux kernel module (legacy)"),
    ("Language", "click(5)", "The Click configuration language"),
    ("Elementdoc", "elementdoc", "Writing element documentation"),
]
TOOLS = ["click-align", "click-combine", "click-devirtualize", "click-fastclassifier",
         "click-flatten", "click-install", "click-mkmindriver", "click-pretty",
         "click-uncombine", "click-undead", "click-uninstall", "click-xform"]

CATEGORY_NAMES = {
    "analysis": "Analysis", "annotations": "Annotations", "arp": "ARP",
    "aqm": "Active queue management", "basicsched": "Schedulers",
    "basicsources": "Sources", "debugging": "Debugging", "dpdk": "DPDK",
    "ethernet": "Ethernet", "flow": "Flows", "icmp": "ICMP", "information": "Information",
    "ip": "IP", "ip6": "IPv6", "ipsec": "IPsec", "local": "Local", "multithread": "Multithreading",
    "netmap": "Netmap", "nat": "Address translation", "packet": "Packet manipulation",
    "shaping": "Shaping", "standard": "Standard", "tcp": "TCP", "udp": "UDP",
    "threads": "Threads", "wifi": "Wireless", "storage": "Storage", "tunnel": "Tunnels",
    "ids": "Intrusion detection", "ip rewriting": "IP rewriting", "classification": "Classification",
}

NAV = [("Overview", "index.html"), ("Documentation", "docs/index.html"),
       ("Elements", "elements/index.html"), ("Reference", "reference/index.html")]


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------
def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def slugify(name):
    return re.sub(r"[^A-Za-z0-9._-]+", "-", name).strip("-")


def prettify_category(c):
    c = c.split(",")[0].strip()
    return CATEGORY_NAMES.get(c.lower(), c[:1].upper() + c[1:])


def strip_tags(s):
    return html.unescape(re.sub(r"<[^>]+>", "", s)).strip()


class Site:
    def __init__(self, args):
        self.a = args
        self.pages = {}      # lowercase key -> output path (relative to out)
        self.titles = {}     # output path -> title
        self.search = []     # search index entries
        self.elements = {}   # element name -> dict(category, desc, md)

    # ------------------------------------------------------------ discovery
    def scan_sources(self):
        """Read =c/=s blocks in element sources to learn categories."""
        info = {}
        root = os.path.join(self.a.repo, "elements")
        for dp, _, files in os.walk(root):
            for fn in files:
                if not fn.endswith((".hh", ".h", ".cc")):
                    continue
                text = read(os.path.join(dp, fn))
                for m in re.finditer(r"/\*(.*?)\*/", text, re.S):
                    body = m.group(1)
                    if "=c" not in body or "=s" not in body:
                        continue
                    lines = [re.sub(r"^\s*\*? ?", "", l) for l in body.split("\n")]
                    name = cat = None
                    i = 0
                    while i < len(lines):
                        l = lines[i].strip()
                        if l == "=c" and name is None:
                            j = i + 1
                            while j < len(lines) and not lines[j].strip():
                                j += 1
                            mm = re.match(r"([A-Za-z0-9_]+)", lines[j].strip()) if j < len(lines) else None
                            name = mm.group(1) if mm else None
                        elif l.startswith("=s "):
                            cat = l[3:].strip()
                        i += 1
                    if name and cat:
                        info.setdefault(name, cat)
        return info

    def load_elements(self):
        cats = self.scan_sources()
        edir = self.a.elements
        for fn in sorted(os.listdir(edir)):
            if not fn.endswith(".md"):
                continue
            name = fn[:-3]
            if name in TOOLS or name in [r[0] for r in REFERENCE]:
                continue
            text = read(os.path.join(edir, fn))
            m = re.search(r"Click element;\s*\n(.*?)\n\s*\n", text, re.S)
            desc = " ".join(m.group(1).split()) if m else ""
            self.elements[name] = dict(
                category=prettify_category(cats.get(name, "other")),
                desc=desc, md=text)
            self.pages[name.lower()] = "elements/%s.html" % slugify(name)

    def load_wiki(self):
        self.wiki = {}
        w = self.a.wiki
        for fn in sorted(os.listdir(w)):
            if fn.endswith(".md") and not fn.startswith("_"):
                slug = fn[:-3]
                self.wiki[slug] = read(os.path.join(w, fn))
                self.pages[slug.lower()] = "docs/%s.html" % slugify(slug)
        self.pages["elements"] = "elements/index.html"
        self.pages["home"] = "docs/index.html"
        self.pages["language"] = "reference/Language.html"
        for key, (path, title) in REPO_PAGES.items():
            if os.path.exists(os.path.join(self.a.repo, path)):
                self.pages[key.lower()] = "docs/%s.html" % key
        for name, _, _ in REFERENCE:
            self.pages[name.lower()] = "reference/%s.html" % name
        for t in TOOLS:
            self.pages[t] = "reference/%s.html" % t

    # -------------------------------------------------------------- linking
    def resolve(self, href, depth):
        """Rewrite a Markdown link target to a path relative to the page."""
        if re.match(r"^(https?:|mailto:|#|//|data:)", href):
            return self.fix_github(href)
        path, _, anchor = href.partition("#")
        target = unquote(path).strip()
        target = re.sub(r"^(\./|\.\./)+", "", target)
        target = re.sub(r"\.(md|html)$", "", target)
        if target.startswith("images/"):
            return "../" * depth + "docs/" + path
        key = target.lower()
        key = key[len("elements/"):] if key.startswith("elements/") and key[9:] in self.pages else key
        out = self.pages.get(key)
        if out:
            return "../" * depth + out + ("#" + anchor if anchor else "")
        if target and os.path.exists(os.path.join(self.a.repo, unquote(path))):
            return "%s/blob/main/%s" % (GITHUB, unquote(path))
        if target:
            return "%s/wiki/%s" % (GITHUB, path) + ("#" + anchor if anchor else "")
        return href

    @staticmethod
    def fix_github(href):
        href = href.replace("/blob/master/", "/blob/main/").replace("doc/../", "")
        return href.replace("http://github.com/tbarbette/fastclick//", GITHUB + "/")

    def render_md(self, text, depth):
        md = markdown.Markdown(
            extensions=["extra", "toc", "sane_lists", "codehilite"],
            extension_configs={
                "toc": {"permalink": "§", "toc_depth": "2-3", "slugify": self.heading_id},
                "codehilite": {"guess_lang": False, "css_class": "codehilite"},
            })
        body = md.convert(text)
        toc = md.toc_tokens

        def fix_a(m):
            return '%s"%s"' % (m.group(1), html.escape(self.resolve(html.unescape(m.group(2)), depth)))

        body = re.sub(r'(<a [^>]*?href=)"([^"]*)"', fix_a, body)
        body = re.sub(r'(<img [^>]*?src=)"(images/[^"]*)"', lambda m: '%s"%s"' % (m.group(1), m.group(2)), body)
        body = re.sub(r'<img [^>]*src="(?!images/|https?:)[^"]*"[^>]*>', "", body)
        body = re.sub(r"<table>", '<div class="tablewrap"><table>', body)
        body = re.sub(r"</table>", "</table></div>", body)
        return body, toc

    @staticmethod
    def heading_id(value, sep):
        v = re.sub(r"[^\w\s-]", "", html.unescape(value).lower()).strip()
        return re.sub(r"[\s_-]+", sep, v)

    # ------------------------------------------------------------- template
    def layout(self, *, title, body, depth, active, sidebar="", toc="", description="", wide=False):
        root = "../" * depth
        nav = "".join(
            '<a href="%s%s"%s>%s</a>' % (root, href, ' class="on"' if label == active else "", label)
            for label, href in NAV)
        api = '<a href="%sapi/index.html">API</a>' % root if self.a.api else ""
        full_title = "FastClick" if title == "FastClick" else "%s — FastClick" % title
        layout_cls = "page" + (" wide" if wide else "") + (" has-side" if sidebar else "")
        return """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{full_title}</title>
<meta name="description" content="{desc}">
<link rel="icon" href="{root}static/favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="{root}static/style.css">
<script>document.documentElement.dataset.root="{root}";</script>
<script defer src="{root}static/site.js"></script>
</head>
<body>
<header class="top">
  <a class="brand" href="{root}index.html"><span class="mark">Fast</span>Click</a>
  <nav>{nav}{api}<a href="{github}">GitHub</a></nav>
  <div class="search"><input id="q" type="search" placeholder="Search docs and elements  ( / )" autocomplete="off" aria-label="Search"><ul id="results" hidden></ul></div>
</header>
<div class="{layout_cls}">
{sidebar}
<main>
{body}
</main>
{toc}
</div>
<footer>
  <p>FastClick is an extended version of the <a href="https://github.com/kohler/click">Click Modular Router</a>, released under the Click license.
  Source at <a href="{github}">github.com/tbarbette/fastclick</a>. This site is regenerated by CI from the repository sources and the wiki.</p>
</footer>
</body>
</html>
""".format(full_title=html.escape(full_title), desc=html.escape(description), root=root, nav=nav + "",
           api=api, github=GITHUB, layout_cls=layout_cls, sidebar=sidebar, body=body, toc=toc)

    def sidebar_docs(self, current, depth):
        root = "../" * depth
        out = ['<aside class="side"><details open><summary>Menu</summary>']
        used = set()
        for group, items in DOC_GROUPS:
            lis = []
            for label, slug in items:
                key = slug[1:] if slug.startswith("@") else slug
                href = self.pages.get(key.lower())
                if not href:
                    continue
                used.add(key.lower())
                lis.append('<li><a href="%s%s"%s>%s</a></li>' % (
                    root, href, ' class="on"' if href == current else "", html.escape(label)))
            if lis:
                out.append("<h4>%s</h4><ul>%s</ul>" % (group, "".join(lis)))
        rest = [s for s in sorted(self.wiki) if s.lower() not in used and s.lower() != "home"]
        if rest:
            lis = "".join('<li><a href="%s%s"%s>%s</a></li>' % (
                root, self.pages[s.lower()], ' class="on"' if self.pages[s.lower()] == current else "",
                html.escape(s.replace("-", " "))) for s in rest)
            out.append("<h4>More</h4><ul>%s</ul>" % lis)
        out.append("</details></aside>")
        return "".join(out)

    @staticmethod
    def toc_html(tokens):
        def walk(items):
            s = "".join('<li><a href="#%s">%s</a>%s</li>' % (
                t["id"], strip_tags(t["name"]), walk(t["children"]) if t["children"] else "") for t in items)
            return "<ul>%s</ul>" % s if s else ""
        # drop the page title (single h1) and promote its children
        if len(tokens) == 1 and tokens[0]["level"] == 1:
            tokens = tokens[0]["children"]
        inner = walk(tokens)
        return '<aside class="toc"><h4>On this page</h4>%s</aside>' % inner if inner else ""

    # --------------------------------------------------------------- pages
    def emit_doc(self, out_path, title, text, active="Documentation", description=""):
        depth = out_path.count("/")
        body, toc = self.render_md(text, depth)
        # the first <h1> is the page title; drop a duplicate coming from the Markdown itself
        body = re.sub(r"^\s*<h1[^>]*>.*?</h1>", "", body, count=1, flags=re.S)
        body = '<article class="doc"><h1>%s</h1>%s</article>' % (html.escape(title), body)
        page = self.layout(title=title, body=body, depth=depth, active=active,
                           sidebar=self.sidebar_docs(out_path, depth), toc=self.toc_html(toc),
                           description=description)
        write(os.path.join(self.a.out, out_path), page)
        self.titles[out_path] = title
        plain = strip_tags(body)
        self.search.append(dict(t=title, u=out_path, k="Documentation", d=plain[:220]))

    def build_docs(self):
        for slug, text in self.wiki.items():
            if slug.lower() == "home":
                continue
            title = slug.replace("-", " ")
            title = title[:1].upper() + title[1:]
            self.emit_doc("docs/%s.html" % slugify(slug), title, text)
        for key, (path, title) in REPO_PAGES.items():
            p = os.path.join(self.a.repo, path)
            if os.path.exists(p):
                self.emit_doc("docs/%s.html" % key, title, read(p))
        self.build_docs_index()

    def build_docs_index(self):
        root = "../"
        cards = []
        for group, items in DOC_GROUPS:
            lis = []
            for label, slug in items:
                key = slug[1:] if slug.startswith("@") else slug
                href = self.pages.get(key.lower())
                if href:
                    lis.append('<li><a href="%s%s">%s</a></li>' % (root, href, html.escape(label)))
            cards.append('<section><h2>%s</h2><ul class="plain">%s</ul></section>' % (group, "".join(lis)))
        body = ('<article class="doc"><h1>Documentation</h1>'
                '<p class="lead">Guides, tutorials and internals notes. These pages are the project wiki, '
                'rebuilt on every change. Per-element reference is under <a href="%selements/index.html">Elements</a>.</p>'
                '<div class="cols">%s</div>%s</article>') % (
                    root, "".join(cards),
                    read(os.path.join(SITE_DIR, "docs-extra.html")).replace("{{R}}", root))
        write(os.path.join(self.a.out, "docs/index.html"),
              self.layout(title="Documentation", body=body, depth=1, active="Documentation",
                          sidebar=self.sidebar_docs("docs/index.html", 1)))

    def build_elements(self):
        by_cat = {}
        for name, e in self.elements.items():
            by_cat.setdefault(e["category"], []).append(name)
        for name, e in self.elements.items():
            out_path = self.pages[name.lower()]
            text = e["md"]
            text = re.sub(r"^.*Element Documentation\n=+\n", "", text, count=1)
            text = re.sub(r"NAME\n-+\n.*?(?=\nSYNOPSIS\n)", "", text, count=1, flags=re.S)
            body, toc = self.render_md(text, 1)
            badge = '<a class="badge" href="index.html#cat-%s">%s</a>' % (
                slugify(e["category"].lower()), html.escape(e["category"]))
            body = ('<article class="doc element"><p class="crumb"><a href="index.html">Elements</a> / %s</p>'
                    '<h1><code>%s</code></h1><p class="lead">%s</p>%s</article>') % (
                        badge, html.escape(name), html.escape(e["desc"]), body)
            page = self.layout(title=name, body=body, depth=1, active="Elements",
                               toc=self.toc_html(toc), description=e["desc"])
            write(os.path.join(self.a.out, out_path), page)
            self.search.append(dict(t=name, u=out_path, k="Element · " + e["category"], d=e["desc"]))
        # index
        letters = []
        sections = []
        for cat in sorted(by_cat, key=lambda c: (c == "Other", c.lower())):
            items = "".join(
                '<li><a href="%s.html"><code>%s</code></a><span>%s</span></li>' % (
                    slugify(n), html.escape(n), html.escape(self.elements[n]["desc"]))
                for n in sorted(by_cat[cat], key=str.lower))
            sections.append('<section id="cat-%s"><h2>%s <small>%d</small></h2><ul class="elist">%s</ul></section>' % (
                slugify(cat.lower()), html.escape(cat), len(by_cat[cat]), items))
            letters.append('<a href="#cat-%s">%s</a>' % (slugify(cat.lower()), html.escape(cat)))
        body = ('<article class="doc"><h1>Elements</h1>'
                '<p class="lead">%d elements, generated from the documentation comments in the source tree. '
                'Use the search box to find one by name or description.</p>'
                '<p class="catnav">%s</p>%s</article>') % (len(self.elements), " · ".join(letters), "".join(sections))
        write(os.path.join(self.a.out, "elements/index.html"),
              self.layout(title="Elements", body=body, depth=1, active="Elements"))

    def build_reference(self):
        edir = self.a.elements
        rows = []
        for name, label, desc in REFERENCE:
            p = os.path.join(edir, name + ".md")
            if not os.path.exists(p):
                continue
            self.emit_ref("reference/%s.html" % name, name if name != "Userlevel" else "click (userlevel driver)", read(p))
            rows.append('<li><a href="%s.html"><code>%s</code></a><span>%s</span></li>' % (name, html.escape(label), html.escape(desc)))
        trows = []
        for t in TOOLS:
            p = os.path.join(edir, t + ".md")
            if os.path.exists(p):
                self.emit_ref("reference/%s.html" % t, t, read(p))
                trows.append('<li><a href="%s.html"><code>%s</code></a></li>' % (t, t))
        body = ('<article class="doc"><h1>Reference</h1>'
                '<p class="lead">The command-line driver, the configuration language and the Click toolchain.</p>'
                '<h2>Driver and language</h2><ul class="elist">%s</ul>'
                '<h2>Tools</h2><ul class="plain cols3">%s</ul>%s</article>') % (
                    "".join(rows), "".join(trows),
                    '<h2>C++ API</h2><p>Class-level documentation is generated with Doxygen: '
                    '<a href="../api/index.html">browse the API reference</a>.</p>' if self.a.api else "")
        write(os.path.join(self.a.out, "reference/index.html"),
              self.layout(title="Reference", body=body, depth=1, active="Reference"))

    def emit_ref(self, out_path, title, text):
        body, toc = self.render_md(text, 1)
        body = re.sub(r"^\s*<h1[^>]*>.*?</h1>", "", body, count=1, flags=re.S)
        body = '<article class="doc"><p class="crumb"><a href="index.html">Reference</a></p><h1>%s</h1>%s</article>' % (
            html.escape(title), body)
        write(os.path.join(self.a.out, out_path),
              self.layout(title=title, body=body, depth=1, active="Reference", toc=self.toc_html(toc)))
        self.search.append(dict(t=title, u=out_path, k="Reference", d=strip_tags(body)[:200]))

    def build_landing(self):
        text = read(os.path.join(SITE_DIR, "landing.html"))
        text = text.replace("{{ELEMENT_COUNT}}", str(len(self.elements)))
        write(os.path.join(self.a.out, "index.html"),
              self.layout(title="FastClick", body=text, depth=0, active="Overview", wide=True,
                          description="FastClick: a high-performance packet processing framework extending the Click Modular Router "
                                      "with DPDK and Netmap I/O, batching, flow management and automatic multi-core scaling."))

    # ----------------------------------------------------------------- run
    def run(self):
        out = self.a.out
        if os.path.exists(out):
            shutil.rmtree(out)
        os.makedirs(out)
        self.load_wiki()
        self.load_elements()
        shutil.copytree(os.path.join(SITE_DIR, "static"), os.path.join(out, "static"))
        img = os.path.join(self.a.wiki, "images")
        if os.path.isdir(img):
            shutil.copytree(img, os.path.join(out, "docs", "images"))
        self.build_docs()
        self.build_elements()
        self.build_reference()
        self.build_landing()
        if self.a.api and os.path.isdir(self.a.api):
            shutil.copytree(self.a.api, os.path.join(out, "api"))
        write(os.path.join(out, "search-index.json"), json.dumps(self.search, separators=(",", ":")))
        write(os.path.join(out, "CNAME"), "www.fastclick.dev\n")
        write(os.path.join(out, ".nojekyll"), "")
        print("built %d elements, %d wiki pages, %d search entries" % (
            len(self.elements), len(self.wiki), len(self.search)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wiki", required=True)
    ap.add_argument("--elements", required=True)
    ap.add_argument("--repo", default=os.path.join(SITE_DIR, ".."))
    ap.add_argument("--api")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    args.repo = os.path.abspath(args.repo)
    Site(args).run()


if __name__ == "__main__":
    sys.exit(main())
