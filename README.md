# SHiP-RRIP+ Experiment (ChampSim CRC2)

This repository contains the SHiP-RRIP+ replacement policy and a reproducible script to compile and run it in the ChampSim CRC2 environment.

> **Note:** This repo assumes the CRC2-style ChampSim layout where replacement policy sources and helper `lru.cc` are in `champ_repl_pol/`, headers are in `inc/`, and traces are placed in `traces/`. Adjust paths in `reproduce.sh` if your layout differs.

## Files
- `champ_repl_pol/new_policy.cc` — the improved replacement policy to test.
- `champ_repl_pol/015_ship_rrip__signature_based_hit_predictor_with_srrip.cc` — baseline policy (original).
- `champ_repl_pol/lru.cc` — helper / driver (used to build a runnable binary).
- `reproduce.sh` — build + run script (macOS & Linux compatible).
- `plot_results.py` — optional Python script to plot IPC/MPKI from `results/summary.csv`.
- `traces/` — **place your five .champsimtrace.gz files here**.
- `results/` — generated outputs and `summary.csv`.

## Quick usage

1. Ensure you have the Command Line Tools installed (macOS) or a working g++ (Linux).
2. Make the script executable:
   ```bash
   chmod +x reproduce.sh
