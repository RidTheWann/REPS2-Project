import unittest
import xml.etree.ElementTree as ET

from generate_progress_dashboard import (
    build_dashboard_data,
    parse_roadmap,
    render_index_html,
    render_svg,
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

    def test_counts_checklist_items_per_milestone_and_globally(self):
        data = parse_roadmap(self.roadmap)
        self.assertEqual(data["totals"], {
            "completed": 2,
            "total": 3,
            "percent": 67,
        })
        self.assertEqual(data["milestones"][0]["percent"], 50)
        self.assertEqual(data["milestones"][1]["percent"], 100)

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

    def test_svg_is_valid_xml_and_escapes_roadmap_text(self):
        roadmap = self.roadmap.replace("Foundation", "Foundation & <IR>")
        data = build_dashboard_data(roadmap, self.project_state)
        svg = render_svg(data)
        root = ET.fromstring(svg)
        self.assertEqual(root.tag, "{http://www.w3.org/2000/svg}svg")
        self.assertIn("Foundation &amp; &lt;IR&gt;", svg)
        self.assertIn("Checklist completion only", svg)

    def test_svg_canvas_expands_for_additional_milestones(self):
        roadmap = "# Roadmap\n" + "\n".join(
            f"## M{i} — Stage {i}\n- [x] Completed item" for i in range(9)
        )
        data = build_dashboard_data(roadmap, self.project_state)
        root = ET.fromstring(render_svg(data))
        self.assertGreater(int(root.attrib["height"]), 850)

    def test_dashboard_uses_a_restrained_next_gen_systems_style(self):
        data = build_dashboard_data(self.roadmap, self.project_state)
        svg = render_svg(data)
        root = ET.fromstring(svg)

        self.assertEqual(root.attrib["width"], "1160")
        self.assertIn("Engineering progress", svg)
        self.assertIn("#090e19", svg)
        self.assertIn("#67e8f9", svg)
        self.assertIn('pattern id="grid"', svg)
        self.assertIn("Milestone map", svg)
        self.assertIn("Release gates", svg)
        self.assertNotIn('fill="#ffffff"', svg)

    def test_dashboard_page_uses_translucent_ps2_cover_wall(self):
        page = render_index_html()
        self.assertIn("color-scheme: dark", page)
        self.assertIn("progress-overlay.svg", page)
        self.assertIn("cover-wall", page)
        self.assertIn("wall-shade", page)
        self.assertIn("backdrop-filter: blur(18px)", page)
        self.assertIn("rgba(5, 10, 18, .30)", page)
        self.assertIn("xlenore/ps2-covers", page)
        self.assertEqual(
            page.count(
                "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/"
            ),
            28,
        )

    def test_glass_svg_is_translucent_while_readme_svg_stays_opaque(self):
        data = build_dashboard_data(self.roadmap, self.project_state)
        readme_svg = render_svg(data)
        glass_svg = render_svg(data, glass=True)

        ET.fromstring(readme_svg)
        ET.fromstring(glass_svg)
        self.assertNotIn('fill-opacity="0.56"', readme_svg)
        self.assertIn('fill-opacity="0.56"', glass_svg)

    def test_empty_roadmap_is_rejected(self):
        with self.assertRaises(ValueError):
            parse_roadmap("# Empty\n## M0 — Empty\n")


if __name__ == "__main__":
    unittest.main()
