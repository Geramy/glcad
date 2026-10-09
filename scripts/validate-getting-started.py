#!/usr/bin/env python3
"""Independently audit the bundled sample using ezdxf (required, never skipped)."""
import argparse
from pathlib import Path
import math
import ezdxf


def validate(path):
    document = ezdxf.readfile(path)
    assert not document.audit().has_errors, "DXF audit failed"
    assert document.units == ezdxf.units.MM, "Sample must use millimetres"
    model = document.modelspace()
    lines = list(model.query("LINE"))
    assert len(lines) == 4, "Expected four plate edges"
    corners = {(0., 0., 0.), (100., 0., 0.), (100., 60., 0.), (0., 60., 0.)}
    assert {tuple(line.dxf.start) for line in lines} == corners
    assert {tuple(line.dxf.end) for line in lines} == corners
    assert sorted(round((line.dxf.end - line.dxf.start).magnitude, 8) for line in lines) == [60, 60, 100, 100]
    circles = list(model.query("CIRCLE"))
    assert len(circles) == 1 and tuple(circles[0].dxf.center) == (50., 30., 0.)
    assert math.isclose(circles[0].dxf.radius, 10), "Expected a 20 mm diameter hole"
    assert len(model.query("TEXT")) == 4, "Expected four workflow labels"
    assert len(model) == 9, "Unexpected sample entities"
    print("Sample passed: 100 x 60 mm plate, 20 mm hole, four workflow labels.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "assets/samples/GettingStarted.dxf")
    validate(parser.parse_args().path)
