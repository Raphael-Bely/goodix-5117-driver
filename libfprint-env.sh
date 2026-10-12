# Source this to work against the locally built libfprint rather than the
# system one:
#
#   . ./libfprint-env.sh
#
# It only affects the shell you run it in. The distribution's libfprint in
# /usr stays untouched, and nothing here needs root.

LIBFPRINT_PREFIX="$HOME/.local/libfprint-dev"
LIBFPRINT_ARCH="$(gcc -print-multiarch)"

export PKG_CONFIG_PATH="$LIBFPRINT_PREFIX/lib/$LIBFPRINT_ARCH/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$LIBFPRINT_PREFIX/lib/$LIBFPRINT_ARCH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

echo "libfprint: $(pkg-config --modversion libfprint-2 2>/dev/null || echo 'not found')  from $LIBFPRINT_PREFIX"
