# DGN v7 reader

The `upstream` directory contains unchanged DGN access-library sources and VAX
floating-point conversion code from GDAL **v3.13.3** (2026-08-18):

- `ogr/ogrsf_frmts/dgn/` in https://github.com/OSGeo/gdal/tree/v3.13.3
- `port/cpl_vax.cpp` from the same release

Copyright notices remain in each source. The code is MIT licensed; see
`LICENSE.txt`. `SHA256SUMS` records the imported source bytes.

The `port` directory provides the small subset of CPL types, allocation, byte
order and local file I/O needed by this reader. It does not include GDAL, OGR,
network/virtual filesystems or driver discovery. The reader target uses the read,
open, helper and curve-stroking sources; the writer is reserved for regression
fixtures. DGN v8 is a different format and is not decoded by this library.
