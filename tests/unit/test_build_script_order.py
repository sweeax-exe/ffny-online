"""scripts/build.ps1 -Clean must clean before begin-build: begin-build writes its snapshot into the build folder,
so a clean after it deleted the state record-build needs (build state validation failed)."""
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "scripts" / "build.ps1"


class BuildScriptOrder(unittest.TestCase):
    def test_clean_precedes_begin_build_and_record_build(self):
        s = SCRIPT.read_text(encoding="utf-8")
        clean = s.index("Remove-Item -LiteralPath $cleanTarget")
        begin = s.index(") begin-build")
        build = s.index("cmake --build")
        record = s.index(") record-build")
        self.assertLess(clean, begin)
        self.assertLess(begin, build)
        self.assertLess(build, record)


if __name__ == "__main__":
    unittest.main()
