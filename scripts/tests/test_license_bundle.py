import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("license_bundle", Path(__file__).parents[1] / "bundle-license-notices.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class LicenseBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.build = self.repo / "build"
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text("CMAKE_SYSTEM_NAME:STRING=iOS\nCMAKE_OSX_SYSROOT:STRING=iphonesimulator\n")
        paths = ["third_party/dgnlib/LICENSE.txt", "assets/fonts/LICENSE-DejaVu.txt",
                 "assets/fonts/LICENSE-MaterialSymbols.txt", "build/_deps/volk-src/LICENSE.md",
                 "build/_deps/vulkan_headers-src/LICENSE.md",
                 "build/_deps/vulkan_headers-src/LICENSES/Apache-2.0.txt",
                 "build/_deps/vulkan_headers-src/LICENSES/MIT.txt", "build/_deps/stb-src/LICENSE",
                 "build/_deps/cpp_httplib-src/LICENSE", "build/_deps/nlohmann_json-src/LICENSE.MIT",
                 "build/_deps/moltenvk_ios_simulator-src/LICENSE"]
        for path in paths:
            target = self.repo / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("Fixture notice for " + path + "\n")

    def test_project_license_is_required_and_never_chosen(self):
        result = module.bundle(self.repo, self.build, self.repo / "out")
        self.assertFalse(result["complete_inputs"])
        self.assertEqual([m["component"] for m in result["missing"]], ["GLCAD project license"])
        self.assertFalse((self.repo / "LICENSE").exists())
        notice = (self.repo / "out/THIRD_PARTY_NOTICES.txt").read_text()
        self.assertIn("NOTICE INVENTORY IS INCOMPLETE", notice)
        self.assertIn("Missing GLCAD project license", notice)

    def test_exact_license_bytes_and_hash_are_preserved(self):
        (self.repo / "LICENSE").write_text("Owner-selected fixture license\n")
        result = module.bundle(self.repo, self.build, self.repo / "out")
        self.assertTrue(result["complete_inputs"])
        notice = (self.repo / "out/THIRD_PARTY_NOTICES.txt").read_text()
        for entry in result["inputs"]:
            source = self.repo / entry["source"]
            self.assertIn(source.read_text(), notice)
            self.assertEqual(entry["sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertEqual(json.loads((self.repo / "out/license-inventory.json").read_text()), result)

    def test_missing_dependency_fails_instead_of_silently_omitting_it(self):
        (self.build / "_deps/stb-src/LICENSE").unlink()
        result = module.bundle(self.repo, self.build, self.repo / "out")
        self.assertIn("stb", [m["component"] for m in result["missing"]])
        self.assertFalse(result["complete_inputs"])

    def test_enabled_optional_libraries_require_actual_notices(self):
        with (self.build / "CMakeCache.txt").open("a") as cache:
            cache.write("GLCAD_PORTABLE_UNDERLAYS:BOOL=ON\nOPENSSL_SSL_LIBRARY:FILEPATH=/some/libssl.a\n")
        result = module.bundle(self.repo, self.build, self.repo / "out")
        missing = [m["component"] for m in result["missing"]]
        for name in ("OpenSSL", "libpng", "libjpeg", "libtiff", "Poppler"):
            self.assertIn(name, missing)
        supplied = self.repo / "actual-openssl-license.txt"
        supplied.write_text("Fixture OpenSSL license text\n")
        result = module.bundle(self.repo, self.build, self.repo / "out", [f"OpenSSL={supplied}"])
        self.assertNotIn("OpenSSL", [m["component"] for m in result["missing"]])
        self.assertIn("OpenSSL", [m["component"] for m in result["inputs"]])


if __name__ == "__main__":
    unittest.main()
