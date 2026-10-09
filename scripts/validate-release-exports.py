#!/usr/bin/env python3
"""Validate the release_workflow corpus with independent format readers.

Run gcapp_tests with GLCAD_TEST_ONLY=release_workflow and
GLCAD_RELEASE_ARTIFACT_DIR=/tmp/glcad-release-corpus, then run this script
with that directory. Requires cadquery-ocp, trimesh, scipy, ezdxf and pypdf in a
separate Python environment (pip install -r scripts/release-readers-requirements.txt).
Missing readers fail rather than silently skip.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

import ezdxf
from ezdxf.entities.boundary_paths import ArcEdge
import trimesh
from pypdf import PdfReader
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepGProp import BRepGProp
from OCP.GProp import GProp_GProps
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_Reader
from OCP.TopAbs import TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.BRepClass3d import BRepClass3d_SolidClassifier
from OCP.TopAbs import TopAbs_IN, TopAbs_OUT
from OCP.gp import gp_Pnt


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(stem):
    expected = json.loads(stem.with_suffix(".json").read_text())
    mesh = trimesh.load_mesh(stem.with_suffix(".stl"), process=True)
    require(mesh.is_watertight, "STL is not watertight")
    require(mesh.is_winding_consistent, "STL has inconsistent winding")
    require(mesh.body_count == 1, "STL has disconnected bodies")
    require(math.isclose(mesh.volume, expected["mesh_volume_m3"], abs_tol=1e-4),
            f"STL volume changed: {mesh.volume}")

    reader = STEPControl_Reader()
    require(reader.ReadFile(str(stem.with_suffix(".step"))) == IFSelect_RetDone,
            "STEP reader rejected file")
    require(reader.TransferRoots() > 0, "STEP has no transferable roots")
    shape = reader.OneShape()
    require(not shape.IsNull(), "STEP has no shape")
    require(BRepCheck_Analyzer(shape).IsValid(), "STEP boundary representation is invalid")
    solids = TopExp_Explorer(shape, TopAbs_SOLID)
    count = 0
    while solids.More():
        count += 1
        solids.Next()
    require(count == 1, f"STEP contains {count} solids")
    mass = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, mass)
    # Open Cascade's transfer target length unit defaults to millimetres.
    volume = mass.Mass() / 1e9
    require(math.isclose(volume, expected["exact_volume_m3"], abs_tol=1e-4),
            f"STEP exact volume changed: {volume}; expected {expected['exact_volume_m3']}")

    drawing = ezdxf.readfile(stem.with_suffix(".dxf"))
    audit = drawing.audit()
    require(not audit.has_errors and not audit.has_fixes, "DXF required audit repairs")
    require(drawing.units == 6, f"DXF units changed: {drawing.units}")
    model = drawing.modelspace()
    if expected["profile"] == "circle":
        circles = list(model.query("CIRCLE"))
        require(len(circles) == 1, "DXF did not preserve the circle entity")
        require(math.isclose(circles[0].dxf.radius, 1, abs_tol=1e-6), "DXF circle radius changed")
    else:
        lines = list(model.query("LINE"))
        require(len(lines) == 4, "DXF did not preserve the four profile edges")
        for line in lines:
            require(math.isclose((line.dxf.end-line.dxf.start).magnitude, 2, abs_tol=1e-6),
                    "DXF profile edge length changed")

    pdf = PdfReader(stem.with_suffix(".pdf"), strict=True)
    require(len(pdf.pages) == 1, "PDF page count changed")
    page = pdf.pages[0]
    require(page.mediabox.width > 0 and page.mediabox.height > 0, "PDF page has no extent")
    require(page.get_contents() is not None and len(page.get_contents().get_data()) > 50,
            "PDF drawing has no content")
    return {"part": stem.name, "stl_volume_m3": float(mesh.volume),
            "step_volume_m3": volume, "formats": ["STL", "STEP", "DXF", "PDF"]}


def validate_drawing(stem, outline_layer="ReleaseOutline", note="Release drawing", patterned=True):
    drawing = ezdxf.readfile(stem.with_suffix(".dxf"))
    audit = drawing.audit()
    require(not audit.has_errors and not audit.has_fixes, "Dimensioned DXF required audit repairs")
    require(drawing.units == 6, "Dimensioned DXF units changed")
    require(drawing.layers.get(outline_layer).dxf.lineweight == 50,
            "Dimensioned DXF lost 0.5 mm layer lineweight")
    model = drawing.modelspace()
    dimensions = list(model.query("DIMENSION"))
    require(len(dimensions) == 1, "DXF lost the measured dimension")
    require(math.isclose(dimensions[0].get_measurement(), 8, abs_tol=1e-6),
            "DXF dimension no longer measures eight metres")
    hatches = list(model.query("HATCH"))
    require(len(hatches) == 1 and len(hatches[0].paths) == 2,
            "DXF hatch lost its circular exclusion")
    if patterned:
        require(hatches[0].pattern is not None and len(hatches[0].pattern.lines)>0,
                "DXF hatch lost its pattern definition")
        revisions=[e for e in model.query("LINE") if e.dxf.layer=="Revision"]
        require(len(revisions)==5, "DXF lost the separate rectangle or open revision stroke")
        arcs=[edge for path in hatches[0].paths for edge in getattr(path,"edges",[])
              if isinstance(edge,ArcEdge)]
        require(len(arcs)==1 and math.isclose(arcs[0].radius,1.25,abs_tol=1e-6) and
                math.isclose(abs(arcs[0].end_angle-arcs[0].start_angle),360,abs_tol=1e-6),
                "Associative hatch lost the edited analytic circular boundary")
    else:
        require(hatches[0].dxf.solid_fill == 1, "DXF hatch lost its solid fill")
    require(len(model.query("CIRCLE")) == 1, "DXF lost the hole outline")
    outlines = [e for e in model.query("LINE CIRCLE") if e.dxf.layer != "Revision"]
    require(len(outlines) == 5 and all(e.dxf.layer == outline_layer for e in outlines),
            "DXF drawing entities changed layers")

    pdf = PdfReader(stem.with_suffix(".pdf"), strict=True)
    require(len(pdf.pages) == 1, "Dimensioned PDF page count changed")
    page = pdf.pages[0]
    mm = 72 / 25.4
    require(math.isclose(float(page.mediabox.width), 420*mm, abs_tol=.05) and
            math.isclose(float(page.mediabox.height), 297*mm, abs_tol=.05),
            "PDF is not A3 landscape")
    current = None
    widths = []
    segments = []
    path_segments = []

    def operand(operator, args, matrix, _text_matrix):
        nonlocal current
        if operator == b"w":
            widths.append(float(args[0]))
        elif operator in (b"m", b"l"):
            x, y = map(float, args)
            a, b, c, d, e, f = matrix
            point = (a*x+c*y+e, b*x+d*y+f)
            if operator == b"l" and current:
                segments.append(math.dist(current, point))
                path_segments.append((current, point))
            current = point

    text = page.extract_text(visitor_operand_before=operand)
    require(note in text, "PDF lost searchable drawing note")
    require("8" in text, "PDF lost dimension text")
    require(any(math.isclose(width, .5*mm, abs_tol=.02) for width in widths),
            "PDF lost the configured 0.5 mm lineweight")
    for length in (160*mm, 100*mm):
        require(any(math.isclose(segment, length, abs_tol=.05) for segment in segments),
                f"PDF scale changed: missing expected {length} point outline edge")
    if patterned:
        horizontal=[edge for edge in path_segments
                    if math.isclose(math.dist(*edge),160*mm,abs_tol=.05) and
                    abs(edge[0][1]-edge[1][1])<.01]
        pair=next(((a,b) for a in horizontal for b in horizontal
                   if math.isclose(b[0][1]-a[0][1],100*mm,abs_tol=.05)),None)
        require(pair is not None,"PDF lost the plate boundary")
        left=min(point[0] for edge in pair for point in edge)
        bottom=pair[0][0][1]
        center=(left+80*mm,bottom+50*mm)
        interior_strokes=0
        for a,b in path_segments:
            dx,dy=b[0]-a[0],b[1]-a[1]
            squared=dx*dx+dy*dy
            if squared==0:
                continue
            t=max(0,min(1,((center[0]-a[0])*dx+(center[1]-a[1])*dy)/squared))
            distance=math.dist(center,(a[0]+t*dx,a[1]+t*dy))
            require(distance>=25*mm*.99,"PDF strokes cross the edited circular exclusion")
            x,y=(a[0]+b[0])/2,(a[1]+b[1])/2
            if (left+mm<x<left+159*mm and bottom+mm<y<bottom+99*mm and
                    squared>(10*mm)**2 and (abs(dx)<.01 or abs(dy)<.01)):
                interior_strokes+=1
        require(interior_strokes>=10,"PDF lost the interior hatch pattern")
    return {"drawing": stem.name, "scale": "1:50", "paper": "A3 landscape",
            "checks": ["dimension", "text", "layer", "lineweight", "hatch hole", "plotted lengths"]}


def validate_incoming(directory):
    fixtures = Path(__file__).resolve().parent.parent / "glcad/src/ui/tests/fixtures/incoming"
    source = json.loads((fixtures / "manifest.json").read_text())
    for name, digest in source["sha256"].items():
        require(hashlib.sha256((fixtures / name).read_bytes()).hexdigest() == digest,
                f"Incoming fixture changed without updating its manifest: {name}")
    drawing_result = validate_drawing(directory / "incoming-drawing", "IncomingOutline",
                                      "Incoming plate", patterned=False)
    drawing = ezdxf.readfile(directory / "incoming-drawing.dxf")
    model = drawing.modelspace()
    revisions = [e for e in model.query("LINE") if e.dxf.layer == "Revision"]
    require(len(revisions) == 1 and
            math.isclose((revisions[0].dxf.end - revisions[0].dxf.start).magnitude, 1, abs_tol=1e-6),
            "Incoming DXF did not retain the one-metre revision line")
    require(drawing.layers.get("Revision").dxf.lineweight == 35, "Revision lineweight changed")
    notes = list(model.query("TEXT MTEXT"))
    require(len(notes) == 1 and notes[0].dxf.layer == "IncomingNotes", "Incoming note layer changed")

    expected = (40 - math.pi) * 3.25
    mesh = trimesh.load_mesh(directory / "incoming-part.stl", process=True)
    require(mesh.is_watertight and mesh.is_winding_consistent and mesh.body_count == 1,
            "Incoming STEP edit produced an invalid printable shell")
    require(math.isclose(mesh.volume, expected, abs_tol=.003), "Incoming edited STL volume changed")
    for actual, target in zip(mesh.bounds.flatten(), (0, 0, 0, 8, 5, 3.25)):
        require(math.isclose(actual, target, abs_tol=1e-5), "Incoming edited STL bounds changed")
    reader = STEPControl_Reader()
    require(reader.ReadFile(str(directory / "incoming-part.step")) == IFSelect_RetDone,
            "Incoming edited STEP cannot be read")
    require(reader.TransferRoots() > 0, "Incoming edited STEP has no roots")
    shape = reader.OneShape()
    require(BRepCheck_Analyzer(shape).IsValid(), "Incoming edited STEP is invalid")
    mass = GProp_GProps(); BRepGProp.VolumeProperties_s(shape, mass)
    require(math.isclose(mass.Mass() / 1e9, expected, abs_tol=.003), "Incoming edited STEP volume changed")
    solids = TopExp_Explorer(shape, TopAbs_SOLID)
    require(solids.More(), "Incoming edited STEP has no solid")
    solid = solids.Current(); solids.Next()
    require(not solids.More(), "Incoming edited STEP contains extra solids")
    for point, state in ((gp_Pnt(4000, 2500, 1000), TopAbs_OUT),
                         (gp_Pnt(1000, 1000, 1000), TopAbs_IN)):
        classifier = BRepClass3d_SolidClassifier(solid, point, 1e-6)
        require(classifier.State() == state, "Incoming edited STEP changed the through-hole region")
    return {"incoming_drawing": drawing_result, "incoming_part_volume_m3": mass.Mass()/1e9,
            "checks": ["source hashes", "import/edit/save/export/reopen", "revision geometry",
                       "text and layers", "lineweights", "scaled PDF", "solid validity", "through hole"]}


def validate_step_units(directory):
    result = []
    for unit in ("metre", "millimetre", "centimetre", "inch", "foot"):
        reader = STEPControl_Reader()
        path = directory / ("units-" + unit + ".step")
        require(reader.ReadFile(str(path)) == IFSelect_RetDone,
                f"STEP {unit} reader rejected file")
        require(reader.TransferRoots() == 1, f"STEP {unit} transfer failed")
        shape = reader.OneShape()
        require(not shape.IsNull() and BRepCheck_Analyzer(shape).IsValid(),
                f"STEP {unit} shape is invalid")
        mass = GProp_GProps()
        BRepGProp.VolumeProperties_s(shape, mass)
        volume = mass.Mass() / 1e9
        require(math.isclose(volume, 6, abs_tol=1e-6),
                f"STEP {unit} changed physical dimensions: volume {volume}")
        result.append(unit)
    return {"step_units": result, "volume_m3": 6}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    results = []
    for name in ("square-part", "circle-part"):
        results.append(validate(args.directory / name))
    results.append(validate_drawing(args.directory / "dimensioned-sheet"))
    results.append(validate_step_units(args.directory))
    results.append(validate_incoming(args.directory))
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
