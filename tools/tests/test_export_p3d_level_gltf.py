import importlib.util
import sys
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "export_p3d_level_gltf.py"
SPEC = importlib.util.spec_from_file_location("p3d_exporter", MODULE_PATH)
EXPORTER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = EXPORTER
SPEC.loader.exec_module(EXPORTER)


class TriangleConversionTests(unittest.TestCase):
    def primitive(self, primitive_type, indices):
        value = EXPORTER.Primitive("x.p3d", "mesh", 0, 0, "shader",
                                   primitive_type, 0, 5, len(indices))
        value.positions = [(0.0, 0.0, 0.0)] * 5
        value.indices = indices
        return value

    def test_triangle_list_discards_incomplete_tail(self):
        value = self.primitive(EXPORTER.TRIANGLES, [0, 1, 2, 3])
        self.assertEqual(EXPORTER.triangle_indices(value), [0, 1, 2])

    def test_triangle_strip_alternates_winding(self):
        value = self.primitive(EXPORTER.TRIANGLE_STRIP, [0, 1, 2, 3, 4])
        self.assertEqual(EXPORTER.triangle_indices(value),
                         [0, 1, 2, 2, 1, 3, 2, 3, 4])

    def test_triangle_strip_removes_degenerates(self):
        value = self.primitive(EXPORTER.TRIANGLE_STRIP, [0, 1, 1, 2, 3])
        self.assertEqual(EXPORTER.triangle_indices(value), [1, 2, 3])

    def test_sun_quaternion_maps_positive_z_to_toward_sun(self):
        # For the engine's default vector this also protects the glTF -Z light
        # direction convention from accidentally being inverted later.
        direction = (0.45, 1.0, -0.30)
        qx, qy, qz, qw = EXPORTER.quaternion_from_z(direction)
        rotated = (2 * (qx * qz + qw * qy),
                   2 * (qy * qz - qw * qx),
                   1 - 2 * (qx * qx + qy * qy))
        length = sum(v * v for v in direction) ** 0.5
        expected = tuple(v / length for v in direction)
        for actual, wanted in zip(rotated, expected):
            self.assertAlmostEqual(actual, wanted, places=6)

    def test_explicit_blender_path_is_preserved(self):
        path = Path("tools") / "fake-blender.exe"
        self.assertEqual(EXPORTER.find_blender(path), path.resolve())

    def test_p3d_uv_flips_vertical_axis_for_gltf(self):
        self.assertEqual(EXPORTER.p3d_uv_to_gltf((0.25, 0.10)), (0.25, 0.90))


if __name__ == "__main__":
    unittest.main()
