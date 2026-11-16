#!/usr/bin/env bash
#
# inspect.sh - inspect libseastar.a (or .so) and detect its CMake build type
#
# Seastar build types (from upstream CMakeLists.txt):
#   Debug, Sanitize, Fuzz : SEASTAR_DEBUG + SEASTAR_DEFAULT_ALLOCATOR, ASan/UBSan on
#   Dev                   : -O1 (GCC) / -O2 (Clang), no -g, SEASTAR_TYPE_ERASE_MORE
#   Release               : -O3 -DNDEBUG (CMake default)
#   RelWithDebInfo        : -O2 -g -DNDEBUG (CMake default)
#   MinSizeRel            : -Os -DNDEBUG (CMake default)
#
# Evidence is gathered in this order of authority:
#   1. Build metadata next to the library (CMakeCache.txt, SeastarTargets-<cfg>.cmake)
#   2. Heuristics on the binary: sanitizer refs, Seastar allocator symbols,
#      debug info, and the -O level recorded in DWARF / .GCC.command.line
#
# Usage: inspect.sh [-q] [-n N] <libseastar.a | libseastar.so>

set -u
export LC_ALL=C

PROD_SAMPLE=300
QUIET=0

usage() {
  cat <<EOF
Usage: ${0##*/} [-q] [-n N] <libseastar.a | libseastar.so>

  -q    quiet: print only the verdict (for scripting)
  -n N  number of DWARF compile units to sample (default: $PROD_SAMPLE)
  -h    show this help
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

while getopts ":qn:h" opt; do
  case $opt in
  q) QUIET=1 ;;
  n) PROD_SAMPLE=$OPTARG ;;
  h)
    usage
    exit 0
    ;;
  *)
    usage >&2
    exit 1
    ;;
  esac
done
shift $((OPTIND - 1))

[[ $# -eq 1 ]] || {
  usage >&2
  exit 1
}
LIB=$1
[[ -f $LIB ]] || die "no such file: $LIB"
[[ $PROD_SAMPLE =~ ^[0-9]+$ && $PROD_SAMPLE -gt 0 ]] || die "-n needs a positive integer"

for tool in nm readelf grep sed awk sort uniq mktemp readlink head cut tr; do
  command -v "$tool" >/dev/null 2>&1 || die "required tool not found: $tool"
done

tmp=$(mktemp -d) || die "mktemp failed"
trap 'rm -rf "$tmp"' EXIT

row() { ((QUIET)) || printf '  %-14s %s\n' "$1" "$2"; }
say() { ((QUIET)) || printf '%s\n' "$*"; }

# ---------------------------------------------------------------------------
# 1. Symbols
# ---------------------------------------------------------------------------
nm "$LIB" >"$tmp/nm.txt" 2>/dev/null
nm -u "$LIB" >"$tmp/nm_u.txt" 2>/dev/null
if [[ ! -s $tmp/nm.txt ]]; then
  nm -D "$LIB" >"$tmp/nm.txt" 2>/dev/null
  nm -D -u "$LIB" >"$tmp/nm_u.txt" 2>/dev/null
fi
[[ -s $tmp/nm.txt ]] || die "nm found no symbols (stripped, or not an ELF library?)"

def_sym() { grep -qE " [TW] $1\$" "$tmp/nm.txt"; }

if def_sym malloc && def_sym free && def_sym _Znwm; then
  alloc=yes # Seastar's own allocator is compiled in (no SEASTAR_DEFAULT_ALLOCATOR)
else
  alloc=no
fi

asan=$(grep -cE '__(asan|ubsan|tsan|msan)_' "$tmp/nm_u.txt" || true)
cov=$(grep -cE '__sanitizer_cov_' "$tmp/nm_u.txt" || true)

# ---------------------------------------------------------------------------
# 2. Debug info, compiler, optimisation level
# ---------------------------------------------------------------------------
dbg=$(readelf -S "$LIB" 2>/dev/null | grep -c '\.debug_' || true)

: >"$tmp/prod.txt"
if ((dbg > 0)); then
  readelf --debug-dump=info "$LIB" 2>/dev/null |
    grep -m "$PROD_SAMPLE" 'DW_AT_producer' >"$tmp/prod.txt" || true
fi

# Present only if built with -frecord-gcc-switches
readelf -p .GCC.command.line "$LIB" 2>/dev/null |
  grep -E '^[[:space:]]*\[[[:space:]]*[0-9a-fA-F]+\]' | head -n 500 >"$tmp/cmd.txt" || true

opt_from_stream() {
  grep -oE -- ' ?-O(0|1|2|3|s|g|z|fast)\b' |
    sed -E 's/^[[:space:]]+//; s/^-//' |
    sort | uniq -c | sort -rn | awk 'NR==1{print $2}'
}

opt_prod=""
opt_cmd=""
[[ -s $tmp/prod.txt ]] && opt_prod=$(opt_from_stream <"$tmp/prod.txt")
[[ -s $tmp/cmd.txt ]] && opt_cmd=$(opt_from_stream <"$tmp/cmd.txt")

flags_recorded=0
if [[ -s $tmp/cmd.txt ]] || grep -qE ' -(g|std=|m)' "$tmp/prod.txt" 2>/dev/null; then
  flags_recorded=1
fi

if [[ -n $opt_cmd ]]; then
  opt=$opt_cmd
  opt_src=".GCC.command.line"
elif [[ -n $opt_prod ]]; then
  opt=$opt_prod
  opt_src="DWARF producer"
elif ((flags_recorded)); then
  opt=O0
  opt_src="no -O flag recorded, assuming -O0"
else
  opt=unknown
  opt_src="compiler does not record flags"
fi

producer=""
compiler="unknown"
gz="unknown"
if [[ -s $tmp/prod.txt ]]; then
  producer=$(head -n1 "$tmp/prod.txt" |
    sed -E 's/.*DW_AT_producer[[:space:]]*:[[:space:]]*(\([^)]*\):[[:space:]]*)?//')
  compiler=${producer%% -*}
  if [[ $producer == *" -gz"* ]]; then gz=yes; else gz=no; fi
fi

if grep -q -- '-DNDEBUG' "$tmp/cmd.txt" 2>/dev/null; then
  ndebug=yes
else
  ndebug=unknown # -D macros are usually not recorded, so absence proves nothing
fi

# ---------------------------------------------------------------------------
# 3. Build metadata next to the library
# ---------------------------------------------------------------------------
real=$(readlink -f -- "$LIB")
dir=$(dirname -- "$real")

canon() {
  case ${1,,} in
  release) echo Release ;;
  relwithdebinfo) echo RelWithDebInfo ;;
  minsizerel) echo MinSizeRel ;;
  debug) echo Debug ;;
  dev) echo Dev ;;
  sanitize) echo Sanitize ;;
  fuzz) echo Fuzz ;;
  *) echo "$1" ;;
  esac
}

# SeastarTargets-<config>.cmake
cfg_raw=""
for d in "$dir" "$dir/cmake/Seastar" "$dir/../lib/cmake/Seastar" \
  "$dir/../lib64/cmake/Seastar" "$dir/../share/cmake/Seastar"; do
  for f in "$d"/SeastarTargets-*.cmake; do
    [[ -e $f ]] || continue
    c=${f##*/SeastarTargets-}
    cfg_raw+="${c%.cmake}"$'\n'
  done
done
cfg_list=$(printf '%s' "$cfg_raw" | sort -u | sed '/^$/d')
cfg_count=0
[[ -n $cfg_list ]] && cfg_count=$(printf '%s\n' "$cfg_list" | wc -l)

# CMakeCache.txt (walk up a few levels; must be a cache that knows about Seastar)
cache=""
d=$dir
for _ in 1 2 3 4 5; do
  if [[ -f $d/CMakeCache.txt ]] && grep -q '^Seastar_' "$d/CMakeCache.txt"; then
    cache=$d/CMakeCache.txt
    break
  fi
  [[ $d == / ]] && break
  d=$(dirname -- "$d")
done

cache_type=""
if [[ -n $cache ]]; then
  cache_type=$(grep -m1 '^CMAKE_BUILD_TYPE:' "$cache" | cut -d= -f2-)
fi

# seastar.pc (public compile definitions differ per build type)
pc=""
for f in "$dir/seastar.pc" "$dir/pkgconfig/seastar.pc" \
  "$dir/../lib/pkgconfig/seastar.pc" "$dir/../lib64/pkgconfig/seastar.pc"; do
  [[ -f $f ]] && {
    pc=$f
    break
  }
done
pc_defs=""
if [[ -n $pc ]]; then
  pc_defs=$(grep -hoE -- '-DSEASTAR_[A-Z0-9_]+(=[^ ]*)?' "$pc" | sort -u | tr '\n' ' ')
fi
has_def() { [[ " $pc_defs" == *" -D$1 "* ]]; }

# ---------------------------------------------------------------------------
# 4. Heuristic classification
# ---------------------------------------------------------------------------
heur=""
conf=""
why=""

if ((cov > 0)); then
  heur=Fuzz
  conf=high
  why="fuzzer coverage instrumentation symbols present"

elif ((asan > 0)) || [[ $alloc == no ]]; then
  # Debug / Sanitize / Fuzz: SEASTAR_DEFAULT_ALLOCATOR and sanitizers
  case $opt in
  Os)
    heur=Sanitize
    conf=medium
    why="sanitizer/default-allocator build at -Os (Sanitize uses -Os -g)"
    ;;
  O0)
    heur=Debug
    conf=medium
    why="sanitizer/default-allocator build, unoptimised"
    ;;
  unknown)
    heur="Debug or Sanitize"
    conf=low
    why="sanitizer/default-allocator build, optimisation level unknown"
    ;;
  *)
    heur="Debug or Sanitize (custom flags)"
    conf=low
    why="sanitizer/default-allocator build with -$opt"
    ;;
  esac

elif has_def SEASTAR_TYPE_ERASE_MORE; then
  heur=Dev
  conf=high
  why="pkg-config defines SEASTAR_TYPE_ERASE_MORE but not SEASTAR_DEBUG"

elif ((dbg > 0)); then
  case $opt in
  O2)
    heur=RelWithDebInfo
    conf=medium
    why="Seastar allocator, no sanitizers, -O2 with debug info"
    ;;
  O1)
    heur="Dev (with -g)"
    conf=low
    why="-O1 is Dev's GCC level; debug info is not a Dev default"
    ;;
  O3)
    heur="Release (with -g, custom flags)"
    conf=low
    why="-O3 plus debug info"
    ;;
  Os)
    heur="MinSizeRel (with -g, custom flags)"
    conf=low
    why="-Os plus debug info"
    ;;
  O0)
    heur="Custom (unoptimised, Seastar allocator)"
    conf=low
    why="-O0 with Seastar allocator is not a stock build type"
    ;;
  *)
    heur="RelWithDebInfo or Dev"
    conf=low
    why="debug info present, optimisation level unknown"
    ;;
  esac

else
  case $opt in
  O3)
    if [[ $ndebug == yes ]]; then conf=high; else conf=medium; fi
    heur=Release
    why="-O3, no debug info"
    ;;
  Os)
    heur=MinSizeRel
    conf=medium
    why="-Os, no debug info"
    ;;
  O1)
    heur=Dev
    conf=medium
    why="-O1 without -g matches Dev on GCC"
    ;;
  O2)
    heur="Dev (Clang)"
    conf=low
    why="-O2 without -g matches Dev on Clang"
    ;;
  *)
    heur="Release (or MinSizeRel/Dev)"
    conf=low
    why="no debug info, so the -O level is not recoverable from the binary"
    ;;
  esac
fi

# ---------------------------------------------------------------------------
# 5. Final verdict
# ---------------------------------------------------------------------------
verdict=""
vsrc=""
vconf=""
if [[ -n $cache_type ]]; then
  verdict=$(canon "$cache_type")
  vsrc="CMakeCache.txt"
  vconf=high
elif ((cfg_count == 1)); then
  verdict=$(canon "$cfg_list")
  vsrc="SeastarTargets-*.cmake"
  vconf=high
else
  verdict=$heur
  vsrc="heuristic"
  vconf=$conf
fi

if ((QUIET)); then
  printf '%s\n' "$verdict"
  exit 0
fi

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
echo "Seastar library inspection"
echo
echo "File"
row "path" "$real"
echo
echo "Binary evidence"
row "compiler" "$compiler"
if [[ $opt == unknown ]]; then
  row "opt level" "unknown ($opt_src)"
else
  row "opt level" "-$opt ($opt_src)"
fi
row "debug info" "$([[ $dbg -gt 0 ]] && echo "yes ($dbg .debug_* sections, -gz: $gz)" || echo no)"
row "-DNDEBUG" "$ndebug"
row "allocator" "$([[ $alloc == yes ]] && echo 'Seastar allocator (malloc/free/operator new defined)' ||
  echo 'default libc allocator (SEASTAR_DEFAULT_ALLOCATOR)')"
row "sanitizers" "$([[ $asan -gt 0 ]] && echo "yes ($asan asan/ubsan/tsan/msan refs)" || echo none)"
row "fuzzer" "$([[ $cov -gt 0 ]] && echo "yes ($cov coverage refs)" || echo none)"

echo
echo "Build metadata"
if [[ -n $cache ]]; then
  row "CMakeCache" "$cache"
  grep -E '^(CMAKE_BUILD_TYPE|CMAKE_CXX_COMPILER|CMAKE_CXX_STANDARD|CMAKE_CXX_FLAGS|Seastar_(SANITIZE|DEBUG_SHARED_PTR|STACK_GUARDS|ALLOC_FAILURE_INJECTION|API_LEVEL|ALLOC_PAGE_SIZE|SSTRING|TASK_BACKTRACE|DEBUG_ALLOCATIONS|HEAP_PROFILING|SPLIT_DWARF|COMPRESS_DEBUG)):' "$cache" |
    sed -E 's/:[A-Z]+=/ = /' | sed 's/^/                 /'
else
  row "CMakeCache" "not found near the library"
fi
if ((cfg_count > 0)); then
  row "targets cfg" "$(printf '%s' "$cfg_list" | tr '\n' ' ')"
else
  row "targets cfg" "no SeastarTargets-<config>.cmake found"
fi
if [[ -n $pc ]]; then
  row "pkg-config" "$pc"
  row "pc defines" "${pc_defs:-none}"
else
  row "pkg-config" "seastar.pc not found near the library"
fi

echo
echo "Heuristic: $heur (confidence: $conf)"
echo "  reason:  $why"
echo
echo "Verdict:   $verdict  (source: $vsrc, confidence: $vconf)"

if [[ $vsrc != heuristic ]]; then
  if [[ ${heur,,} != *"${verdict,,}"* ]]; then
    echo
    echo "note: the binary heuristics suggest '$heur', which differs from the metadata."
    echo "      The CMakeCache/targets file may belong to a different build than this library."
  fi
elif [[ $vconf == low ]]; then
  echo
  echo "note: low confidence. Release-style builds carry no debug info, so the -O level"
  echo "      cannot be read from the binary. Look for CMakeCache.txt or"
  echo "      SeastarTargets-<config>.cmake from the same build, or rebuild with"
  echo "      -frecord-gcc-switches to make the flags recoverable."
fi
