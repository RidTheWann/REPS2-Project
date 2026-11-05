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
    """Render a clean, responsive README-friendly progress dashboard."""
    width = 1080
    totals = data["totals"]
    completed = totals["completed"]
    total = totals["total"]
    percent = totals["percent"]
    generated = datetime.fromisoformat(data["generated_at"]).strftime("%Y-%m-%d %H:%M UTC")

    rows_top = 293
    row_height = 44
    rows_bottom = rows_top + len(data["milestones"]) * row_height
    gates_heading_y = rows_bottom + 30
    gates_top = gates_heading_y + 22
    stage_label_y = gates_top + 82
    stage_text_y = stage_label_y + 25
    footer_y = stage_text_y + 42
    height = footer_y + 24

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        "<title id=\"title\">REPS2 development progress</title>",
        f"<desc id=\"desc\">{_esc(completed)} of {_esc(total)} roadmap checklist tasks completed ({_esc(percent)} percent). Checklist progress is not game compatibility or release readiness.</desc>",
        '<rect x="0.5" y="0.5" width="1079" height="{0}" rx="10" fill="#ffffff" stroke="#e2e8f0"/>'.format(height - 1),
        '<text x="40" y="42" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="11" font-weight="700" letter-spacing="1.4">REPS2  /  PROJECT STATUS</text>',
        '<text x="40" y="82" fill="#0f172a" font-family="Segoe UI,Arial,sans-serif" font-size="29" font-weight="700">Development progress</text>',
        '<text x="40" y="107" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="13">Roadmap checklist across all engineering milestones</text>',
        f'<text x="1040" y="42" text-anchor="end" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="11">UPDATED  {_esc(generated)}</text>',
        '<path d="M40 129 H1040" stroke="#e2e8f0" stroke-width="1"/>',
        f'<text x="40" y="196" fill="#2563eb" font-family="Segoe UI,Arial,sans-serif" font-size="43" font-weight="700">{percent}%</text>',
        f'<text x="155" y="166" fill="#0f172a" font-family="Segoe UI,Arial,sans-serif" font-size="15" font-weight="600">{completed:,} of {total:,} checklist items complete</text>',
        '<text x="1040" y="166" text-anchor="end" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="12">Overall roadmap progress</text>',
        '<rect x="155" y="181" width="885" height="8" rx="4" fill="#e2e8f0"/>',
        f'<rect x="155" y="181" width="{885 * percent / 100:.1f}" height="8" rx="4" fill="#2563eb"/>',
        '<text x="40" y="258" fill="#0f172a" font-family="Segoe UI,Arial,sans-serif" font-size="15" font-weight="700">Milestone progress</text>',
        '<text x="40" y="279" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="10" font-weight="700" letter-spacing="0.8">MILESTONE</text>',
        '<text x="640" y="279" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="10" font-weight="700" letter-spacing="0.8">PROGRESS</text>',
        '<text x="955" y="279" text-anchor="end" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="10" font-weight="700" letter-spacing="0.8">TASKS</text>',
        '<text x="1040" y="279" text-anchor="end" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="10" font-weight="700" letter-spacing="0.8">DONE</text>',
    ]

    for index, milestone in enumerate(data["milestones"]):
        y = rows_top + index * row_height
        done = milestone["completed"]
        count = milestone["total"]
        pct = milestone["percent"]
        milestone_id = _esc(milestone["id"])
        title = _esc(_clip(milestone["title"], 58))
        parts.extend([
            f'<text x="40" y="{y + 20}" fill="#2563eb" font-family="Segoe UI,Arial,sans-serif" font-size="11" font-weight="700">{milestone_id}</text>',
            f'<text x="83" y="{y + 20}" fill="#1e293b" font-family="Segoe UI,Arial,sans-serif" font-size="13" font-weight="500">{title}</text>',
            f'<rect x="640" y="{y + 10}" width="230" height="7" rx="3.5" fill="#e2e8f0"/>',
            f'<rect x="640" y="{y + 10}" width="{230 * pct / 100:.1f}" height="7" rx="3.5" fill="#2563eb"/>',
            f'<text x="955" y="{y + 20}" text-anchor="end" fill="#475569" font-family="Segoe UI,Arial,sans-serif" font-size="12">{done} / {count}</text>',
            f'<text x="1040" y="{y + 20}" text-anchor="end" fill="#0f172a" font-family="Segoe UI,Arial,sans-serif" font-size="12" font-weight="600">{pct}%</text>',
            f'<path d="M40 {y + 34} H1040" stroke="#f1f5f9" stroke-width="1"/>',
        ])

    parts.extend([
        f'<text x="40" y="{gates_heading_y}" fill="#0f172a" font-family="Segoe UI,Arial,sans-serif" font-size="15" font-weight="700">Release readiness</text>',
    ])

    gates = data["release_gates"] or [{"label": "Release gates", "status": "not recorded"}]
    column_width = 1000 / 3
    for index, gate in enumerate(gates[:3]):
        x = 40 + index * column_width
        status = gate["status"]
        status_color = "#166534" if status.lower() in {"met", "claimed"} else "#475569"
        parts.extend([
            f'<text x="{x:.1f}" y="{gates_top + 17}" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="10" font-weight="700" letter-spacing="0.6">{_esc(gate["label"]).upper()}</text>',
            f'<text x="{x:.1f}" y="{gates_top + 40}" fill="{status_color}" font-family="Segoe UI,Arial,sans-serif" font-size="13" font-weight="600">{_esc(_clip(status, 34))}</text>',
        ])
        if index < min(len(gates), 3) - 1:
            separator_x = x + column_width - 18
            parts.append(
                f'<path d="M{separator_x:.1f} {gates_top} V{gates_top + 48}" stroke="#e2e8f0" stroke-width="1"/>'
            )

    stage = _esc(_clip(data["real_title_stage"], 110))
    parts.extend([
        f'<path d="M40 {stage_label_y - 13} H1040" stroke="#e2e8f0" stroke-width="1"/>',
        f'<text x="40" y="{stage_label_y}" fill="#64748b" font-family="Segoe UI,Arial,sans-serif" font-size="10" font-weight="700" letter-spacing="0.8">REAL-TITLE TESTING</text>',
        f'<text x="40" y="{stage_text_y}" fill="#334155" font-family="Segoe UI,Arial,sans-serif" font-size="13">{stage}</text>',
        f'<text x="40" y="{footer_y}" fill="#94a3b8" font-family="Segoe UI,Arial,sans-serif" font-size="10">{_esc(data["metric_note"])}</text>',
        "</svg>",
    ])
    return "\n".join(parts) + "\n"


def render_index_html() -> str:
    return """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#ffffff">
  <title>REPS2 — Development progress</title>
  <style>
    :root {
      color-scheme: light;
      background: #f8fafc;
      color: #0f172a;
      font-family: Inter, "Segoe UI", Arial, sans-serif;
    }
    * { box-sizing: border-box; }
    body { margin: 0; padding: 36px 20px; }
    main {
      width: min(1080px, 100%);
      margin: 0 auto;
      padding: 20px;
      background: #fff;
      border: 1px solid #e2e8f0;
      border-radius: 10px;
    }
    img { display: block; width: 100%; height: auto; }
    footer {
      margin-top: 16px;
      padding-top: 12px;
      border-top: 1px solid #e2e8f0;
      color: #64748b;
      font-size: 12px;
      line-height: 1.6;
    }
    a { color: #2563eb; text-decoration: none; }
    a:hover { text-decoration: underline; }
    @media (max-width: 640px) {
      body { padding: 12px 8px; }
      main { padding: 8px; border-radius: 6px; }
    }
  </style>
</head>
<body>
  <main>
    <img src="progress.svg" alt="REPS2 development progress dashboard">
    <footer>
      Generated from
      <a href="https://github.com/RidTheWann/REPS2-Project/blob/main/docs/ROADMAP.md">the project roadmap</a>
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
