#!/usr/bin/env python3
"""Generate the REPS2 roadmap progress dashboard (SVG, HTML, and JSON).

The roadmap is the sole source for checklist progress. Release-gate and
real-title claims are read from PROJECT_STATE.md so checklist completion is
never misrepresented as game compatibility or release readiness.
"""
from __future__ import annotations

import argparse
import html
import json
import re
from concurrent.futures import ThreadPoolExecutor
from io import BytesIO
from urllib.error import URLError
from urllib.request import Request, urlopen
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

MILESTONE_HEADER = re.compile(r"^##\s+(M\d+)\s+—\s+(.+?)\s*$")
CHECKBOX = re.compile(r"^\s*-\s+\[([ xX])\]\s+(.+?)\s*$")
GATE_LINE = re.compile(r"^\s*-\s*(Alpha|Public Beta|REPS2 1\.0):\s*(.+?)\s*$")

# Verified cover serials from xlenore/ps2-covers; artwork is hotlinked, not vendored.
COVER_SERIALS = (
    "SCUS-97328", "SCUS-97399", "SCUS-97472", "SLUS-20312",
    "SLUS-20341", "SLUS-20370", "SLUS-20635", "SLUS-20915",
    "SLUS-20946", "SLUS-20964", "SLUS-21050", "SLUS-21134",
    "SLUS-21267", "SLUS-21269",
)
COVER_IMAGE_BASE = (
    "https://raw.githubusercontent.com/xlenore/ps2-covers/main/"
    "covers/default/"
)
COVER_IMAGE_URLS = tuple(
    f"{COVER_IMAGE_BASE}{serial}.jpg" for serial in COVER_SERIALS
)


def parse_roadmap(source: str) -> dict[str, Any]:
    """Count checked and unchecked roadmap tasks, grouped by milestone."""
    sections: list[tuple[str, str, list[str]]] = []
    active: tuple[str, str, list[str]] | None = None

    for line in source.splitlines():
        header = MILESTONE_HEADER.match(line)
        if header:
            if active is not None:
                sections.append(active)
            active = (header.group(1), header.group(2), [])
        elif active is not None:
            active[2].append(line)

    if active is not None:
        sections.append(active)
    if not sections:
        raise ValueError("No milestone headings matching '## M0 — Name' were found.")

    milestones: list[dict[str, Any]] = []
    seen: set[str] = set()
    for milestone_id, title, lines in sections:
        if milestone_id in seen:
            raise ValueError(f"Duplicate roadmap milestone: {milestone_id}")
        seen.add(milestone_id)

        tasks = [match.group(1).lower() == "x" for line in lines
                 if (match := CHECKBOX.match(line))]
        completed = sum(tasks)
        total = len(tasks)
        milestones.append({
            "id": milestone_id,
            "title": title,
            "completed": completed,
            "total": total,
            "percent": round(completed * 100 / total) if total else 0,
        })

    milestones.sort(key=lambda item: int(item["id"][1:]))
    task_total = sum(item["total"] for item in milestones)
    completed_total = sum(item["completed"] for item in milestones)
    if task_total == 0:
        raise ValueError("The roadmap contains milestone headings but no checklist tasks.")

    return {
        "milestones": milestones,
        "totals": {
            "completed": completed_total,
            "total": task_total,
            "percent": round(completed_total * 100 / task_total),
        },
    }


def parse_release_gates(project_state: str) -> tuple[list[dict[str, str]], str]:
    """Read release-gate labels and the real-title evidence boundary."""
    gate_section = re.search(
        r"(?ms)^## Release-gate status\s*\n(.*?)(?=^## |\Z)", project_state
    )
    gates: list[dict[str, str]] = []
    if gate_section:
        for line in gate_section.group(1).splitlines():
            match = GATE_LINE.match(line)
            if match:
                details = match.group(2)
                status = details.split(";", 1)[0].strip().rstrip(".")
                gates.append({"label": match.group(1), "status": status})

    evidence_section = re.search(
        r"(?ms)^## M4 real-title evidence\s*\n(.*?)(?=^## |\Z)", project_state
    )
    evidence = evidence_section.group(1) if evidence_section else ""
    if re.search(r"No guest-visible boot stage (?:was )?demonstrated", evidence, re.I):
        real_title_stage = "No guest-visible boot stage demonstrated"
    elif evidence_section:
        real_title_stage = "See PROJECT_STATE.md for the latest evidence"
    else:
        real_title_stage = "Real-title evidence not recorded"

    return gates, real_title_stage


def build_dashboard_data(roadmap: str, project_state: str) -> dict[str, Any]:
    roadmap_data = parse_roadmap(roadmap)
    gates, real_title_stage = parse_release_gates(project_state)
    generated_at = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    return {
        "generated_at": generated_at,
        **roadmap_data,
        "release_gates": gates,
        "real_title_stage": real_title_stage,
        "metric_note": (
            "Checklist completion only; it is not a game compatibility, "
            "boot success, or release-readiness score."
        ),
    }


def _esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def _clip(value: str, limit: int = 43) -> str:
    return value if len(value) <= limit else value[: limit - 1].rstrip() + "…"


def render_svg(data: dict[str, Any], *, glass: bool = False) -> str:
    """Render a restrained, next-generation systems dashboard in SVG."""
    width = 1160
    totals = data["totals"]
    completed = totals["completed"]
    total = totals["total"]
    percent = totals["percent"]
    remaining = total - completed
    generated = datetime.fromisoformat(data["generated_at"]).strftime("%Y-%m-%d  %H:%M UTC")

    rows_top = 354
    row_height = 48
    rows_bottom = rows_top + len(data["milestones"]) * row_height
    gates_heading_y = rows_bottom + 34
    gates_top = gates_heading_y + 24
    stage_rule_y = gates_top + 66
    stage_label_y = stage_rule_y + 27
    stage_text_y = stage_label_y + 25
    footer_y = stage_text_y + 42
    height = footer_y + 28

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        "<title id=\"title\">REPS2 engineering progress</title>",
        f"<desc id=\"desc\">{_esc(completed)} of {_esc(total)} roadmap checklist tasks completed ({_esc(percent)} percent). Checklist progress is not game compatibility or release readiness.</desc>",
        """<defs>
          <linearGradient id="surface" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0" stop-color="#101a2d"/>
            <stop offset="1" stop-color="#090e19"/>
          </linearGradient>
          <linearGradient id="progress" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0" stop-color="#4de1ec"/>
            <stop offset="1" stop-color="#8b8cf8"/>
          </linearGradient>
          <radialGradient id="ambient" cx="1" cy="0" r="1">
            <stop offset="0" stop-color="#5269d8" stop-opacity=".17"/>
            <stop offset="1" stop-color="#5269d8" stop-opacity="0"/>
          </radialGradient>
          <pattern id="grid" width="32" height="32" patternUnits="userSpaceOnUse">
            <path d="M32 0H0V32" fill="none" stroke="#8aa4c8" stroke-opacity=".055" stroke-width="1"/>
          </pattern>
        </defs>""",
        f'<rect x="0.5" y="0.5" width="1159" height="{height - 1}" rx="18" fill="{"none" if glass else "url(#surface)"}" stroke="#426078" stroke-opacity="{0.62 if glass else 1}"/>',
        (f'<rect x="1" y="1" width="1158" height="{height - 2}" rx="17" fill="#050a12" fill-opacity=".035"/>' if glass else f'<rect x="1" y="1" width="1158" height="{height - 2}" rx="17" fill="url(#ambient)"/>'),
        (f'<rect x="1" y="1" width="1158" height="{height - 2}" rx="17" fill="url(#grid)" opacity=".12"/>' if glass else f'<rect x="1" y="1" width="1158" height="{height - 2}" rx="17" fill="url(#grid)"/>'),
        '<path d="M40 124H1120" stroke="#26364d" stroke-width="1"/>',
        '<circle cx="48" cy="42" r="4" fill="#67e8f9"/>',
        '<text x="62" y="46" fill="#9bb1cd" font-family="Consolas,Menlo,monospace" font-size="11" font-weight="600" letter-spacing="1.2">REPS2  /  NATIVE RECOMPILATION</text>',
        f'<text x="1120" y="46" text-anchor="end" fill="#7185a2" font-family="Consolas,Menlo,monospace" font-size="10">REPOSITORY STATE  •  {_esc(generated)}</text>',
        '<text x="40" y="91" fill="#eef5ff" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="31" font-weight="650" letter-spacing="-.7">Engineering progress</text>',
        '<text x="42" y="111" fill="#8da1bd" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="12">A live view of the REPS2 roadmap, implementation gates and real-title evidence.</text>',
        f'<rect x="40" y="148" width="1080" height="151" rx="13" fill="#050a12" fill-opacity="{0.34 if glass else 0.82}" stroke="#26364d"/>',
        '<text x="66" y="180" fill="#8fa6c3" font-family="Consolas,Menlo,monospace" font-size="10" font-weight="600" letter-spacing="1.4">ROADMAP COMPLETION</text>',
        f'<text x="64" y="245" fill="#e6f8ff" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="54" font-weight="700" letter-spacing="-2">{percent}<tspan fill="#67e8f9" font-size="27">%</tspan></text>',
        f'<text x="244" y="202" fill="#e2ebf8" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="15" font-weight="600">{completed:,} of {total:,} tasks completed</text>',
        '<text x="244" y="224" fill="#8499b5" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="12">Completion is measured from checked items in docs/ROADMAP.md</text>',
        '<rect x="244" y="244" width="650" height="8" rx="4" fill="#25364e"/>',
        f'<rect x="244" y="244" width="{650 * percent / 100:.1f}" height="8" rx="4" fill="url(#progress)"/>',
        '<path d="M920 174V272" stroke="#26364d" stroke-width="1"/>',
        f'<text x="952" y="211" fill="#f0f5ff" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="27" font-weight="650">{remaining:,}</text>',
        '<text x="952" y="232" fill="#8fa6c3" font-family="Consolas,Menlo,monospace" font-size="10" letter-spacing="1">OPEN ITEMS</text>',
        '<text x="40" y="333" fill="#e7effb" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="16" font-weight="650">Milestone map</text>',
        '<text x="1120" y="333" text-anchor="end" fill="#7388a5" font-family="Consolas,Menlo,monospace" font-size="10">IMPLEMENTATION TRACK  /  MILESTONES</text>',
        '<text x="64" y="350" fill="#748ba9" font-family="Consolas,Menlo,monospace" font-size="9" letter-spacing="1">STAGE</text>',
        '<text x="540" y="350" fill="#748ba9" font-family="Consolas,Menlo,monospace" font-size="9" letter-spacing="1">COMPLETION</text>',
        '<text x="1000" y="350" text-anchor="end" fill="#748ba9" font-family="Consolas,Menlo,monospace" font-size="9" letter-spacing="1">TASKS</text>',
        '<text x="1120" y="350" text-anchor="end" fill="#748ba9" font-family="Consolas,Menlo,monospace" font-size="9" letter-spacing="1">STATUS</text>',
    ]

    for index, milestone in enumerate(data["milestones"]):
        y = rows_top + index * row_height
        done = milestone["completed"]
        count = milestone["total"]
        pct = milestone["percent"]
        milestone_id = _esc(milestone["id"])
        title = _esc(_clip(milestone["title"], 55))
        if count and done == count:
            stage_status = "COMPLETE"
            stage_color = "#7ee7c0"
        elif done:
            stage_status = "ACTIVE"
            stage_color = "#67e8f9"
        else:
            stage_status = "PENDING"
            stage_color = "#8394ad"
        parts.extend([
            f'<rect x="40" y="{y}" width="1080" height="42" rx="7" fill="#050a12" fill-opacity="{0.20 if glass else 0.58}"/>',
            f'<text x="64" y="{y + 25}" fill="#67e8f9" font-family="Consolas,Menlo,monospace" font-size="11" font-weight="600">{milestone_id}</text>',
            f'<text x="112" y="{y + 25}" fill="#dce7f6" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="13" font-weight="500">{title}</text>',
            f'<rect x="540" y="{y + 17}" width="350" height="6" rx="3" fill="#293a52"/>',
            f'<rect x="540" y="{y + 17}" width="{350 * pct / 100:.1f}" height="6" rx="3" fill="url(#progress)"/>',
            f'<text x="1000" y="{y + 25}" text-anchor="end" fill="#a9bad0" font-family="Consolas,Menlo,monospace" font-size="10">{done} / {count}</text>',
            f'<circle cx="1030" cy="{y + 21}" r="3" fill="{stage_color}"/>',
            f'<text x="1042" y="{y + 25}" fill="{stage_color}" font-family="Consolas,Menlo,monospace" font-size="9" letter-spacing=".4">{stage_status}</text>',
        ])

    parts.append(
        f'<text x="40" y="{gates_heading_y}" fill="#e7effb" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="16" font-weight="650">Release gates</text>'
    )
    parts.append(
        f'<text x="1120" y="{gates_heading_y}" text-anchor="end" fill="#7388a5" font-family="Consolas,Menlo,monospace" font-size="10">PRODUCT READINESS</text>'
    )

    gates = data["release_gates"] or [{"label": "Release gates", "status": "not recorded"}]
    column_width = 360
    for index, gate in enumerate(gates[:3]):
        x = 40 + index * 370
        status = gate["status"]
        status_color = "#7ee7c0" if status.lower() in {"met", "claimed"} else "#f6c777"
        parts.extend([
            f'<rect x="{x}" y="{gates_top}" width="{column_width}" height="54" rx="8" fill="#050a12" fill-opacity="{0.30 if glass else 1}" stroke="#426078" stroke-opacity=".68"/>',
            f'<text x="{x + 15}" y="{gates_top + 20}" fill="#8298b5" font-family="Consolas,Menlo,monospace" font-size="9" font-weight="600" letter-spacing=".8">{_esc(gate["label"]).upper()}</text>',
            f'<text x="{x + 15}" y="{gates_top + 40}" fill="{status_color}" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="12" font-weight="600">{_esc(_clip(status, 43))}</text>',
        ])

    stage = _esc(_clip(data["real_title_stage"], 112))
    parts.extend([
        f'<path d="M40 {stage_rule_y}H1120" stroke="#26364d" stroke-width="1"/>',
        f'<text x="40" y="{stage_label_y}" fill="#8298b5" font-family="Consolas,Menlo,monospace" font-size="9" font-weight="600" letter-spacing="1">REAL-TITLE EVIDENCE</text>',
        f'<text x="40" y="{stage_text_y}" fill="#d5e2f3" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="12">{stage}</text>',
        f'<text x="40" y="{footer_y}" fill="#70839f" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="10">{_esc(data["metric_note"])}</text>',
        f'<text x="1120" y="{footer_y}" text-anchor="end" fill="#536985" font-family="Consolas,Menlo,monospace" font-size="9">REPS2 // STATUS SNAPSHOT</text>',
        "</svg>",
    ])
    return "\n".join(parts) + "\n"


def render_index_html() -> str:
    """Render the full dashboard with an upstream PS2-cover wallpaper."""
    cover_tiles = "\n".join(
        f'<div class="cover-tile"><img src="{url}" alt="" loading="eager" decoding="async" referrerpolicy="no-referrer"></div>'
        for url in (*COVER_IMAGE_URLS, *reversed(COVER_IMAGE_URLS))
    )
    page = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#050a12">
  <title>REPS2 — Engineering progress</title>
  <style>
    :root {
      color-scheme: dark;
      background: #050a12;
      color: #e7effb;
      font-family: Inter, "Segoe UI", Arial, sans-serif;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      padding: 36px 20px;
      overflow-x: hidden;
      position: relative;
      isolation: isolate;
      background: #050a12;
    }
    .cover-wall {
      position: fixed;
      z-index: -1;
      inset: 0;
      overflow: hidden;
      background: #050a12;
    }
    .cover-grid {
      position: absolute;
      inset: -10vh -7vw;
      display: grid;
      grid-template-columns: repeat(7, minmax(0, 1fr));
      grid-auto-rows: clamp(210px, 29vh, 320px);
      gap: 10px;
      transform: rotate(-3deg) scale(1.12);
      filter: saturate(.82) brightness(.72);
    }
    .cover-tile {
      min-width: 0;
      overflow: hidden;
      border: 1px solid rgba(174, 204, 240, .12);
      border-radius: 9px;
      background: linear-gradient(145deg, #18243a, #0b1220);
    }
    .cover-tile img {
      display: block;
      width: 100%;
      height: 100%;
      object-fit: cover;
      opacity: .88;
    }
    .wall-shade {
      position: absolute;
      inset: 0;
      background:
        radial-gradient(ellipse at 50% 18%, rgba(16, 42, 73, .12), transparent 55%),
        linear-gradient(180deg, rgba(2, 6, 13, .48), rgba(2, 6, 13, .72));
      backdrop-filter: none;
    }
    main {
      width: min(1160px, 100%);
      margin: 0 auto;
      padding: 12px;
      border: 1px solid rgba(120, 160, 210, .28);
      border-radius: 16px;
      background: rgba(5, 10, 18, .30);
      box-shadow: 0 24px 80px rgba(0, 0, 0, .38);
      backdrop-filter: blur(3px) saturate(110%);
      -webkit-backdrop-filter: blur(3px) saturate(110%);
    }
    main > img {
      display: block;
      width: 100%;
      height: auto;
    }
    footer {
      margin: 0 12px;
      padding: 14px 8px 8px;
      border-top: 1px solid rgba(120, 160, 210, .20);
      color: #9aabc2;
      font-size: 12px;
      line-height: 1.7;
    }
    a { color: #78e7f2; text-decoration: none; }
    a:hover { color: #b8f5fb; text-decoration: underline; }
    @supports not ((backdrop-filter: blur(1px))) {
      main { background: rgba(5, 10, 18, .88); }
    }
    @media (max-width: 760px) {
      body { padding: 12px 6px; }
      .cover-grid {
        grid-template-columns: repeat(4, minmax(0, 1fr));
        grid-auto-rows: 190px;
        gap: 7px;
      }
      main { padding: 5px; border-radius: 10px; }
      footer { margin: 0 4px; }
    }
    @media (prefers-reduced-transparency: reduce) {
      main { background: rgba(5, 10, 18, .92); backdrop-filter: none; }
      .wall-shade { background: rgba(2, 6, 13, .78); backdrop-filter: none; }
    }
  </style>
</head>
<body>
  <div class="cover-wall" aria-hidden="true">
    <div class="cover-grid">
      {{COVER_TILES}}
    </div>
    <div class="wall-shade"></div>
  </div>
  <main>
    <img src="progress-overlay.svg" alt="REPS2 engineering progress dashboard">
    <footer>
      Live status generated from
      <a href="https://github.com/RidTheWann/REPS2-Project/blob/main/docs/ROADMAP.md">the engineering roadmap</a>
      and <a href="https://github.com/RidTheWann/REPS2-Project/blob/main/docs/PROJECT_STATE.md">verified project state</a>.
      Checklist completion is not a game compatibility or release-readiness score.
      <a href="progress.json">Progress data (JSON)</a>.
      Cover artwork is loaded directly from
      <a href="https://github.com/xlenore/ps2-covers">xlenore/ps2-covers</a>;
      images remain hosted by the upstream project and are not vendored here; artwork remains the property of its respective rights holders.
    </footer>
  </main>
  <script>
    document.querySelectorAll(".cover-tile img").forEach((image) => {
      image.addEventListener("error", () => image.closest(".cover-tile")?.remove(), { once: true });
    });
  </script>
</body>
</html>
"""
    return page.replace("{{COVER_TILES}}", cover_tiles)


def _download_cover_asset(url: str) -> bytes | None:
    """Fetch one upstream cover with a strict timeout and response-size cap."""
    request = Request(
        url,
        headers={"User-Agent": "REPS2-Progress-Dashboard/1.0"},
    )
    try:
        with urlopen(request, timeout=10) as response:
            payload = response.read(2_000_001)
        if not payload or len(payload) > 2_000_000:
            return None
        return payload
    except (OSError, URLError, TimeoutError, ValueError):
        return None


def download_cover_assets() -> list[bytes]:
    """Download cover thumbnails transiently for the README PNG composite."""
    with ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(_download_cover_asset, COVER_IMAGE_URLS))
    assets = [payload for payload in results if payload is not None]
    if not assets:
        raise RuntimeError(
            "Unable to download any PS2 cover thumbnails from xlenore/ps2-covers."
        )
    if len(assets) != len(COVER_IMAGE_URLS):
        print(
            f"Warning: downloaded {len(assets)}/{len(COVER_IMAGE_URLS)} "
            "PS2 cover thumbnails; missing covers will be filled by available images."
        )
    return assets


def render_readme_png(
    data: dict[str, Any],
    *,
    cover_assets: list[bytes] | None = None,
) -> bytes:
    """Rasterize the transparent SVG over a PS2-cover wall for GitHub README.

    GitHub/browser SVG image contexts do not reliably load external SVG
    sub-resources. A PNG composite keeps the game-cover wallpaper visible in
    README while the standalone SVG and HTML dashboard remain available.
    """
    try:
        import cairosvg
        from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageOps
    except ImportError as exc:
        raise RuntimeError(
            "README preview generation requires CairoSVG and Pillow; "
            "install scripts/dashboard-requirements.txt."
        ) from exc

    if cover_assets is None:
        cover_assets = download_cover_assets()
    if not cover_assets:
        raise ValueError("At least one cover image is required for the README preview.")

    glass_svg = render_svg(data, glass=True)
    svg_root = ET.fromstring(glass_svg)
    width = int(svg_root.attrib["width"])
    height = int(svg_root.attrib["height"])

    # Match the Pages wallpaper's seven-column PS2 cover mosaic.
    columns = 7
    tile_width = 174
    tile_height = 236
    gap = 10
    rows = max(4, (height + tile_height + gap - 1) // (tile_height + gap) + 1)
    wall_width = columns * tile_width + (columns + 1) * gap
    wall_height = rows * tile_height + (rows + 1) * gap
    wall = Image.new("RGB", (wall_width, wall_height), (5, 10, 18))
    draw = ImageDraw.Draw(wall)

    for index in range(rows * columns):
        payload = cover_assets[index % len(cover_assets)]
        try:
            with Image.open(BytesIO(payload)) as source:
                cover = ImageOps.fit(
                    source.convert("RGB"),
                    (tile_width, tile_height),
                    method=Image.Resampling.LANCZOS,
                    centering=(0.5, 0.5),
                )
        except (OSError, ValueError):
            continue

        cover = ImageEnhance.Color(cover).enhance(0.82)
        cover = ImageEnhance.Brightness(cover).enhance(0.72)
        x = gap + (index % columns) * (tile_width + gap)
        y = gap + (index // columns) * (tile_height + gap)

        mask = Image.new("L", (tile_width, tile_height), 0)
        ImageDraw.Draw(mask).rounded_rectangle(
            (0, 0, tile_width - 1, tile_height - 1), radius=9, fill=255
        )
        wall.paste(cover, (x, y), mask)
        draw.rounded_rectangle(
            (x, y, x + tile_width - 1, y + tile_height - 1),
            radius=9,
            outline=(174, 204, 240),
            width=1,
        )

    # Slight angle and restrained dark veil retain the same atmosphere as Pages.
    wall = wall.rotate(
        -3,
        resample=Image.Resampling.BICUBIC,
        expand=False,
        fillcolor=(5, 10, 18),
    )
    crop_left = max(0, (wall_width - width) // 2)
    crop_top = max(0, (wall_height - height) // 2)
    wall = wall.crop((crop_left, crop_top, crop_left + width, crop_top + height))

    shade = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    shade_pixels = shade.load()
    for y in range(height):
        ratio = y / max(1, height - 1)
        alpha = round(122 + ratio * 46)
        for x in range(width):
            shade_pixels[x, y] = (2, 6, 13, alpha)
    background = Image.alpha_composite(wall.convert("RGBA"), shade)
    background = background.filter(ImageFilter.GaussianBlur(radius=3))

    overlay_png = cairosvg.svg2png(
        bytestring=glass_svg.encode("utf-8"),
        output_width=width,
        output_height=height,
    )
    overlay = Image.open(BytesIO(overlay_png)).convert("RGBA")
    composite = Image.alpha_composite(background, overlay)

    output = BytesIO()
    composite.save(output, format="PNG", optimize=True)
    return output.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--roadmap", type=Path, default=Path("docs/ROADMAP.md"))
    parser.add_argument("--project-state", type=Path, default=Path("docs/PROJECT_STATE.md"))
    parser.add_argument("--output-dir", type=Path, default=Path("dist/progress"))
    args = parser.parse_args()

    data = build_dashboard_data(
        args.roadmap.read_text(encoding="utf-8"),
        args.project_state.read_text(encoding="utf-8"),
    )
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "progress.svg").write_text(render_svg(data), encoding="utf-8")
    (args.output_dir / "progress-overlay.svg").write_text(
        render_svg(data, glass=True), encoding="utf-8"
    )
    (args.output_dir / "progress.png").write_bytes(render_readme_png(data))
    (args.output_dir / "progress.json").write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    (args.output_dir / "index.html").write_text(render_index_html(), encoding="utf-8")
    print(
        f"Generated dashboard: {data['totals']['completed']}/{data['totals']['total']} "
        f"tasks ({data['totals']['percent']}%) across {len(data['milestones'])} milestones."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
