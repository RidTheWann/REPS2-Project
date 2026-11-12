import unittest
import xml.etree.ElementTree as ET

from generate_progress_dashboard import (
    OPEN_FILL,
    IMPLEMENTED_FILL,
    build_dashboard_data,
    parse_roadmap,
    render_index_html,
    render_progress_grid_svg,
)


class ProgressDashboardTests(unittest.TestCase):
    def setUp(self):
        self.roadmap = """# Roadmap
## M0 — Foundation
- [x] First item
- [ ] Second item
## M1 — Compiler & IR
- [X] Third item
"""
        self.project_state = """# State
## Release-gate status
- Alpha: not claimed; details
- Public Beta: not met; details
- REPS2 1.0: not met; details

## M4 real-title evidence
No guest-visible boot stage was demonstrated.

## Active backlog
- Pending work
"""

    def test_counts_and_preserves_each_checklist_item(self):
        data = parse_roadmap(self.roadmap)
        self.assertEqual(data["totals"], {
            "completed": 2,
            "total": 3,
            "percent": 67,
            "open": 1,
        })
        self.assertEqual(data["milestones"][0]["percent"], 50)
        self.assertEqual(data["milestones"][1]["percent"], 100)
        self.assertEqual(
            data["milestones"][0]["tasks"],
            [
                {"title": "First item", "implemented": True},
                {"title": "Second item", "implemented": False},
            ],
        )

    def test_reads_release_gates_and_real_title_boundary(self):
        data = build_dashboard_data(self.roadmap, self.project_state)
        self.assertEqual(
            [gate["status"] for gate in data["release_gates"]],
            ["not claimed", "not met", "not met"],
        )
        self.assertEqual(
            data["real_title_stage"],
            "No guest-visible boot stage demonstrated",
        )

    def test_grid_svg_is_valid_and_escapes_task_text(self):
        roadmap = self.roadmap.replace("First item", "First & <IR> item")
        data = build_dashboard_data(roadmap, self.project_state)
        svg = render_progress_grid_svg(data)
        root = ET.fromstring(svg)

        self.assertEqual(root.tag, "{http://www.w3.org/2000/svg}svg")
        self.assertIn("First &amp; &lt;IR&gt; item", svg)
        self.assertIn("pencil-hatch", svg)
        self.assertIn("Status Project", svg)
        self.assertIn("not a compatibility or release-readiness score", svg)

    def test_every_roadmap_task_has_one_equal_size_grid_tile(self):
        data = build_dashboard_data(self.roadmap, self.project_state)
        root = ET.fromstring(render_progress_grid_svg(data))
        cells = [node for node in root.iter() if node.attrib.get("class") == "task"]

        self.assertEqual(len(cells), 3)
        self.assertEqual(
            sorted(node.attrib["data-status"] for node in cells),
            ["implemented", "implemented", "open"],
        )
        self.assertEqual(len({node.attrib["width"] for node in cells}), 1)
        self.assertEqual(len({node.attrib["height"] for node in cells}), 1)

    def test_tiles_are_aligned_to_a_fixed_grid(self):
        data = build_dashboard_data(self.roadmap, self.project_state)
        root = ET.fromstring(render_progress_grid_svg(data))
        cells = [node for node in root.iter() if node.attrib.get("class") == "task"]
        x_positions = [float(node.attrib["x"]) for node in cells]
        y_positions = [float(node.attrib["y"]) for node in cells]

        self.assertLess(x_positions[0], x_positions[1])
        self.assertAlmostEqual(x_positions[0], x_positions[2], places=5)
        self.assertGreater(y_positions[2], y_positions[0])

    def test_task_states_keep_distinct_fills_without_a_color_legend(self):
        data = build_dashboard_data(self.roadmap, self.project_state)
        svg = render_progress_grid_svg(data)

        self.assertIn(f'fill="{IMPLEMENTED_FILL}"', svg)
        self.assertIn(f'fill="{OPEN_FILL}"', svg)
        self.assertIn("First item — Implemented", svg)
        self.assertIn("Second item — Open / not marked complete", svg)
        self.assertNotIn("Green cells are checked items", svg)
        self.assertNotIn("blue-grey cells remain open", svg)

    def test_dashboard_page_uses_grid_without_cover_art(self):
        data = build_dashboard_data(self.roadmap, self.project_state)
        page = render_index_html(data)

        self.assertIn("color-scheme: dark", page)
        self.assertIn("progress-grid.svg", page)
        self.assertIn("Status Project", page)
        self.assertIn("Implemented tasks", page)
        self.assertIn("background-size: 22px 22px", page)
        self.assertIn("Segoe Print", page)
        self.assertIn("border: 1px dashed #718071", page)
        self.assertNotIn("legend", page)
        self.assertNotIn("Open / not marked complete", page)
        self.assertNotIn("blue-grey", page)
        self.assertNotIn("treemap", page)
        self.assertNotIn("cover-wall", page)
        self.assertNotIn("xlenore/ps2-covers", page)
        self.assertNotIn("progress-overlay.svg", page)

    def test_dashboard_json_is_serializable(self):
        import json

        data = build_dashboard_data(self.roadmap, self.project_state)
        decoded = json.loads(json.dumps(data))
        self.assertEqual(decoded["totals"]["total"], 3)
        self.assertEqual(decoded["milestones"][0]["tasks"][0]["title"], "First item")


if __name__ == "__main__":
    unittest.main()
