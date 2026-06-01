#!/usr/bin/env python3
"""Build standalone HTML files from Markdown sources.

Usage:
  python3 build_html.py
"""

from __future__ import annotations

import html
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit, urlunsplit


def load_markdown_module():
    try:
        import markdown  # type: ignore
    except ImportError:
        print(
            "Missing dependency: markdown\n"
            "Install it with: python3 -m pip install markdown",
            file=sys.stderr,
        )
        raise SystemExit(1)

    return markdown


def find_markdown_files(root: Path) -> list[Path]:
    return sorted(
        p
        for p in root.rglob("*.md")
        if "html" not in p.parts and not p.name.startswith(".")
    )


def rewrite_md_link_target(href: str) -> str:
    """Convert local .md hrefs to .html while preserving query/fragment."""
    if not href:
        return href

    parts = urlsplit(href)
    if parts.scheme or href.startswith("#"):
        return href

    path = parts.path
    if not path.lower().endswith(".md"):
        return href

    html_path = f"{path[:-3]}.html"
    return urlunsplit((parts.scheme, parts.netloc, html_path, parts.query, parts.fragment))


def rewrite_internal_links(page_html: str) -> str:
    href_re = re.compile(r'href="([^"]+)"')

    def repl(match: re.Match[str]) -> str:
        original_href = match.group(1)
        rewritten_href = rewrite_md_link_target(original_href)
        return f'href="{rewritten_href}"'

    return href_re.sub(repl, page_html)


def build_breadcrumb(rel: Path) -> str:
    """Build a breadcrumb nav HTML for the page at relative path `rel`.

    Every page gets a `Home › …` chain leading to its location. Folder
    `index.md` pages have the folder itself as the current segment;
    other pages have the page stem as current. The root `index.md`
    collapses to a single `Home` current segment.

    All links are relative to the page's directory so the site works
    from `file://` URLs (no server prefix assumptions).
    """
    depth = len(rel.parts) - 1
    prefix = "../" * depth if depth > 0 else ""
    is_index = rel.stem == "index"

    # Root of the tree: this IS the home page.
    if depth == 0 and is_index:
        return '<nav class="breadcrumb"><span class="current">Home</span></nav>'

    parts = list(rel.parent.parts)
    if is_index and parts:
        # Folder index page — current segment is the folder name; ancestors
        # form the link chain.
        leaf_label: str | None = parts[-1]
        trail = parts[:-1]
    else:
        leaf_label = rel.stem
        trail = parts

    segments = [f'<a href="{prefix}index.html">Home</a>']
    cumulative = ""
    for part in trail:
        cumulative = f"{cumulative}/{part}" if cumulative else part
        href = f"{prefix}{cumulative}/index.html"
        segments.append(f'<a href="{href}">{html.escape(part)}</a>')

    if leaf_label is not None:
        segments.append(f'<span class="current">{html.escape(leaf_label)}</span>')

    sep = ' <span class="sep">›</span> '
    return '<nav class="breadcrumb" aria-label="Breadcrumb">' + sep.join(segments) + '</nav>'


def render_page(title: str, body_html: str, breadcrumb_html: str = "") -> str:
    # Light and dark palettes share the same CSS-variable contract; the
    # initial values are the light theme. A `prefers-color-scheme: dark`
    # media query overrides them, and a small inline script honors a
    # `?theme=light|dark` query string or a `mgs-doc-theme` localStorage
    # entry so the user can pin a manual choice via the corner toggle.
    css = """
:root {
  --bg: #f7f7f4;
  --bg-grad: #ffffff;
  --fg: #1b1b1b;
  --muted: #5a5a5a;
  --panel: #ffffff;
  --line: #deded6;
  --accent: #3b6d5a;
  --code-bg: #f2f3ef;
  --quote-line: #c9d7d0;
  --toggle-bg: rgba(255, 255, 255, 0.85);
  --toggle-border: #d4d4cc;
  --toggle-fg: #1b1b1b;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #0f1115;
    --bg-grad: #181c22;
    --fg: #e6e7e9;
    --muted: #9a9d9e;
    --panel: #161a20;
    --line: #2a2f37;
    --accent: #88c0aa;
    --code-bg: #0c0f13;
    --quote-line: #3a5750;
    --toggle-bg: rgba(22, 26, 32, 0.85);
    --toggle-border: #2a2f37;
    --toggle-fg: #e6e7e9;
  }
}
:root[data-theme="dark"] {
  --bg: #0f1115;
  --bg-grad: #181c22;
  --fg: #e6e7e9;
  --muted: #9a9d9e;
  --panel: #161a20;
  --line: #2a2f37;
  --accent: #88c0aa;
  --code-bg: #0c0f13;
  --quote-line: #3a5750;
  --toggle-bg: rgba(22, 26, 32, 0.85);
  --toggle-border: #2a2f37;
  --toggle-fg: #e6e7e9;
}
* { box-sizing: border-box; }
html { color-scheme: light dark; }
body {
  margin: 0;
  padding: 32px 18px;
  background: radial-gradient(circle at top, var(--bg-grad) 0%, var(--bg) 55%);
  color: var(--fg);
  font: 17px/1.65 Georgia, "Times New Roman", serif;
}
main {
  max-width: 920px;
  margin: 0 auto;
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 10px;
  padding: 28px;
}
h1, h2, h3, h4 { line-height: 1.2; margin-top: 1.4em; color: var(--fg); }
h1 { margin-top: 0; }
a { color: var(--accent); text-decoration-thickness: 1px; }
pre {
  background: var(--code-bg);
  border: 1px solid var(--line);
  border-radius: 8px;
  overflow-x: auto;
  padding: 12px;
}
code { font: 0.92em/1.5 Menlo, Monaco, Consolas, monospace; color: var(--fg); }
pre code { color: var(--fg); }
blockquote {
  margin: 0;
  padding-left: 14px;
  border-left: 3px solid var(--quote-line);
  color: var(--muted);
}
table { border-collapse: collapse; }
th, td {
  border: 1px solid var(--line);
  padding: 6px 8px;
  text-align: left;
}
hr { border: none; border-top: 1px solid var(--line); margin: 24px 0; }
img { max-width: 100%; }
/* Breadcrumb strip at the top of every page. Spans the whole panel
   width so the eye reads it before the H1. */
.breadcrumb {
  font: 13px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  color: var(--muted);
  margin: -8px 0 22px 0;
  padding-bottom: 14px;
  border-bottom: 1px solid var(--line);
  word-break: break-word;
}
.breadcrumb a {
  color: var(--accent);
  text-decoration: none;
}
.breadcrumb a:hover { text-decoration: underline; }
.breadcrumb .current {
  color: var(--fg);
  font-weight: 600;
}
.breadcrumb .sep {
  color: var(--muted);
  margin: 0 2px;
}
/* Floating theme toggle in the top-right corner. Pure-CSS button; the
   inline script in <head> swaps :root[data-theme] and persists it. */
.theme-toggle {
  position: fixed;
  top: 16px;
  right: 16px;
  z-index: 100;
  background: var(--toggle-bg);
  color: var(--toggle-fg);
  border: 1px solid var(--toggle-border);
  border-radius: 999px;
  padding: 6px 14px;
  font: 13px/1 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  cursor: pointer;
  backdrop-filter: blur(8px);
}
.theme-toggle:hover { border-color: var(--accent); }
@media print {
  body { background: white; color: black; }
  main { border: none; }
  .theme-toggle { display: none; }
}
"""
    # The theme bootstrap runs synchronously in <head> so the right palette
    # paints on the very first frame (no flash-of-wrong-theme). It reads
    # `?theme=light` / `?theme=dark` first, then localStorage, then leaves
    # the prefers-color-scheme media query to decide. The toggle button
    # writes localStorage and updates `data-theme` live.
    bootstrap_js = """
(function () {
  var root = document.documentElement;
  var params = new URLSearchParams(window.location.search);
  var qp = params.get('theme');
  var saved = qp || localStorage.getItem('mgs-doc-theme');
  if (saved === 'light' || saved === 'dark') {
    root.setAttribute('data-theme', saved);
    if (qp) localStorage.setItem('mgs-doc-theme', qp);
  }
})();
"""
    toggle_js = """
(function () {
  var root = document.documentElement;
  var btn  = document.getElementById('theme-toggle');
  function effective() {
    var pinned = root.getAttribute('data-theme');
    if (pinned) return pinned;
    return window.matchMedia('(prefers-color-scheme: dark)').matches
      ? 'dark' : 'light';
  }
  function label() {
    btn.textContent = effective() === 'dark' ? '☼ Light' : '☾ Dark';
  }
  btn.addEventListener('click', function () {
    var next = effective() === 'dark' ? 'light' : 'dark';
    root.setAttribute('data-theme', next);
    localStorage.setItem('mgs-doc-theme', next);
    label();
  });
  label();
})();
"""
    safe_title = html.escape(title)
    return f"""<!doctype html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\" />
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />
  <title>{safe_title}</title>
  <style>{css}</style>
  <script>{bootstrap_js}</script>
</head>
<body>
  <button id=\"theme-toggle\" class=\"theme-toggle\" type=\"button\">Theme</button>
  <main>
{breadcrumb_html}
{body_html}
  </main>
  <script>{toggle_js}</script>
</body>
</html>
"""


def main() -> int:
    root = Path(__file__).resolve().parent
    out_root = root / "html"
    out_root.mkdir(exist_ok=True)

    markdown = load_markdown_module()
    md_files = find_markdown_files(root)
    if not md_files:
        print("No markdown files found.")
        return 0

    for src in md_files:
        rel = src.relative_to(root)
        out_file = out_root / rel.with_suffix(".html")
        out_file.parent.mkdir(parents=True, exist_ok=True)

        source_text = src.read_text(encoding="utf-8")
        body = markdown.markdown(
            source_text,
            extensions=["fenced_code", "tables", "toc", "sane_lists"],
        )
        body = rewrite_internal_links(body)
        crumbs = build_breadcrumb(rel)
        page = render_page(rel.stem, body, crumbs)
        out_file.write_text(page, encoding="utf-8")
        print(f"Wrote {out_file.relative_to(root)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())