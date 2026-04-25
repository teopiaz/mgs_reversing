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


def render_page(title: str, body_html: str) -> str:
    css = """
:root {
  --bg: #f7f7f4;
  --fg: #1b1b1b;
  --muted: #5a5a5a;
  --panel: #ffffff;
  --line: #deded6;
  --accent: #3b6d5a;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  padding: 32px 18px;
  background: radial-gradient(circle at top, #ffffff 0%, var(--bg) 55%);
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
h1, h2, h3, h4 { line-height: 1.2; margin-top: 1.4em; }
h1 { margin-top: 0; }
a { color: var(--accent); text-decoration-thickness: 1px; }
pre {
  background: #f2f3ef;
  border: 1px solid var(--line);
  border-radius: 8px;
  overflow-x: auto;
  padding: 12px;
}
code { font: 0.92em/1.5 Menlo, Monaco, Consolas, monospace; }
blockquote {
  margin: 0;
  padding-left: 14px;
  border-left: 3px solid #c9d7d0;
  color: var(--muted);
}
table { border-collapse: collapse; }
th, td {
  border: 1px solid var(--line);
  padding: 6px 8px;
  text-align: left;
}
"""
    safe_title = html.escape(title)
    return f"""<!doctype html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\" />
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />
  <title>{safe_title}</title>
  <style>{css}</style>
</head>
<body>
  <main>
{body_html}
  </main>
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
        page = render_page(rel.stem, body)
        out_file.write_text(page, encoding="utf-8")
        print(f"Wrote {out_file.relative_to(root)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())