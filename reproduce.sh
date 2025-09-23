#!/usr/bin/env bash
set -euo pipefail

# ----- Config -----
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

INC_DIR="inc"
POLICY_DIR="champ_repl_pol"

# Policy filenames in champ_repl_pol
BASELINE_SRC="${POLICY_DIR}/015_ship_rrip__signature_based_hit_predictor_with_srrip.cc"
NEW_SRC="${POLICY_DIR}/new_policy.cc"
HELPER_SRC="${POLICY_DIR}/lru.cc"    # driver/main used by CRC2 builds

BASELINE_BIN="baseline_bin"
NEW_BIN="new_policy_bin"
RESULTS_DIR="results"
mkdir -p "$RESULTS_DIR"

# Workloads - edit to match your traces
TRACES=(
  "traces/mcf.champsimtrace.gz"
  "traces/gcc.champsimtrace.gz"
  "traces/bzip2.champsimtrace.gz"
  "traces/sphinx3.champsimtrace.gz"
  "traces/omnetpp.champsimtrace.gz"
)

# Simulation parameters - edit to course spec if needed
WARMUP=200000000
SIM=1000000000

# ----- Compiler selection -----
OS="$(uname -s)"
if [[ "$OS" == "Darwin" ]]; then
  # macOS (Apple Silicon)
  SDK_PATH="$(xcrun --show-sdk-path)"
  CXX="clang++"
  CXXFLAGS="-std=c++11 -stdlib=libc++ -Wall -O2 -isysroot ${SDK_PATH} -I${INC_DIR}"
else
  # Linux
  CXX="g++"
  CXXFLAGS="-std=c++11 -Wall -O2 -I${INC_DIR}"
fi

echo "Using compiler: $CXX"
echo "Compiler flags: $CXXFLAGS"

compile_policy() {
  local src="$1"
  local out="$2"
  echo "Compiling $src -> $out"
  # compile the policy together with the helper main (lru.cc); helper should provide main()
  $CXX $CXXFLAGS "$src" "$HELPER_SRC" -o "$out" || {
    echo "Compilation failed for $src"
    return 1
  }
  echo "Compiled $out"
}

# ----- Compile baseline and new policy -----
compile_policy "$BASELINE_SRC" "$BASELINE_BIN"
compile_policy "$NEW_SRC" "$NEW_BIN"

# ----- Run experiments -----
echo "trace,policy,ipc,mpki,raw_output_file" > "${RESULTS_DIR}/summary.csv"
for TRACE in "${TRACES[@]}"; do
  if [ ! -f "$TRACE" ]; then
    echo "Warning: trace $TRACE not found - skipping"
    continue
  fi

  for BIN in "$BASELINE_BIN" "$NEW_BIN"; do
    LABEL=$(basename "$BIN")
    OUTFILE="${RESULTS_DIR}/$(basename ${TRACE}).${LABEL}.out"
    echo "Running $BIN on $TRACE -> $OUTFILE"
    ./"$BIN" --warmup_instructions $WARMUP --simulation_instructions $SIM "$TRACE" > "$OUTFILE" 2>&1 || true

    # Extract IPC - expects a line like "CPU 0 cumulative IPC: 1.72"
    IPC=$(grep -i "CPU 0 cumulative IPC" "$OUTFILE" 2>/dev/null | awk -F: '{print $2}' | tr -d ' ' | tr -d '\r' | head -n1 || echo "NA")

    # Extract MPKI or LLC misses per 1000 instr - try two common formats:
    MPKI=$(grep -i -E "LLC misses per 1000 instructions|LLC misses per 1000 instr|LLC TOTAL MPKI" "$OUTFILE" 2>/dev/null | awk -F: '{print $2}' | tr -d ' ' | tr -d '\r' | head -n1 || echo "NA")

    echo "$(basename $TRACE),${LABEL},${IPC},${MPKI},${OUTFILE}" >> "${RESULTS_DIR}/summary.csv"
  done
done

echo "Done. Results in ${RESULTS_DIR}/summary.csv"
