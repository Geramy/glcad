`ctbtest.ctb` and `stbtest.stb` are unmodified test fixtures from
[ezdxf](https://github.com/mozman/ezdxf/tree/d6f2ac10caeddc712ed1824aaeb3b9c050de4a04/tests/test_08_addons), retrieved
2026-09-07. The upstream MIT license is included as `ezdxf-LICENSE`.

The importer implements the compressed version 1 container described by the
[upstream parser](https://github.com/mozman/ezdxf/blob/d6f2ac10caeddc712ed1824aaeb3b9c050de4a04/src/ezdxf/addons/acadctb.py).
The fixture checks cover indexed and named styles, exact paper lineweights,
colour, grayscale, screening, and cap/join choices. Unsupported plotting
features produce explicit warnings; the original bytes remain available for
lossless table export.
