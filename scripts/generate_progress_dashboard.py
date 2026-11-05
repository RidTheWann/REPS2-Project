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
    """Render a self-contained, GitHub README-friendly dark/red/gold SVG."""
    width, height = 1160, 850
    totals = data["totals"]
    completed = totals["completed"]
    total = totals["total"]
    percent = totals["percent"]
    generated = datetime.fromisoformat(data["generated_at"]).strftime("%Y-%m-%d %H:%M UTC")

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        "<title id=\"title\">REPS2 project progress dashboard</title>",
        f"<desc id=\"desc\">{_esc(completed)} of {_esc(total)} roadmap checklist tasks completed ({_esc(percent)} percent). Checklist progress is not game compatibility or release readiness.</desc>",
        "<defs><linearGradient id=\"accent\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\"><stop offset=\"0\" stop-color=\"#8f2432\"/><stop offset=\"1\" stop-color=\"#d0a64e\"/></linearGradient></defs>",
        '<rect width="1160" height="850" rx="24" fill="#111015"/>',
        '<path d="M24 176 H1136" stroke="#332b31" stroke-width="1"/>',
        '<text x="38" y="47" fill="#d5ad5a" font-family="Segoe UI,Arial,sans-serif" font-size="13" font-weight="700" letter-spacing="3">REPS2  /  MASTER REVERSE ENGINEERING</text>',
        '<text x="38" y="98" fill="#f5f0e8" font-family="Segoe UI,Arial,sans-serif" font-size="38" font-weight="750">PROJECT PROGRESS</text>',
        '<text x="40" y="132" fill="#aaa2a5" font-family="Segoe UI,Arial,sans-serif" font-size="15">Live roadmap checklist • M0–M7 • generated automatically from repository state</text>',
        '<rect x="824" y="28" width="298" height="128" rx="16" fill="#1d1920" stroke="#48313a"/>',
        '<text x="846" y="56" fill="#c6b6a1" font-family="Segoe UI,Arial,sans-serif" font-size="12" font-weight="700" letter-spacing="1.8">ROADMAP CHECKLIST</text>',
        f'<text x="846" y="108" fill="#e0b95f" font-family="Segoe UI,Arial,sans-serif" font-size="46" font-weight="800">{percent}%</text>',
        f'<text x="978" y="105" fill="#d8d1ca" font-family="Segoe UI,Arial,sans-serif" font-size="15">{completed:,} / {total:,} tasks</text>',
        '<rect x="846" y="123" width="254" height="7" rx="3.5" fill="#39323b"/>',
        f'<rect x="846" y="123" width="{254 * percent / 100:.1f}" height="7" rx="3.5" fill="url(#accent)"/>',
        '<text x="40" y="207" fill="#d5ad5a" font-family="Segoe UI,Arial,sans-serif" font-size="12" font-weight="700" letter-spacing="2">ENGINEERING MILESTONES</text>',
    ]

    for index, milestone in enumerate(data["milestones"]):
        column = index % 2
        row = index // 2
        x = 36 + column * 552
        y = 224 + row * 106
        done = milestone["completed"]
        count = milestone["total"]
        pct = milestone["percent"]
        title = _esc(_clip(milestone["title"]))
        milestone_id = _esc(milestone["id"])
        parts.extend([
            f'<rect x="{x}" y="{y}" width="532" height="92" rx="13" fill="#1b1820" stroke="#39303a"/>',
            f'<text x="{x + 18}" y="{y + 27}" fill="#d6ad58" font-family="Segoe UI,Arial,sans-serif" font-size="12" font-weight="800" letter-spacing="1.2">{milestone_id}</text>',
            f'<text x="{x + 59}" y="{y + 28}" fill="#f1ece6" font-family="Segoe UI,Arial,sans-serif" font-size="16" font-weight="650">{title}</text>',
            f'<text x="{x + 510}" y="{y + 28}" fill="#d8c9b1" font-family="Segoe UI,Arial,sans-serif" font-size="13" text-anchor="end">{done}/{count}  ·  {pct}%</text>',
            f'<rect x="{x + 18}" y="{y + 55}" width="496" height="9" rx="4.5" fill="#39323b"/>',
            f'<rect x="{x + 18}" y="{y + 55}" width="{496 * pct / 100:.1f}" height="9" rx="4.5" fill="url(#accent)"/>',
            f'<text x="{x + 18}" y="{y + 82}" fill="#9f969d" font-family="Segoe UI,Arial,sans-serif" font-size="11">{done} completed  ·  {count - done} remaining</text>',
        ])

    parts.append('<path d="M24 668 H1136" stroke="#332b31" stroke-width="1"/>')
    parts.append('<text x="40" y="697" fill="#d5ad5a" font-family="Segoe UI,Arial,sans-serif" font-size="12" font-weight="700" letter-spacing="2">PRODUCT / RELEASE GATES</text>')

    gates = data["release_gates"]
    if not gates:
        gates = [{"label": "Release gates", "status": "not recorded"}]
    for index, gate in enumerate(gates[:3]):
        x = 36 + index * 368
        status = gate["status"]
        color = "#d6ad58" if status.lower() in {"met", "claimed"} else "#c76a72"
        parts.extend([
            f'<rect x="{x}" y="713" width="350" height="55" rx="11" fill="#1b1820" stroke="#39303a"/>',
            f'<text x="{x + 15}" y="736" fill="#b6abb0" font-family="Segoe UI,Arial,sans-serif" font-size="11" font-weight="700">{_esc(gate["label"]).upper()}</text>',
            f'<text x="{x + 15}" y="757" fill="{color}" font-family="Segoe UI,Arial,sans-serif" font-size="15" font-weight="700">{_esc(status)}</text>',
        ])

    stage = _esc(data["real_title_stage"])
    parts.extend([
        '<rect x="36" y="781" width="1088" height="39" rx="9" fill="#281b20" stroke="#63323a"/>',
        '<text x="52" y="806" fill="#d6ad58" font-family="Segoe UI,Arial,sans-serif" font-size="11" font-weight="800" letter-spacing="1">REAL-TITLE EVIDENCE</text>',
        f'<text x="250" y="806" fill="#e7c6c7" font-family="Segoe UI,Arial,sans-serif" font-size="14" font-weight="600">{stage}</text>',
        f'<text x="40" y="840" fill="#8e858d" font-family="Segoe UI,Arial,sans-serif" font-size="10">Generated {_esc(generated)} • {_esc(data["metric_note"])}</text>',
        "</svg>",
    ])
    return "\n".join(parts) + "\n"


def render_index_html() -> str:
    return """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#111015">
  <title>REPS2 — Project Progress</title>
  <style>
    :root{color-scheme:dark;background:#111015;color:#f5f0e8;font-family:Segoe UI,Arial,sans-serif}
    body{margin:0;padding:32px 18px;display:grid;place-items:center;min-height:100vh;box-sizing:border-box}
    main{width:min(1160px,100%)}img{display:block;width:100%;height:auto}
    p{color:#aaa2a5;line-height:1.6}a{color:#d5ad5a}
  </style>
</head>
<body><main>
  <img src="progress.svg" alt="REPS2 live roadmap progress dashboard">
  <p>Automatically generated from
    <a href="https://github.com/RidTheWann/REPS2-Project/blob/main/docs/ROADMAP.md">docs/ROADMAP.md</a>
    and <a href="https://github.com/RidTheWann/REPS2-Project/blob/main/docs/PROJECT_STATE.md">docs/PROJECT_STATE.md</a>.
    Checklist completion is not a game compatibility or release-readiness score.
    <a href="progress.json">Machine-readable progress data</a>.
  </p>
</main></body></html>
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
