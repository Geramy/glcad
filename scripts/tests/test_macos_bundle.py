import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "bundle-macos.py"


@unittest.skipUnless(sys.platform == "darwin", "Mach-O bundling requires macOS tools")
class MacBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="glcad-bundle-")
        self.root = Path(self.temp.name)
        self.app = self.root / "Drawing App.app"
        self.exe = self.app / "Contents/MacOS/glcad"
        self.exe.parent.mkdir(parents=True)
        (self.app / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleExecutable": "glcad", "CFBundleIdentifier": "com.glcad.bundle-test",
            "CFBundlePackageType": "APPL", "CFBundleVersion": "1"}))

    def tearDown(self):
        self.temp.cleanup()

    def compile(self, output, code, *args):
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["/usr/bin/clang", "-x", "c", "-", "-x", "none", "-o", str(output),
                        "-Wl,-headerpad_max_install_names", *map(str, args)],
                       input=code, text=True, check=True, capture_output=True)
        return output

    def library(self, path, symbol, value):
        return self.compile(path, f"int {symbol}(void) {{ return {value}; }}",
                            "-dynamiclib", "-Wl,-install_name," + str(path))

    def invoke(self, *args, success=True):
        result = subprocess.run([sys.executable, str(SCRIPT), "--app", str(self.app),
                                 "--identity", "-", *map(str, args)],
                                text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def test_transitive_libraries_rpaths_and_relocated_execution(self):
        libraries = self.root / "external SDK/lib"
        leaf = self.compile(libraries / "libLeaf.dylib", "int value(void) { return 42; }",
                            "-dynamiclib", "-Wl,-install_name,@rpath/libLeaf.dylib")
        parent = self.compile(libraries / "libParent.dylib",
                              "int value(void); int answer(void) { return value(); }",
                              "-dynamiclib", leaf, "-Wl,-rpath," + str(libraries),
                              "-Wl,-install_name," + str(libraries / "libParent.dylib"))
        self.compile(self.exe, "int answer(void); int main(void) { return answer() != 42; }", parent,
                     "-Wl,-rpath," + str(libraries))
        self.assertIn("External", self.invoke("--check", success=False).stderr)
        self.invoke()
        self.invoke()
        self.compile(self.exe, "int answer(void); int main(void) { return answer() != 42; }", parent)
        for library in (self.app / "Contents/Frameworks").glob("*.dylib"):
            library.chmod(0o444)
        self.invoke()
        moved = self.root / "relocated/glcad.app"
        moved.parent.mkdir()
        shutil.move(self.app, moved)
        shutil.rmtree(libraries.parent)
        self.app = moved
        self.invoke("--check")
        subprocess.run([str(moved / "Contents/MacOS/glcad")], check=True,
                       env={"PATH": "/usr/bin:/bin"})

    def test_runtime_helpers_and_plugins_keep_their_bundled_dependencies(self):
        self.compile(self.exe, "int main(void) { return 0; }")
        runtime = self.root / "database-runtime"
        dependency = self.library(runtime / "lib/libDatabase.dylib", "database", 17)
        self.compile(runtime / "bin/helper", "int database(void); int main(void) { return database()!=17; }",
                     dependency)
        self.compile(runtime / "lib/plugins/plugin.so", "int database(void); int plugin(void) { return database(); }",
                     "-dynamiclib", dependency)
        (runtime / "lib/libDatabase-current.dylib").symlink_to("libDatabase.dylib")
        self.invoke("--pglite-runtime", runtime)
        shutil.rmtree(runtime)
        self.invoke("--check")
        subprocess.run([str(self.app / "Contents/Resources/pglite/bin/helper")], check=True)

    def test_preserves_third_party_framework_layout(self):
        framework = self.root / "SDK/Example.framework"
        version = framework / "Versions/A"
        binary = self.compile(version / "Example", "int example(void) { return 7; }", "-dynamiclib",
                              "-Wl,-install_name,@rpath/Example.framework/Versions/A/Example")
        (version / "Resources").mkdir()
        (version / "Resources/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleExecutable": "Example", "CFBundleIdentifier": "com.glcad.fixture-framework",
            "CFBundlePackageType": "FMWK", "CFBundleVersion": "1"}))
        (framework / "Versions/Current").symlink_to("A")
        (framework / "Example").symlink_to("Versions/Current/Example")
        (framework / "Resources").symlink_to("Versions/Current/Resources")
        self.compile(self.exe, "int example(void); int main(void) { return example()!=7; }", binary,
                     "-Wl,-rpath," + str(framework.parent))
        self.invoke()
        shutil.rmtree(framework.parent)
        self.invoke("--check")
        subprocess.run([str(self.exe)], check=True)

    def test_rejects_missing_dependency_and_name_collisions(self):
        a = self.library(self.root / "first/libSame.dylib", "first", 1)
        b = self.library(self.root / "second/libSame.dylib", "second", 2)
        self.compile(self.exe, "int first(void); int second(void); int main(void) { return first()+second()!=3; }", a, b)
        self.assertIn("Conflicting library name", self.invoke(success=False).stderr)
        a.unlink()
        self.assertIn("cannot resolve", self.invoke(success=False).stderr)

    def test_rejects_external_symlinks_and_driver_manifests(self):
        self.compile(self.exe, "int main(void) { return 0; }")
        outside = self.root / "external.txt"
        outside.write_text("external")
        link = self.app / "Contents/external"
        link.symlink_to(outside)
        self.assertIn("symlink escapes", self.invoke(success=False).stderr)
        link.unlink()
        manifest = self.app / "Contents/Resources/vulkan/icd.d/driver.json"
        manifest.parent.mkdir(parents=True)
        manifest.write_text(json.dumps({"ICD": {"library_path": "/opt/homebrew/lib/driver.dylib"}}))
        self.assertIn("Invalid bundled Vulkan", self.invoke(success=False).stderr)


if __name__ == "__main__":
    unittest.main()
