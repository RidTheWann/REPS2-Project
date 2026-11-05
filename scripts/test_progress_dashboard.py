import unittest
import xml.etree.ElementTree as ET

from generate_progress_dashboard import (
    build_dashboard_data,
    parse_roadmap,
    render_index_html,
    render_readme_png,
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
        self.assertIn("backdrop-filter: blur(3px) saturate(110%)", page)
        self.assertIn("backdrop-filter: none", page)
        self.assertNotIn("blur(18px)", page)
        self.assertIn("rgba(5, 10, 18, .30)", page)
        self.assertIn("xlenore/ps2-covers", page)
        self.assertEqual(
            page.count(
                "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/"
            ),
            28,
        )

    def test_glass_svg_preserves_the_cover_wall_through_transparent_canvas(self):
        data = build_dashboard_data(self.roadmap, self.project_state)
        readme_svg = render_svg(data)
        glass_svg = render_svg(data, glass=True)

        readme_root = ET.fromstring(readme_svg)
        glass_root = ET.fromstring(glass_svg)
        ns = "{http://www.w3.org/2000/svg}"
        readme_rects = readme_root.findall(f"{ns}rect")
        glass_rects = glass_root.findall(f"{ns}rect")

        # README's standalone preview keeps its own solid background.
        self.assertEqual(readme_rects[0].attrib.get("fill"), "url(#surface)")
        # Pages overlay leaves the wallpaper visible; only a near-clear veil
        # and dark translucent information panels are drawn on top.
        self.assertEqual(glass_rects[0].attrib.get("fill"), "none")
        self.assertEqual(glass_rects[1].attrib.get("fill"), "#050a12")
        self.assertEqual(glass_rects[1].attrib.get("fill-opacity"), ".035")
        self.assertEqual(glass_rects[2].attrib.get("opacity"), ".12")
        self.assertEqual(glass_rects[3].attrib.get("fill-opacity"), "0.34")
        self.assertIn('fill-opacity="0.2"', glass_svg)
        self.assertIn('fill-opacity="0.3"', glass_svg)
        self.assertNotIn('fill="url(#ambient)"', glass_svg)

    def test_readme_png_contains_a_rendered_cover_wall(self):
        from io import BytesIO

        from PIL import Image

        data = build_dashboard_data(self.roadmap, self.project_state)
        cover = Image.new("RGB", (120, 180), (180, 40, 30))
        cover_buffer = BytesIO()
        cover.save(cover_buffer, format="JPEG")

        preview = render_readme_png(
            data,
            cover_assets=[cover_buffer.getvalue()],
        )
        with Image.open(BytesIO(preview)) as rendered:
            self.assertEqual(rendered.format, "PNG")
            self.assertEqual(rendered.size, (1160, 696))
            self.assertEqual(rendered.mode, "RGBA")
            self.assertGreater(len(preview), 10_000)

    def test_empty_roadmap_is_rejected(self):
        with self.assertRaises(ValueError):
            parse_roadmap("# Empty\n## M0 — Empty\n")


if __name__ == "__main__":
    unittest.main()
