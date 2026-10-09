#!/usr/bin/env python3
"""Make a macOS app self-contained, then audit every nested Mach-O image."""

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


SYSTEM_ROOTS = ("/System/Library/", "/usr/lib/")
MACHO_MAGIC = {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xce\xfa\xed\xfe",
               b"\xfe\xed\xfa\xce", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
               b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def system_path(name):
    return os.path.normpath(name).startswith(SYSTEM_ROOTS)


def within(path, directory):
    return path.resolve().is_relative_to(directory.resolve())


def is_macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in MACHO_MAGIC


@dataclass
class Image:
    loads: list
    rpaths: list
    identity: str = ""


def image_info(path):
    image = Image([], [])
    command = ""
    for line in run("/usr/bin/otool", "-l", str(path)).splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            command = line[4:]
        match = re.match(r"(?:name|path) (.+) \(offset \d+\)$", line)
        if not match:
            continue
        value = match[1]
        if command == "LC_ID_DYLIB":
            image.identity = value
        elif command == "LC_RPATH":
            image.rpaths.append(value)
        elif command in {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
                         "LC_LOAD_UPWARD_DYLIB", "LC_LAZY_LOAD_DYLIB"}:
            image.loads.append(value)
    image.loads = list(dict.fromkeys(image.loads))
    image.rpaths = list(dict.fromkeys(image.rpaths))
    return image


def expand(name, loader, executable):
    if name == "@loader_path" or name.startswith("@loader_path/"):
        return loader.parent / name.removeprefix("@loader_path").lstrip("/")
    if name == "@executable_path" or name.startswith("@executable_path/"):
        return executable.parent / name.removeprefix("@executable_path").lstrip("/")
    return Path(name)


def resolve(name, loader, executable, rpaths):
    if name.startswith("@rpath/"):
        candidates = [base / name[7:] for base in rpaths]
    else:
        candidates = [expand(name, loader, executable)]
    for candidate in candidates:
        if candidate.is_absolute() and candidate.is_file():
            return candidate.resolve()
    raise RuntimeError(f"{loader}: cannot resolve {name}")


def copy_tree(source, destination):
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination, symlinks=True)


def audit(app):
    executable = app / "Contents/MacOS/glcad"
    images = sorted(path for path in app.rglob("*") if is_macho(path))
    if not executable.is_file() or executable not in images:
        raise RuntimeError(f"Missing application executable: {executable}")
    for path in app.rglob("*"):
        if path.is_symlink() and (not path.exists() or not within(path, app)):
            raise RuntimeError(f"Bundle symlink escapes or is dangling: {path}")
    for binary in images:
        info = image_info(binary)
        if info.identity and not info.identity.startswith(("@rpath/", "@loader_path/")):
            raise RuntimeError(f"External library identity in {binary}: {info.identity}")
        rpaths = []
        for name in info.rpaths:
            base = expand(name, binary, executable)
            if not base.is_absolute() or not within(base, app):
                raise RuntimeError(f"External runtime search path in {binary}: {name}")
            rpaths.append(base)
        for name in info.loads:
            if system_path(name):
                continue
            if not name.startswith(("@loader_path/", "@executable_path/", "@rpath/")):
                raise RuntimeError(f"External library reference in {binary}: {name}")
            target = resolve(name, binary, executable, rpaths)
            if not within(target, app) or not is_macho(target):
                raise RuntimeError(f"Library does not resolve inside the app: {binary}: {name}")
    for manifest in (app / "Contents/Resources/vulkan").rglob("*.json"):
        data = json.loads(manifest.read_text())
        for item in [data.get("ICD", {}), data.get("layer", {}), *data.get("layers", [])]:
            name = item.get("library_path")
            if name and (Path(name).is_absolute() or not within(manifest.parent / name, app)
                         or not is_macho((manifest.parent / name).resolve())):
                raise RuntimeError(f"Invalid bundled Vulkan library: {manifest}: {name}")
    return {"bundle": str(app), "macho_images": len(images), "external_dependencies": [],
            "allowed_system_roots": list(SYSTEM_ROOTS)}


def bundle(args):
    app = args.app.resolve()
    executable = app / "Contents/MacOS/glcad"
    resources = app / "Contents/Resources"
    frameworks = app / "Contents/Frameworks"
    frameworks.mkdir(parents=True, exist_ok=True)
    origins = {}
    if args.pglite_runtime:
        source = args.pglite_runtime.resolve()
        destination = resources / "pglite"
        for directory in ("bin", "lib", "share", "pgdata-template"):
            if (source / directory).is_dir():
                copy_tree(source / directory, destination / directory)
        for file in source.rglob("*"):
            target = destination / file.relative_to(source)
            if is_macho(file) and target.is_file():
                origins[file.resolve()] = target.resolve()
    for source, relative in ((args.shaders, "shaders"), (args.fonts, "fonts")):
        if source:
            copy_tree(source, resources / relative)

    pending = []
    copied_frameworks = set()
    destinations = {}
    visited = set()

    def stage(source, name=None):
        source = source.resolve()
        if source in origins:
            return origins[source]
        if within(source, app):
            return source
        framework = next((p for p in source.parents if p.suffix == ".framework"), None)
        if framework:
            target_root = frameworks / framework.name
            if framework not in copied_frameworks:
                if target_root in destinations and destinations[target_root] != framework:
                    raise RuntimeError(f"Conflicting framework name: {framework}")
                destinations[target_root] = framework
                copy_tree(framework, target_root)
                copied_frameworks.add(framework)
                for file in framework.rglob("*"):
                    if is_macho(file):
                        target = target_root / file.relative_to(framework)
                        origins[file.resolve()] = target
                        visited.discard(target)
                        pending.append((file, target, []))
            return origins[source]
        target = frameworks / (name or source.name)
        if target in destinations and destinations[target] != source:
            raise RuntimeError(f"Conflicting library name: {source} and {destinations[target]}")
        destinations[target] = source
        # SDK libraries can be read-only: replace the entry, never write through.
        if target.exists() or target.is_symlink():
            target.unlink()
        shutil.copy2(source, target)
        target.chmod(target.stat().st_mode | 0o200)
        origins[source] = target
        # A relink can replace a library reached through stale relative paths.
        visited.discard(target)
        return target

    for source, name in ((args.moltenvk, "libMoltenVK.dylib"), (args.vulkan_loader, "libvulkan.1.dylib")):
        if source:
            target = stage(source, name)
            pending.append((source.resolve(), target, []))
    if args.moltenvk:
        manifest = resources / "vulkan/icd.d/MoltenVK_icd.json"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps({"file_format_version": "1.0.0", "ICD": {
            "library_path": "../../../Frameworks/libMoltenVK.dylib",
            "api_version": "1.4.0", "is_portability_driver": True}}, indent=2) + "\n")

    original_by_target = {target: source for source, target in origins.items()}
    pending += [(original_by_target.get(path, path), path, [])
                for path in app.rglob("*") if is_macho(path)]
    while pending:
        source, target, inherited = pending.pop()
        if target in visited:
            continue
        visited.add(target)
        info = image_info(source)
        rpaths = [expand(name, source, executable) for name in info.rpaths] + inherited
        changes = []
        for name in info.loads:
            if system_path(name):
                continue
            dependency = resolve(name, source, executable, rpaths)
            destination = stage(dependency, Path(name).name)
            pending.append((dependency, destination, rpaths))
            replacement = "@loader_path/" + os.path.relpath(destination, target.parent)
            if replacement != name:
                changes += ["-change", name, replacement]
        if info.identity:
            changes += ["-id", "@rpath/" + os.path.relpath(target, frameworks)]
        for name in info.rpaths:
            base = expand(name, target, executable)
            if not base.is_absolute() or not within(base, app):
                changes += ["-delete_rpath", name]
        if changes:
            run("/usr/bin/install_name_tool", *changes, str(target))

    report = audit(app)
    identity = args.identity or "-"
    options = ["--timestamp", "--options", "runtime"] if identity != "-" else []
    for binary in sorted(visited):
        run("/usr/bin/codesign", "--force", *options, "--sign", identity, str(binary))
    for framework in sorted(frameworks.rglob("*.framework"), key=lambda p: len(p.parts), reverse=True):
        run("/usr/bin/codesign", "--force", *options, "--sign", identity, str(framework))
    run("/usr/bin/codesign", "--force", *options, "--sign", identity, str(app))
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", str(app))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="Audit without changing files")
    parser.add_argument("--moltenvk", type=Path)
    parser.add_argument("--vulkan-loader", type=Path)
    parser.add_argument("--pglite-runtime", type=Path)
    parser.add_argument("--shaders", type=Path)
    parser.add_argument("--fonts", type=Path)
    parser.add_argument("--identity", default=os.environ.get("GLCAD_SIGN_IDENTITY", "-"))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = audit(args.app.resolve()) if args.check else bundle(args)
        if args.report:
            args.report.write_text(json.dumps(report, indent=2) + "\n")
        print(f"macOS bundle verified: {report['macho_images']} Mach-O images, no external non-system libraries")
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"macOS bundle failed: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.output, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
