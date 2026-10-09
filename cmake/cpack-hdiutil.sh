#!/bin/sh
# CPACK_COMMAND_HDIUTIL wrapper: falls back to makehybrid when attach is wedged.
HDIUTIL=/usr/bin/hdiutil

if [ "$1" != "create" ]; then
    exec "$HDIUTIL" "$@"
fi

if "$HDIUTIL" "$@"; then exit 0; fi
sleep 2
if "$HDIUTIL" "$@"; then exit 0; fi

echo "cpack-hdiutil: create failed twice; falling back to makehybrid" >&2

# Args: create -ov -srcfolder <dir> -volname <name> -fs <fs> -format <fmt> <out>
src=""; vol="Untitled"; fmt="UDZO"; out=""
shift
while [ $# -gt 0 ]; do
    case "$1" in
        -srcfolder) src="$2"; shift 2 ;;
        -volname)   vol="$2"; shift 2 ;;
        -format)    fmt="$2"; shift 2 ;;
        -fs|-size)  shift 2 ;;
        -*)         shift ;;
        *)          out="$1"; shift ;;
    esac
done
if [ -z "$src" ] || [ -z "$out" ]; then
    echo "cpack-hdiutil: cannot fall back (no -srcfolder/output)" >&2
    exit 1
fi

tmp="$out.hybrid.dmg"
rm -f "$tmp" "$out"
"$HDIUTIL" makehybrid -quiet -hfs -default-volume-name "$vol" \
    -o "$tmp" "$src" || exit 1
"$HDIUTIL" convert -quiet "$tmp" -format "$fmt" -o "$out" || exit 1
rm -f "$tmp"
