# Dimension association fixtures

`example_2018.dwg` is the unchanged public LibreDWG test drawing:
https://github.com/LibreDWG/libredwg/blob/master/test/test-data/example_2018.dwg

`records.txt` contains the eight DIMASSOC object byte ranges extracted from each
of `example_2000.dwg`, `example_2004.dwg`, `example_2007.dwg`,
`example_2010.dwg`, `example_2013.dwg`, and `example_2018.dwg` in that directory.
Each line records the GLCAD version enum, source handle, body bit offset, data
bit offset, data bit count, object byte size, and unchanged object bytes in hex.
The offset into each extracted byte range is zero. Extraction decompresses
modern DWG object sections but does not rewrite their object fields.

Expected reference fields come from the companion `example_2000.dxf` and the
corresponding DXF records recorded in LibreDWG's `examples/alldxf_DIMASSOC.inc`.
These cover an arc, nested geometry, polyline edge markers, one/two/four-point
associations, and the `2e50` unspecified point sentinel. The installed LibreDWG
0.13.3 DIMASSOC decoder is not the reference oracle: it misreads these fields.
All 48 records were also compared across the six source versions.

The application regression uses the four linear dimensions with handles 467,
47F, 497 and 4AF. They reference endpoints on polyline 419, with edge markers
2/3, 3/1, 1/4 and 4/4 respectively. Angular, ordinate and nested associations
in the same drawing remain separate unsupported cases.

These fixtures establish structural and editing behavior. They do not establish
independent CAD rendering or visual compatibility.
