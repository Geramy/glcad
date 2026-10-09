#!/bin/bash
# Builds the embedded-Postgres engine (pglite + pgvector) from source.
set -euo pipefail

# Drop the caller's jobserver: it breaks Postgres's generated-header order.
unset MAKEFLAGS MFLAGS MAKELEVEL

SRC="$1"
cd "$SRC"

# The submodule pin records an SSH remote; fetch it anonymously instead.
git -c url.https://github.com/.insteadOf=git@github.com: \
    submodule update --init --depth 1 postgres-pglite
(cd postgres-pglite &&
 git submodule update --init --depth 1 pglite/other_extensions/vector)

# Applied patches are recorded in a ledger; reverse-check only adopts old trees.
HERE="$(cd "$(dirname "$0")" && pwd)"
apply_patches() {
  local repo="$1" dir="$2" ledger="$1/.glcad-patches-applied" p name
  for p in "$dir"/*.patch; do
    [ -e "$p" ] || continue
    name="$(basename "$p")"
    if grep -qxF "$name" "$ledger" 2>/dev/null; then
      echo "patch already applied: $p"
      continue
    fi
    if (cd "$repo" && git apply --check "$p" 2>/dev/null); then
      (cd "$repo" && git apply "$p")
    elif (cd "$repo" && git apply --check -R "$p" 2>/dev/null); then
      echo "patch already applied: $p"
    else
      echo "FATAL: patch does not apply: $p" >&2
      exit 1
    fi
    echo "$name" >> "$ledger"
  done
}
apply_patches "$SRC/postgres-pglite" "$HERE/patches"
apply_patches "$SRC" "$HERE/patches-rs"

TRIPLE="${GLCAD_PGLITE_HOST_TRIPLE:-}"
TARGET_OS=""
case "$TRIPLE" in *mingw32*) TARGET_OS=Windows ;; esac

if [ "$TARGET_OS" = Windows ]; then
  # mingw: no zlib, host zic, no template (a win32 cluster is LLP64).
  export PGL_HOST_TRIPLE="$TRIPLE" PGL_TARGET_OS=Windows
  export PGL_CONFIGURE_EXTRA="--without-zlib"
  export ZIC="${GLCAD_PGLITE_ZIC:-/usr/sbin/zic}"
elif [ -n "$TRIPLE" ]; then
  : "${GLCAD_PGLITE_CC:?GLCAD_PGLITE_HOST_TRIPLE is set; GLCAD_PGLITE_CC must name the target compiler}"
  # The template needs an initdb that runs here: build a host engine first.
  if [ ! -f "$SRC/native/out-host/pgdata-template/PG_VERSION" ]; then
    # Set cross state aside: a stale cross config.status yields iOS host binaries.
    rm -rf "$SRC/native/out-host" "$SRC/native/out-cross-save"
    [ -d "$SRC/native/out" ] && mv "$SRC/native/out" "$SRC/native/out-cross-save"
    # Point the host pass at the build machine's SDK, not the target SDK.
    HOST_SDKROOT="$( (command -v xcrun >/dev/null 2>&1 && \
                      xcrun --sdk macosx --show-sdk-path) || true )"
    env -u PG_SYSROOT -u CC -u host_alias -u build_alias -u ZIC \
        -u PGL_HOST_TRIPLE -u PGL_TARGET_OS -u PGL_CONFIGURE_EXTRA \
        -u IPHONEOS_DEPLOYMENT_TARGET -u MACOSX_DEPLOYMENT_TARGET \
        SDKROOT="$HOST_SDKROOT" \
        bash native/build-libpglite.sh
    # initdb now, before the host tree moves: libpq's install name is absolute.
    "$SRC/native/out/install/bin/initdb" --allow-group-access \
        --encoding UTF8 --locale=C --locale-provider=libc --auth=trust \
        --no-sync -U postgres \
        -D "$SRC/native/out/pgdata-template" >/dev/null
    mv "$SRC/native/out" "$SRC/native/out-host"
    if [ -d "$SRC/native/out-cross-save" ]; then
      mv "$SRC/native/out-cross-save" "$SRC/native/out"
    fi
  fi
  mkdir -p "$SRC/native/out"
  WRAP="$SRC/native/out/glcad-target-cc"
  printf '#!/bin/sh\nexec %s "$@"\n' "$GLCAD_PGLITE_CC" > "$WRAP"
  chmod +x "$WRAP"
  export CC="$WRAP"
  export host_alias="$TRIPLE"
  export build_alias="$(bash postgres-pglite/config/config.guess)"
  ZIC_HOST="${GLCAD_PGLITE_ZIC:-$(command -v zic || true)}"
  [ -n "$ZIC_HOST" ] && export ZIC="$ZIC_HOST"
fi

bash native/build-libpglite.sh

OUT="$SRC/native/out"
PREFIX="$OUT/install"
NPROC="$( (command -v nproc >/dev/null 2>&1 && nproc) || sysctl -n hw.ncpu )"
VEC="$SRC/postgres-pglite/pglite/other_extensions/vector"
if [ "$TARGET_OS" = Windows ]; then
  # win32: pgvector links statically; a shell pg_config twin answers PGXS.
  cat > "$OUT/pg_config-cross" <<EOF
#!/bin/sh
case "\$1" in
  --bindir)            echo "$PREFIX/bin" ;;
  --docdir)            echo "$PREFIX/share/doc/postgresql" ;;
  --includedir)        echo "$PREFIX/include" ;;
  --pkgincludedir)     echo "$PREFIX/include/postgresql" ;;
  --includedir-server) echo "$PREFIX/include/postgresql/server" ;;
  --libdir)            echo "$PREFIX/lib" ;;
  --pkglibdir)         echo "$PREFIX/lib/postgresql" ;;
  --localedir)         echo "$PREFIX/share/locale" ;;
  --mandir)            echo "$PREFIX/share/man" ;;
  --sharedir)          echo "$PREFIX/share/postgresql" ;;
  --sysconfdir)        echo "$PREFIX/etc/postgresql" ;;
  --pgxs)              echo "$PREFIX/lib/postgresql/pgxs/src/makefiles/pgxs.mk" ;;
  *)                   echo "" ;;
esac
EOF
  chmod +x "$OUT/pg_config-cross"

  OBJS="$(sed -n 's/^OBJS = //p' "$VEC/Makefile")"
  EXTVERSION="$(sed -n 's/^EXTVERSION = //p' "$VEC/Makefile")"
  # BUILDING_DLL: PGDLLIMPORT globals stay direct references (static link).
  make -C "$VEC" USE_PGXS=1 PG_CONFIG="$OUT/pg_config-cross" \
       PG_CPPFLAGS=-DBUILDING_DLL -j"$NPROC" $OBJS "sql/vector--$EXTVERSION.sql"

  # Rename symbols colliding with plpgsql's; register every fmgr entry point.
  for o in $OBJS; do
    "$TRIPLE-objcopy" \
      --redefine-sym Pg_magic_func=pgl_vector_Pg_magic_func \
      --redefine-sym _PG_init=pgl_vector__PG_init "$VEC/$o"
  done
  SYMS="Pg_magic_func=pgl_vector_Pg_magic_func _PG_init=pgl_vector__PG_init"
  for f in $( (cd "$VEC" && "$TRIPLE-nm" --defined-only $OBJS) |
              awk '$2=="T" && $3 ~ /^pg_finfo_/ {print $3}' | sort -u ); do
    SYMS="$SYMS ${f#pg_finfo_}=${f#pg_finfo_} $f=$f"
  done
  {
    echo '/* Static-extension registry entry for pgvector (win32). */'
    echo 'extern void pgl_register_static_ext(const char *name,'
    echo '    const char *const *symbol_names, void *const *symbol_addrs, int nsyms);'
    for s in $SYMS; do echo "extern void ${s#*=}(void);"; done
    echo 'static const char *const vector_symbol_names[] = {'
    for s in $SYMS; do echo "	\"${s%%=*}\","; done
    echo '};'
    echo 'static void *const vector_symbol_addrs[] = {'
    for s in $SYMS; do echo "	(void *) ${s#*=},"; done
    echo '};'
    echo '__attribute__((constructor)) static void pgl_register_vector(void) {'
    echo '	pgl_register_static_ext("vector", vector_symbol_names, vector_symbol_addrs,'
    echo '		(int) (sizeof(vector_symbol_names) / sizeof(vector_symbol_names[0])));'
    echo '}'
  } > "$OUT/vector_static_ext.c"
  "$TRIPLE-gcc" -O2 -c "$OUT/vector_static_ext.c" -o "$OUT/vector_static_ext.o"

  (cd "$VEC" && "$TRIPLE-ar" rs "$OUT/libpglite.a" $OBJS)
  "$TRIPLE-ar" rs "$OUT/libpglite.a" "$OUT/vector_static_ext.o"

  mkdir -p "$PREFIX/share/postgresql/extension"
  cp "$VEC/vector.control" "$VEC"/sql/vector--*.sql \
     "$PREFIX/share/postgresql/extension/"
elif [ -n "$TRIPLE" ]; then
  # pg_config is a target executable: compile pgvector by hand, as PGXS would.
  case "$TRIPLE" in
    *-apple-*) MOD=vector.dylib; MODLD="-bundle -Wl,-undefined,dynamic_lookup" ;;
    *)         MOD=vector.so;    MODLD="-shared" ;;
  esac
  EXTVERSION="$(sed -n 's/^EXTVERSION *= *//p' "$VEC/Makefile")"
  for c in "$VEC"/src/*.c; do
    "$CC" -O2 -fPIC -ftree-vectorize -fassociative-math -fno-signed-zeros \
      -fno-trapping-math \
      -I"$PREFIX/include/postgresql/server" \
      -I"$PREFIX/include/postgresql/internal" \
      -c "$c" -o "${c%.c}.o"
  done
  "$CC" $MODLD -o "$VEC/$MOD" "$VEC"/src/*.o
  cp "$VEC/sql/vector.sql" "$VEC/sql/vector--$EXTVERSION.sql"
  mkdir -p "$PREFIX/lib/postgresql" "$PREFIX/share/postgresql/extension"
  cp "$VEC/$MOD" "$PREFIX/lib/postgresql/"
  cp "$VEC/vector.control" "$VEC"/sql/vector--*.sql \
     "$PREFIX/share/postgresql/extension/"
else
  make -C "$VEC" \
       USE_PGXS=1 PG_CONFIG="$PREFIX/bin/pg_config" -j"$NPROC"
  make -C "$VEC" \
       USE_PGXS=1 PG_CONFIG="$PREFIX/bin/pg_config" install
fi

# Template cluster, initdb'd once at build time; none for Windows (LLP64).
TPL="$PREFIX/pgdata-template"
if [ ! -f "$TPL/PG_VERSION" ]; then
  if [ "$TARGET_OS" = Windows ]; then
    :
  elif [ -n "$TRIPLE" ]; then
    rm -rf "$TPL"
    cp -R "$SRC/native/out-host/pgdata-template" "$TPL"
  else
    rm -rf "$TPL"
    "$PREFIX/bin/initdb" --allow-group-access --encoding UTF8 --locale=C \
        --locale-provider=libc --auth=trust --no-sync -U postgres \
        -D "$TPL" >/dev/null
  fi
fi

PARTS="bin lib share"
[ -d "$TPL" ] && PARTS="$PARTS pgdata-template"
(cd "$PREFIX" && COPYFILE_DISABLE=1 tar -czf \
    "$OUT/pglite-runtime.tar.gz" $PARTS)
