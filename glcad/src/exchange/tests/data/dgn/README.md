# DGN seed

`seed_2d.dgn` is the blank 2D seed distributed with GDAL v3.13.3 at
`ogr/ogrsf_frmts/dgn/data/seed_2d.dgn`. It is used only to construct regression
drawings with the upstream fixture writer. `gdgn_test_support` shares a generated
offset millimetre/custom-label underlay fixture between the decoder and UI
exchange tests; it is not linked into the application. Source and licence information is in
`third_party/dgnlib/README.md` and `third_party/dgnlib/LICENSE.txt`.
