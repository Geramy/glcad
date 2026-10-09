#!/usr/bin/env python3
"""Run local release gates and preserve logs, exported artifacts and JUnit output.

Independent readers are mandatory: --readers-python names a Python environment
containing the dependencies of validate-release-exports.py. This runner never
turns local results into a claim that the external beta has completed.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/debug"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--readers-python", required=True, type=Path)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = (args.output or root / "build/release-checks" / stamp).resolve()
    output.mkdir(parents=True, exist_ok=True)
    results = []

    def run(name, command, env=None, timeout=900, cwd=None):
        with (output / (name + ".log")).open("w") as log:
            print(f"Running {name}; log: {log.name}", flush=True)
            try:
                result = subprocess.run([str(x) for x in command], cwd=cwd or root, env=env,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
                passed = result.returncode == 0
            except (OSError, subprocess.TimeoutExpired) as error:
                log.write(str(error) + "\n")
                passed = False
        results.append({"name": name, "passed": passed})
        return passed

    with tempfile.TemporaryDirectory(prefix="glcad-release-config-") as config:
        environment = dict(os.environ, GLCAD_CONFIG_DIR=config)
        environment.pop("GLCAD_TEST_ONLY", None)
        if not args.skip_build and not run("build", ["cmake", "--build", build, "-j", "6"]):
            return 1
        run("full-tests", ["ctest", "--test-dir", build, "-L", "full", "--output-on-failure",
                           "--no-tests=error", "--output-junit", output / "tests.xml"], environment)
        corpus_env = dict(environment, GLCAD_TEST_ONLY="release_workflow",
                          GLCAD_RELEASE_ARTIFACT_DIR=str(output / "exports"))
        executable = build / "glcad/src/ui/gcapp_tests"
        if os.name == "nt": executable = executable.with_suffix(".exe")
        corpus = run("workflow-corpus", [executable], corpus_env, cwd=config)
        if corpus:
            run("independent-exports", [args.readers_python.absolute(),
                root / "scripts/validate-release-exports.py", output / "exports"])
        else:
            results.append({"name": "independent-exports", "passed": False,
                            "reason": "workflow corpus failed"})
    report = {"created_utc": stamp, "checks": results,
              "external_beta_complete": False,
              "scope": "Host core/workflow tests and independent file readers; simulator is a separate gate."}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if results and all(item["passed"] for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
