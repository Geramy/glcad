# Layer transparency fixture

`layer-alpha.dxf` extends the existing `../r2000_layers.dwg` fixture with
application-tagged layer data. `layer-alpha.dwg` was converted and independently
decoded with GNU LibreDWG 0.13.3:

```sh
dxf2dwg -y --as r2000 -o layer-alpha.dwg layer-alpha.dxf
dwgread -O JSON -o layer-alpha.json layer-alpha.dwg
```

The layers precede their APPID records in the object map. Expected imported
transparency percentages are:

| Layer | Extended data | Percentage |
| --- | --- | --- |
| 0 | AcCmTransparency, 1071, 0x020000bf | 25 |
| HALF | AcCmTransparency, 1071, 0x02000080 | 50 |
| INVISIBLE | AcCmTransparency, 1071, 0x02000000 | 100 |
| UNRELATED | DifferentApp, 1071, 0x02000000 | 0 |
| WRONG_TYPE | AcCmTransparency, 1040, 33554432.0 | 0 |
| BYBLOCK | AcCmTransparency, 1071, 0x01000000 | 0 |
| INVALID_BITS | AcCmTransparency, 1071, 0x02000100 | 0 |

The value encoding follows [ezdxf's layer implementation](https://github.com/mozman/ezdxf/blob/master/src/ezdxf/entities/layer.py).
The DWG EED payload uses code 71 and a raw little-endian 32-bit value, as decoded
by [LibreDWG](https://github.com/LibreDWG/libredwg/blob/master/src/decode.c).
The R2000 container exercises EED transport; it does not establish support for
transparent display in R2000 CAD applications. This is a structural fixture,
not an independently rendered CAD reference image.
