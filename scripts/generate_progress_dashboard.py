#!/usr/bin/env python3
"""Generate the REPS2 roadmap checklist grid dashboard (SVG, HTML, and JSON).

The roadmap checklist is the only source for implementation colors and
completion counts. Release-gate and real-title notes come from PROJECT_STATE.md.
The generator intentionally uses only the Python standard library.
"""
from __future__ import annotations

import argparse
import html
import json
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

MILESTONE_HEADER = re.compile(r"^##\s+(M\d+)\s+—\s+(.+?)\s*$")
CHECKBOX = re.compile(r"^\s*-\s+\[([ xX])\]\s+(.+?)\s*$")
GATE_LINE = re.compile(r"^\s*-\s*(Alpha|Public Beta|REPS2 1\.0):\s*(.+?)\s*$")

WIDTH = 1200
HEIGHT = 920
IMPLEMENTED_FILL = "#126B43"
IMPLEMENTED_STROKE = "#48D597"
OPEN_FILL = "#26364A"
OPEN_STROKE = "#647A96"


def parse_roadmap(source: str) -> dict[str, Any]:
    """Parse milestone checklist items and calculate weighted completion."""
    milestones: list[dict[str, Any]] = []
    active: dict[str, Any] | None = None

    for line in source.splitlines():
        header = MILESTONE_HEADER.match(line)
        if header:
            if active is not None:
                milestones.append(active)
            active = {
                "id": header.group(1),
                "title": header.group(2),
                "completed": 0,
                "total": 0,
                "tasks": [],
            }
            continue

        if active is None:
            continue
        match = CHECKBOX.match(line)
        if match is None:
            continue

        implemented = match.group(1).lower() == "x"
        task = {
            "title": match.group(2),
            "implemented": implemented,
        }
        active["tasks"].append(task)
        active["total"] += 1
        active["completed"] += int(implemented)

    if active is not None:
        milestones.append(active)
    if not milestones:
        raise ValueError("No roadmap milestones matching '## M0 — Name' were found.")

    seen: set[str] = set()
    for milestone in milestones:
        if milestone["id"] in seen:
            raise ValueError(f"Duplicate roadmap milestone: {milestone['id']}")
        seen.add(milestone["id"])
        milestone["percent"] = (
            round(milestone["completed"] * 100 / milestone["total"])
            if milestone["total"] else 0
        )

    milestones.sort(key=lambda item: int(item["id"][1:]))
    task_total = sum(item["total"] for item in milestones)
    completed_total = sum(item["completed"] for item in milestones)
    if task_total == 0:
        raise ValueError("The roadmap contains milestones but no checklist tasks.")

    return {
        "milestones": milestones,
        "totals": {
            "completed": completed_total,
            "total": task_total,
            "percent": round(completed_total * 100 / task_total),
            "open": task_total - completed_total,
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
    milestones = parse_roadmap(roadmap)
    gates, real_title_stage = parse_release_gates(project_state)
    return {
        "generated_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        **milestones,
        "release_gates": gates,
        "real_title_stage": real_title_stage,
        "metric_note": (
            "Checklist completion only; it is not a game compatibility, "
            "boot-success, or release-readiness score."
        ),
    }


def _esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def _clip(value: str, limit: int) -> str:
    return value if len(value) <= limit else value[:limit - 1].rstrip() + "…"



def render_progress_grid_svg(data: dict[str, Any]) -> str:
    """Render a milestone grid with subtle hand-drawn pencil texture."""
    totals = data["totals"]
    completed = totals["completed"]
    total = totals["total"]
    percent = totals["percent"]
    milestones = data["milestones"]

    margin = 30
    grid_width = WIDTH - (margin * 2)
    columns = 16
    column_gap = 4
    row_gap = 4
    cell_height = 24
    cell_width = (grid_width - column_gap * (columns - 1)) / columns
    section_header_height = 20
    section_gap = 10
    first_section_y = 184

    section_layout: list[tuple[dict[str, Any], int, float, float]] = []
    cursor_y = first_section_y
    for milestone in milestones:
        count = milestone["total"]
        rows = (count + columns - 1) // columns
        grid_height = rows * cell_height + max(0, rows - 1) * row_gap
        task_y = cursor_y + section_header_height + 5
        section_layout.append((milestone, rows, task_y, grid_height))
        cursor_y = task_y + grid_height + section_gap

    canvas_height = max(390, int(cursor_y + 34))
    generated = datetime.fromisoformat(data["generated_at"]).strftime("%Y-%m-%d %H:%M UTC")

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{canvas_height}" viewBox="0 0 {WIDTH} {canvas_height}" role="img" aria-labelledby="title desc">',
        '<title id="title">REPS2 Status Project roadmap progress grid</title>',
        f'<desc id="desc">{_esc(completed)} of {_esc(total)} roadmap tasks are marked implemented ({_esc(percent)}%). The milestone grid uses a restrained pencil-sketch treatment with graphite hatching and hand-drawn contours. This is not a compatibility or release-readiness score.</desc>',
        """<defs>
          <pattern id="paper-grid" width="22" height="22" patternUnits="userSpaceOnUse">
            <path d="M22 0H0V22" fill="none" stroke="#d6ded6" stroke-opacity=".045" stroke-width=".6"/>
          </pattern>
          <pattern id="pencil-hatch" width="9" height="9" patternUnits="userSpaceOnUse">
            <path d="M-2 9L9 -2 M2 11L11 2" fill="none" stroke="#d9e0d9" stroke-opacity=".16" stroke-width=".6"/>
          </pattern>
          <linearGradient id="metric" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0" stop-color="#477e63"/>
            <stop offset="1" stop-color="#89ad8c"/>
          </linearGradient>
        </defs>""",
        f'<rect x="0" y="0" width="{WIDTH}" height="{canvas_height}" rx="10" fill="#111613"/>',
        f'<rect x="0" y="0" width="{WIDTH}" height="{canvas_height}" rx="10" fill="url(#paper-grid)"/>',
        '<path d="M22 20L1176 18 M24 21L1174 22" fill="none" stroke="#89958b" stroke-opacity=".38" stroke-width=".7" stroke-linecap="round"/>',
        '<text x="32" y="42" fill="#aab5a9" font-family="Consolas,Menlo,monospace" font-size="10" font-weight="600" letter-spacing="1.4">REPS2  /  PROJECT STATUS</text>',
        f'<text x="1168" y="42" text-anchor="end" fill="#879286" font-family="Consolas,Menlo,monospace" font-size="9">UPDATED {_esc(generated)}</text>',
        '<text x="30" y="82" fill="#e1e5dc" font-family="Segoe Print,Bradley Hand,Comic Sans MS,cursive" font-size="29" font-weight="600">Status Project</text>',
        '<path d="M30 91Q135 94 230 90T436 92" fill="none" stroke="#a7b2a4" stroke-opacity=".68" stroke-width="1.1" stroke-linecap="round"/>',
        '<text x="32" y="111" fill="#aab4a8" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="11">Roadmap checklist, arranged by development milestone.</text>',
        '<rect x="30" y="128" width="1140" height="48" rx="2" fill="#171e19" fill-opacity=".88" stroke="#839083" stroke-opacity=".68" stroke-width=".9" stroke-dasharray="8 2 1 2"/>',
        '<text x="48" y="147" fill="#aab4a8" font-family="Consolas,Menlo,monospace" font-size="9" font-weight="600" letter-spacing="1">ROADMAP COMPLETION</text>',
        f'<text x="48" y="166" fill="#e0e7dc" font-family="Segoe Print,Bradley Hand,Comic Sans MS,cursive" font-size="20" font-weight="600">{percent}%</text>',
        '<rect x="132" y="154" width="470" height="7" rx="1" fill="#303b34" stroke="#748176" stroke-opacity=".5" stroke-width=".5"/>',
        f'<rect x="132" y="154" width="{470 * percent / 100:.2f}" height="7" rx="1" fill="url(#metric)"/>',
        f'<text x="620" y="162" fill="#d6ded4" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="12">{completed:,} / {total:,} implemented</text>',
        f'<text x="1150" y="162" text-anchor="end" fill="#aab4a8" font-family="Consolas,Menlo,monospace" font-size="10">{totals["open"]:,} OPEN</text>',
    ]

    for milestone, rows, task_y, grid_height in section_layout:
        done = milestone["completed"]
        count = milestone["total"]
        all_done = count > 0 and done == count
        accent = IMPLEMENTED_STROKE if all_done else "#69809b"
        header_y = task_y - section_header_height - 5
        percent_done = milestone["percent"]

        parts.extend([
            f'<rect x="{margin}" y="{header_y:.2f}" width="{grid_width}" height="{section_header_height}" rx="2" fill="#171e19" stroke="#859184" stroke-opacity=".72" stroke-width=".8" stroke-dasharray="7 2 1 2"/>',
            f'<text x="{margin + 10}" y="{header_y + 14:.2f}" fill="#e0e5dc" font-family="Segoe Print,Bradley Hand,Comic Sans MS,cursive" font-size="10" font-weight="600">{_esc(milestone["id"])}  {_esc(milestone["title"])}</text>',
            f'<text x="{margin + grid_width - 10}" y="{header_y + 16:.2f}" text-anchor="end" fill="{accent}" font-family="Consolas,Menlo,monospace" font-size="9">{done}/{count}  ·  {percent_done}%</text>',
        ])

        for index, task in enumerate(milestone["tasks"]):
            column = index % columns
            row = index // columns
            x = margin + column * (cell_width + column_gap)
            y = task_y + row * (cell_height + row_gap)
            implemented = task["implemented"]
            fill = IMPLEMENTED_FILL if implemented else OPEN_FILL
            stroke = IMPLEMENTED_STROKE if implemented else OPEN_STROKE
            status = "Implemented" if implemented else "Open / not marked complete"
            status_key = "implemented" if implemented else "open"
            label_limit = max(6, int((cell_width - 14) / 4.5))
            short_label = _esc(_clip(task["title"], label_limit))

            parts.append(
                f'<rect class="task" data-status="{status_key}" '
                f'x="{x:.2f}" y="{y:.2f}" width="{cell_width:.2f}" height="{cell_height}" '
                f'rx="2" fill="{fill}" stroke="{stroke}" stroke-width=".9" '
                f'stroke-dasharray="6 1.2 1.4 1.2">'
                f'<title>{_esc(task["title"])} — {status}</title></rect>'
            )
            parts.append(
                f'<rect x="{x + 1:.2f}" y="{y + 1:.2f}" width="{cell_width - 2:.2f}" '
                f'height="{cell_height - 2}" rx="1" fill="url(#pencil-hatch)" opacity=".42"/>'
            )
            parts.append(
                f'<path d="M{x + 1.4:.2f} {y + 1.6:.2f} '
                f'L{x + cell_width * .48:.2f} {y + .9:.2f} '
                f'L{x + cell_width - 1.2:.2f} {y + 1.5:.2f} '
                f'L{x + cell_width - .9:.2f} {y + cell_height - 1.3:.2f} '
                f'L{x + cell_width * .51:.2f} {y + cell_height - .8:.2f} '
                f'L{x + 1.1:.2f} {y + cell_height - 1.4:.2f}" '
                f'fill="none" stroke="{stroke}" stroke-width=".55" stroke-opacity=".55" '
                f'stroke-linecap="round" stroke-linejoin="round"/>'
            )
            parts.append(
                f'<text x="{x + 6:.2f}" y="{y + 16:.2f}" fill="#e2e8df" '
                f'font-family="Inter,Segoe UI,Arial,sans-serif" font-size="8.5">{short_label}</text>'
            )

    parts.extend([
        f'<text x="30" y="{canvas_height - 14}" fill="#9aa69a" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="9">Source: docs/ROADMAP.md · Release and real-title evidence: docs/PROJECT_STATE.md</text>',
        f'<text x="1170" y="{canvas_height - 14}" text-anchor="end" fill="#9aa69a" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="9">CHECKLIST METRIC ONLY · NOT GAME COMPATIBILITY</text>',
        "</svg>",
    ])
    return "\n".join(parts) + "\n"

def render_index_html(data: dict[str, Any]) -> str:
    """Render a responsive, pencil-sketch styled roadmap dashboard."""
    gates = data["release_gates"] or [{"label": "Release gates", "status": "not recorded"}]
    gate_cards: list[str] = []
    for gate in gates[:3]:
        met = gate["status"].lower() in {"met", "claimed"}
        status_color = "#59d39a" if met else "#d3a65f"
        gate_cards.append(
            f'<article class="gate"><div>{_esc(gate["label"])}</div>'
            f'<strong style="color:{status_color}">{_esc(gate["status"])}</strong></article>'
        )

    totals = data["totals"]
    generated = _esc(data["generated_at"])
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#111613">
  <meta name="description" content="REPS2 project status and roadmap progress.">
  <title>REPS2 — Status Project</title>
  <style>
    :root {{
      color-scheme: dark;
      background: #111613;
      color: #e1e5dc;
      font-family: Inter, "Segoe UI", Arial, sans-serif;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      min-height: 100vh;
      padding: 34px 20px;
      background-color: #111613;
      background-image:
        linear-gradient(rgba(214, 222, 214, .035) 1px, transparent 1px),
        linear-gradient(90deg, rgba(214, 222, 214, .035) 1px, transparent 1px);
      background-size: 22px 22px;
    }}
    main {{
      width: min(1240px, 100%);
      margin: 0 auto;
      padding: clamp(16px, 3vw, 32px);
      border: 1px dashed #718071;
      border-radius: 3px;
      background: rgba(17, 22, 19, .96);
      box-shadow: 0 18px 55px rgba(0, 0, 0, .34);
    }}
    .eyebrow {{
      color: #aab5a9;
      font: 600 11px/1.5 Consolas, Menlo, monospace;
      letter-spacing: 1.5px;
      text-transform: uppercase;
    }}
    h1 {{ margin: 12px 0 8px; font-family: "Segoe Print", "Bradley Hand", "Comic Sans MS", cursive; font-size: clamp(28px, 4vw, 42px); font-weight: 600; letter-spacing: -.8px; }}
    .intro {{ max-width: 850px; margin: 0; color: #aab4a8; line-height: 1.65; }}
    .metrics {{ display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 12px; margin: 26px 0 18px; }}
    .metric, .gate {{ padding: 16px; border: 1px dashed #718071; border-radius: 3px; background: #171e19; }}
    .metric span {{ display: block; color: #aab5a9; font: 600 10px Consolas, Menlo, monospace; letter-spacing: 1px; text-transform: uppercase; }}
    .metric strong {{ display: block; margin-top: 8px; color: #e1e5dc; font-size: 25px; }}
    figure {{ margin: 0; overflow: hidden; border: 1px dashed #718071; border-radius: 3px; background: #111613; }}
    figure img {{ display: block; width: 100%; height: auto; }}
    h2 {{ margin: 28px 0 12px; font-family: "Segoe Print", "Bradley Hand", "Comic Sans MS", cursive; font-size: 18px; font-weight: 600; }}
    .gates {{ display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 12px; }}
    .gate div {{ color: #aab5a9; font: 600 10px Consolas, Menlo, monospace; letter-spacing: .8px; text-transform: uppercase; }}
    .gate strong {{ display: block; margin-top: 8px; font-size: 13px; color: #e1e5dc; }}
    .evidence {{ padding: 16px; margin-top: 12px; border-left: 2px dashed #98a58f; border-radius: 2px; background: #171e19; color: #d0d8ce; font-size: 13px; }}
    footer {{ display: flex; flex-wrap: wrap; justify-content: space-between; gap: 12px; margin-top: 24px; padding-top: 18px; border-top: 1px dashed #718071; color: #9aa69a; font-size: 12px; line-height: 1.7; }}
    a {{ color: #a9c6a9; text-decoration: underline; text-decoration-style: dotted; text-underline-offset: 3px; }}
    a:hover {{ color: #d0dfcf; text-decoration-style: solid; }}
    @media (max-width: 700px) {{
      body {{ padding: 12px 6px; }}
      main {{ padding: 14px; border-radius: 12px; }}
      .metrics, .gates {{ grid-template-columns: 1fr; }}
      footer {{ display: block; }}
    }}
  </style>
</head>
<body>
  <main>
    <div class="eyebrow">REPS2 / project telemetry</div>
    <h1>Status Project</h1>
    <p class="intro">A milestone-grouped view of REPS2 implementation work, rendered with a subtle pencil-sketch finish.</p>
    <section class="metrics" aria-label="Roadmap summary">
      <article class="metric"><span>Checklist completion</span><strong>{totals["percent"]}%</strong></article>
      <article class="metric"><span>Implemented tasks</span><strong>{totals["completed"]:,} / {totals["total"]:,}</strong></article>
      <article class="metric"><span>Open tasks</span><strong>{totals["open"]:,}</strong></article>
    </section>
    <figure><img src="progress-grid.svg" alt="REPS2 roadmap implementation checklist, grouped by milestone"></figure>
    <h2>Release gates</h2>
    <section class="gates" aria-label="Release gate status">{"".join(gate_cards)}</section>
    <div class="evidence"><strong>Real-title evidence:</strong> {_esc(data["real_title_stage"])}. Checklist completion does not mean a game has booted or is playable.</div>
    <footer>
      <span>Generated {generated} from <a href="https://github.com/RidTheWann/REPS2-Project/blob/main/docs/ROADMAP.md">docs/ROADMAP.md</a> and <a href="https://github.com/RidTheWann/REPS2-Project/blob/main/docs/PROJECT_STATE.md">docs/PROJECT_STATE.md</a>.</span>
      <span><a href="progress.json">Machine-readable progress JSON</a> · <a href="https://github.com/RidTheWann/REPS2-Project">Source repository</a></span>
    </footer>
  </main>
</body>
</html>
"""


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
    (args.output_dir / "progress-grid.svg").write_text(
        render_progress_grid_svg(data), encoding="utf-8"
    )
    (args.output_dir / "progress.json").write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    (args.output_dir / "index.html").write_text(
        render_index_html(data), encoding="utf-8"
    )
    print(
        f"Generated REPS2 checklist grid: {data['totals']['completed']}/"
        f"{data['totals']['total']} tasks ({data['totals']['percent']}%) "
        f"across {len(data['milestones'])} milestones."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
