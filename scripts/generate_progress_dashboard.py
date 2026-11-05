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
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

MILESTONE_HEADER = re.compile(r"^##\s+(M\d+)\s+—\s+(.+?)\s*$")
CHECKBOX = re.compile(r"^\s*-\s+\[([ xX])\]\s+(.+?)\s*$")
GATE_LINE = re.compile(r"^\s*-\s*(Alpha|Public Beta|REPS2 1\.0):\s*(.+?)\s*$")


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


def render_svg(data: dict[str, Any]) -> str:
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
        f'<rect x="0.5" y="0.5" width="1159" height="{height - 1}" rx="18" fill="url(#surface)" stroke="#26364d"/>',
        f'<rect x="1" y="1" width="1158" height="{height - 2}" rx="17" fill="url(#ambient)"/>',
        f'<rect x="1" y="1" width="1158" height="{height - 2}" rx="17" fill="url(#grid)"/>',
        '<path d="M40 124H1120" stroke="#26364d" stroke-width="1"/>',
        '<circle cx="48" cy="42" r="4" fill="#67e8f9"/>',
        '<text x="62" y="46" fill="#9bb1cd" font-family="Consolas,Menlo,monospace" font-size="11" font-weight="600" letter-spacing="1.2">REPS2  /  NATIVE RECOMPILATION</text>',
        f'<text x="1120" y="46" text-anchor="end" fill="#7185a2" font-family="Consolas,Menlo,monospace" font-size="10">REPOSITORY STATE  •  {_esc(generated)}</text>',
        '<text x="40" y="91" fill="#eef5ff" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="31" font-weight="650" letter-spacing="-.7">Engineering progress</text>',
        '<text x="42" y="111" fill="#8da1bd" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="12">A live view of the REPS2 roadmap, implementation gates and real-title evidence.</text>',
        '<rect x="40" y="148" width="1080" height="151" rx="13" fill="#0b1220" fill-opacity=".82" stroke="#26364d"/>',
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
            f'<rect x="40" y="{y}" width="1080" height="42" rx="7" fill="#111b2b" fill-opacity=".58"/>',
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
            f'<rect x="{x}" y="{gates_top}" width="{column_width}" height="54" rx="8" fill="#101a29" stroke="#26364d"/>',
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
    return """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#090e19">
  <title>REPS2 — Engineering progress</title>
  <style>
    :root {
      color-scheme: dark;
      background: #080c15;
      color: #e7effb;
      font-family: Inter, "Segoe UI", Arial, sans-serif;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      padding: 36px 20px;
      background:
        radial-gradient(ellipse at 85% 0%, rgba(82,105,216,.10), transparent 42%),
        #080c15;
    }
    main {
      width: min(1160px, 100%);
      margin: 0 auto;
      padding: 12px;
      border: 1px solid #1d2b40;
      border-radius: 16px;
      background: rgba(12,18,30,.88);
    }
    img { display: block; width: 100%; height: auto; }
    footer {
      margin: 0 12px;
      padding: 14px 8px 8px;
      border-top: 1px solid #1d2b40;
      color: #8295b0;
      font-size: 12px;
      line-height: 1.7;
    }
    a { color: #67e8f9; text-decoration: none; }
    a:hover { color: #a5f3fc; text-decoration: underline; }
    @media (max-width: 640px) {
      body { padding: 12px 6px; }
      main { padding: 4px; border-radius: 10px; }
      footer { margin: 0 4px; }
    }
  </style>
</head>
<body>
  <main>
    <img src="progress.svg" alt="REPS2 engineering progress dashboard">
    <footer>
      Live status generated from
      <a href="https://github.com/RidTheWann/REPS2-Project/blob/main/docs/ROADMAP.md">the engineering roadmap</a>
      and <a href="https://github.com/RidTheWann/REPS2-Project/blob/main/docs/PROJECT_STATE.md">verified project state</a>.
      Checklist completion is not a game compatibility or release-readiness score.
      <a href="progress.json">Progress data (JSON)</a>.
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
    (args.output_dir / "progress.svg").write_text(render_svg(data), encoding="utf-8")
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
