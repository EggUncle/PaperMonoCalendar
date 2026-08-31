"""Check committed previews without rendering fonts or accessing user data."""

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("preview_renderer", ROOT / "tools/render_previews.py")
renderer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(renderer)


class PreviewTests(unittest.TestCase):
    def test_images_are_black_white_and_metadata_free(self):
        for state in ("synced", "waiting"):
            with self.subTest(state=state):
                pixels = renderer.read_png((ROOT / f"docs/images/calendar-{state}.png").read_bytes())
                self.assertEqual(len(pixels), 800 * 520)
                self.assertEqual(set(pixels), {0, 255})
                # The documentation caption must be present outside the screen.
                self.assertIn(0, pixels[488 * 800:515 * 800])

    def test_today_stays_blank_and_waiting_has_no_activity_pattern(self):
        synced = renderer.read_png((ROOT / "docs/images/calendar-synced.png").read_bytes())
        waiting = renderer.read_png((ROOT / "docs/images/calendar-waiting.png").read_bytes())
        self.assertNotEqual(synced, waiting)
        # Fixture is 2028-02-18: today is row 2, column 5. Below its digits stays white.
        self.assertEqual(set(synced[285 * 800 + 400:285 * 800 + 459]), {255})
        # 2028-02-04 has level 4 in the fixture; the waiting counterpart has no fill.
        self.assertEqual(set(waiting[189 * 800 + 400:189 * 800 + 459]), {255})
        self.assertEqual(set(synced[189 * 800 + 400:189 * 800 + 459]), {0, 255})


if __name__ == "__main__":
    unittest.main()
