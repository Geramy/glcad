# Incoming exchange fixtures

These fixed files were created with ezdxf 1.4.4 and Open Cascade through
cadquery-ocp 8.0.1.0.0, independently of GLCAD's exporters. `manifest.json`
records their checksums, dimensions and analytic solid volume.

The DXF contains an 8 × 5 metre rectangle, a one metre radius circular hole,
a solid hatch with that hole, a dimension, text and explicit layer weights.
The STEP contains the same plate extruded three metres, with a through hole.
Its native Open Cascade representation exercises surface curves, oriented
face bounds and a cylindrical seam. GLCAD imports curved STEP faces with a
0.1 mm chord tolerance, so subsequent volume checks allow tessellation error.

The release workflow tests import these inputs, edit them, save and reopen
native documents, export deliverables and reopen the exports. The separate
`scripts/validate-release-exports.py` checks those outputs using independent
readers, including the retained hole and edited dimensions.

To deliberately regenerate the corpus, install
`scripts/release-readers-requirements.txt` and run
`scripts/create-release-inputs.py`. File timestamps and tool metadata can
change the hashes; commit regenerated fixtures and their manifest together.
Routine tests use the retained files without regenerating them.

This small synthetic corpus supplements the workflow tests. Customer projects
and external beta acceptance remain separate release requirements.
