#!/usr/bin/env python3
"""Bundle actual license texts for a configured build, without choosing a project license.

This reads CMakeCache.txt but never invokes CMake or downloads license text.
Missing notices fail unless --allow-incomplete is explicitly selected for a
development bundle. The JSON inventory records the actual input hashes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def read_cache(path):
    values = {}
    for line in path.read_text().splitlines():
        match = re.match(r"([^/#][^:]*):[^=]+=(.*)$", line)
        if match:
            values[match[1]] = match[2]
    return values


def enabled(value):
    return value.upper() not in ("", "0", "OFF", "FALSE", "NO", "NOTFOUND") and not value.endswith("-NOTFOUND")


def collect(repo, build, extras=(), project_license=None):
    cache = read_cache(build / "CMakeCache.txt")
    deps = Path(cache.get("FETCHCONTENT_BASE_DIR", str(build / "_deps")))
    files = []
    missing = []

    def add(component, path):
        path = Path(path)
        if not path.is_file():
            missing.append({"component": component, "expected": str(path)})
        else:
            files.append((component, path))

    add("DGN reader", repo / "third_party/dgnlib/LICENSE.txt")
    add("DejaVu font", repo / "assets/fonts/LICENSE-DejaVu.txt")
    add("Material Symbols font", repo / "assets/fonts/LICENSE-MaterialSymbols.txt")
    add("volk", Path(cache.get("volk_SOURCE_DIR", str(deps / "volk-src"))) / "LICENSE.md")
    vulkan = Path(cache.get("VULKAN_HEADERS_SOURCE_DIR", str(deps / "vulkan_headers-src")))
    for name in ("LICENSE.md", "LICENSES/Apache-2.0.txt", "LICENSES/MIT.txt"):
        add("Vulkan headers", vulkan / name)
    add("stb", deps / "stb-src/LICENSE")
    add("cpp-httplib", Path(cache.get("httplib_SOURCE_DIR", str(deps / "cpp_httplib-src"))) / "LICENSE")
    add("nlohmann/json", Path(cache.get("nlohmann_json_SOURCE_DIR", str(deps / "nlohmann_json-src"))) / "LICENSE.MIT")

    supplied = {}
    for item in extras:
        if "=" not in item:
            raise ValueError("--extra-license must be COMPONENT=PATH")
        component, path = item.split("=", 1)
        if not component or not path:
            raise ValueError("--extra-license must have both a component and a path")
        supplied.setdefault(component.lower(), []).append(Path(path))
        add(component, path)

    def require_external(name):
        if name.lower() not in supplied:
            missing.append({"component": name, "expected": "Supply --extra-license " + name + "=/path/to/actual/license"})

    system = cache.get("CMAKE_SYSTEM_NAME", "")
    if system == "iOS":
        source = "moltenvk_ios_simulator-src" if "simulator" in cache.get("CMAKE_OSX_SYSROOT", "").lower() else "moltenvk_ios-src"
        add("MoltenVK", deps / source / "LICENSE")
    elif enabled(cache.get("GLCAD_MOLTENVK", "")):
        require_external("MoltenVK")
    if enabled(cache.get("GLCAD_VULKAN_LOADER", "")) and system != "iOS":
        require_external("Vulkan-Loader")

    if enabled(cache.get("GLCAD_WITH_PGLITE", "")):
        base = build / "pglite/src/pglite_engine"
        add("pglite-rs", base / "LICENSE")
        add("PostgreSQL", base / "postgres-pglite/COPYRIGHT")
        add("pgvector", base / "postgres-pglite/pglite/other_extensions/vector/LICENSE")
        icu = base / "postgres-pglite/pglite/static/minimal-icu"
        if icu.exists():
            for license_file in sorted(icu.glob("*/LICENSE")):
                add("ICU", license_file)
    if enabled(cache.get("OPENSSL_SSL_LIBRARY", "")):
        require_external("OpenSSL")
    if enabled(cache.get("GLCAD_PORTABLE_UNDERLAYS", "")):
        for name in ("libpng", "libjpeg", "libtiff", "Poppler"):
            require_external(name)
    if enabled(cache.get("GLCAD_XPS_UNDERLAYS", "")):
        for name in ("libgxps", "Cairo", "GLib", "libarchive"):
            require_external(name)
    # Apple supplies system zlib. Other packages may redistribute their copy.
    if system != "iOS":
        require_external("zlib")

    if project_license:
        add("GLCAD project license", project_license)
    else:
        existing = next((repo / name for name in ("LICENSE", "LICENSE.txt", "LICENSE.md", "COPYING") if (repo / name).is_file()), None)
        if existing:
            add("GLCAD project license", existing)
        else:
            missing.append({"component": "GLCAD project license", "expected": "Owner must choose and supply a project license; this script does not choose one."})
    return files, missing, cache


def bundle(repo, build, output, extras=(), project_license=None):
    files, missing, cache = collect(repo, build, extras, project_license)
    entries = []
    parts = ["GLCAD dependency license notices\n\nOriginal license text follows unchanged for each recorded input.\n"]
    if missing:
        parts.append("DEVELOPMENT BUILD: NOTICE INVENTORY IS INCOMPLETE\n" +
                     "\n".join("Missing " + item["component"] + ": " + item["expected"] for item in missing) +
                     "\nResolve these inputs and review the distribution before release.\n")
    for component, path in sorted(files, key=lambda row: (row[0].lower(), str(row[1]))):
        data = path.read_bytes()
        try:
            relative = str(path.resolve().relative_to(repo.resolve()))
        except ValueError:
            relative = str(path)
        entries.append({"component": component, "source": relative,
                        "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
        parts.append("\n" + "=" * 72 + "\n" + component + "\nSource: " + relative + "\n\n" + data.decode("utf-8"))
    review = [
        "Compare this inventory with the final linked/shipped artifacts, including transitive dependencies.",
        "License texts alone do not resolve source-distribution, relinking, attribution, trademark or App Store compatibility obligations.",
        "The project owner must approve the project license and the distribution combination."
    ]
    inventory = {"complete_inputs": not missing, "inputs": entries, "missing": missing,
                 "review_required": review,
                 "build_features": {key: cache.get(key, "") for key in
                    ("CMAKE_SYSTEM_NAME", "GLCAD_WITH_PGLITE", "GLCAD_PORTABLE_UNDERLAYS", "GLCAD_XPS_UNDERLAYS")}}
    output.mkdir(parents=True, exist_ok=True)
    (output / "THIRD_PARTY_NOTICES.txt").write_text("\n".join(parts), encoding="utf-8")
    (output / "license-inventory.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    return inventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--extra-license", action="append", default=[])
    parser.add_argument("--project-license", type=Path)
    parser.add_argument("--allow-incomplete", action="store_true")
    args = parser.parse_args()
    try:
        result = bundle(args.repo.resolve(), args.build.resolve(), args.output.resolve(),
                        args.extra_license, args.project_license)
    except (OSError, ValueError) as error:
        parser.exit(2, str(error) + "\n")
    print(f"Bundled {len(result['inputs'])} license texts; {len(result['missing'])} missing inputs.")
    for missing in result["missing"]:
        print(f"MISSING {missing['component']}: {missing['expected']}")
    return 0 if result["complete_inputs"] or args.allow_incomplete else 2


if __name__ == "__main__":
    raise SystemExit(main())
