import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "patch_animatedgif.py"
SPEC = importlib.util.spec_from_file_location("patch_animatedgif", SCRIPT)
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class Environment:
    def __init__(self, root, name="active"):
        self.root = root
        self.name = name

    def subst(self, value):
        return {"$PROJECT_LIBDEPS_DIR": str(self.root), "$PIOENV": self.name}[value]


class PatchAnimatedGifTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def library(self, name="active"):
        library = self.root / name / "AnimatedGIF"
        source = library / "src"
        source.mkdir(parents=True)
        (source / "AnimatedGIF.h").write_text(PATCH.OLD_SNIPPET + PATCH.REGISTER_OLD)
        (source / "gif.inl").write_text(PATCH.PALETTE_OLD + PATCH.COPY_OLD)
        return library

    def test_active_environment_and_idempotence(self):
        active = self.library()
        sibling = self.library("sibling")
        env = Environment(self.root)
        self.assertTrue(PATCH.patch_animatedgif(env, require_found=True))
        first = {p.name: p.read_bytes() for p in (active / "src").iterdir()}
        self.assertTrue(PATCH.patch_animatedgif(env, require_found=True))
        self.assertEqual(first, {p.name: p.read_bytes() for p in (active / "src").iterdir()})
        self.assertEqual((sibling / "src" / "AnimatedGIF.h").read_text(), PATCH.OLD_SNIPPET + PATCH.REGISTER_OLD)
        self.assertEqual((sibling / "src" / "gif.inl").read_text(), PATCH.PALETTE_OLD + PATCH.COPY_OLD)
        self.assertIn(PATCH.PALETTE_NEW, first["gif.inl"].decode())
        self.assertIn(PATCH.COPY_NEW, first["gif.inl"].decode())

    def test_previously_patched_install_upgrades_to_same_sources(self):
        previous = self.library("previous") / "src"
        (previous / "AnimatedGIF.h").write_text(PATCH.NEW_SNIPPET + PATCH.REGISTER_OLD)
        (previous / "gif.inl").write_text(PATCH.PALETTE_NEW + PATCH.COPY_BOUNDED)
        clean = self.library() / "src"
        for src in (clean, previous):
            PATCH.patch_library(src.parent)
        for path in clean.iterdir():
            self.assertEqual(path.read_bytes(), (previous / path.name).read_bytes())
        self.assertIn(PATCH.REGISTER_NEW, (previous / "AnimatedGIF.h").read_text())

    def test_unknown_decoder_source_fails_without_rewriting_it(self):
        source = self.library() / "src" / "gif.inl"
        source.write_text("unknown upstream implementation")
        with self.assertRaises(RuntimeError):
            PATCH._patch_decoder(source)
        self.assertEqual(source.read_text(), "unknown upstream implementation")

    def test_missing_active_dependency_cannot_be_satisfied_by_sibling(self):
        self.library("sibling")
        self.assertFalse(PATCH.patch_animatedgif(Environment(self.root)))
        with self.assertRaises(SystemExit):
            PATCH.patch_animatedgif(Environment(self.root), require_found=True)


if __name__ == "__main__":
    unittest.main()
