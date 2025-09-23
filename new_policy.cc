#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE    1
#define LLC_SETS    (NUM_CORE * 2048)
#define LLC_WAYS    16

// RRPV configuration (3 bits → values 0..7)
static const int RRPV_BITS    = 3;
static const int MAX_RRPV     = (1 << RRPV_BITS) - 1;

// SHiP configuration
static const int SHCT_SIZE    = 1024;         // must be power of two
static const int SHCT_MAX     = 7;            // 3-bit counter max
static const int SHCT_INIT    = 4;            // initial counter value
static const int THRESHOLD    = SHCT_INIT;    // reuse threshold
static const int SIGN_SHIFT   = 4;            // signature = (PC>>SHIFT) & (SHCT_SIZE-1)

// Replacement state per block
static uint8_t  repl_rrpv   [NUM_CORE][LLC_SETS][LLC_WAYS];
static uint16_t repl_sig    [NUM_CORE][LLC_SETS][LLC_WAYS];  // PC signature index
static uint8_t  repl_reused [NUM_CORE][LLC_SETS][LLC_WAYS];  // reuse bit

// Global SHCT: per‐signature saturating counters
static uint8_t  SHCT[SHCT_SIZE];

// Statistics
static uint64_t stat_hits;
static uint64_t stat_misses;

// Initialize replacement state
void InitReplacementState() {
    stat_hits   = 0;
    stat_misses = 0;
    // Initialize RRPVs, signatures, reuse bits
    for (int c = 0; c < NUM_CORE; c++) {
        for (int s = 0; s < LLC_SETS; s++) {
            for (int w = 0; w < LLC_WAYS; w++) {
                repl_rrpv[c][s][w]   = MAX_RRPV;
                repl_sig[c][s][w]    = 0;
                repl_reused[c][s][w] = 0;
            }
        }
    }
    // Initialize SHCT to mid‐value
    for (int i = 0; i < SHCT_SIZE; i++) {
        SHCT[i] = SHCT_INIT;
    }
}

// Helper: saturating increment/decrement
static inline void sat_inc(uint8_t &c, uint8_t max_v) {
    if (c < max_v) c++;
}
static inline void sat_dec(uint8_t &c) {
    if (c > 0) c--;
}

// SRRIP victim selection (with adaptive second-pass aging)
uint32_t GetVictimInSet(
    uint32_t         cpu,
    uint32_t         set,
    const BLOCK     *current_set,
    uint64_t         PC,
    uint64_t         paddr,
    uint32_t         type
) {
    // First pass: try to find MAX_RRPV
    for (int attempt = 0; attempt < 2; ++attempt) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (repl_rrpv[cpu][set][w] == MAX_RRPV) {
                return w;
            }
        }
        // Aging: in the first attempt, age by +1; if still none found, do a stronger aging (+2 saturating)
        if (attempt == 0) {
            for (uint32_t w = 0; w < LLC_WAYS; w++) {
                if (repl_rrpv[cpu][set][w] < MAX_RRPV) {
                    repl_rrpv[cpu][set][w]++;
                }
            }
        } else {
            for (uint32_t w = 0; w < LLC_WAYS; w++) {
                if (repl_rrpv[cpu][set][w] < MAX_RRPV) {
                    // stronger aging to force eviction when stuck (helps streaming/thrashing)
                    repl_rrpv[cpu][set][w] = (repl_rrpv[cpu][set][w] + 2 > MAX_RRPV) ? MAX_RRPV : repl_rrpv[cpu][set][w] + 2;
                }
            }
        }
    }
    // Fallback (should not be reached): pick way 0
    return 0;
}

// Update replacement state on access or miss
void UpdateReplacementState(
    uint32_t cpu,
    uint32_t set,
    uint32_t way,
    uint64_t paddr,
    uint64_t PC,
    uint64_t victim_addr,
    uint32_t type,
    uint8_t  hit
) {
    // Local alias
    uint8_t &line_rrpv   = repl_rrpv[cpu][set][way];
    uint16_t &line_sig   = repl_sig[cpu][set][way];
    uint8_t &line_reused = repl_reused[cpu][set][way];

    if (hit) {
        // On hit: mark reused, promote to MRU AND strengthen SHCT for the signature
        stat_hits++;
        line_reused = 1;
        line_rrpv   = 0;
        // strengthen SHCT for this signature (helps learning faster)
        uint16_t sig = line_sig & (SHCT_SIZE - 1);
        if (sig < SHCT_SIZE) sat_inc(SHCT[sig], SHCT_MAX);
        return;
    }

    // On miss: we have evicted the old block at (set,way)
    stat_misses++;

    // Update SHCT based on whether the evicted block was reused
    uint16_t old_sig = line_sig & (SHCT_SIZE - 1);
    if (old_sig < SHCT_SIZE) {
        if (line_reused) {
            sat_inc(SHCT[old_sig], SHCT_MAX);
        } else {
            sat_dec(SHCT[old_sig]);
        }
    }

    // Compute new signature for incoming block (keep same scheme)
    uint16_t newsig = (uint32_t)(PC >> SIGN_SHIFT) & (SHCT_SIZE - 1);
    line_sig   = newsig;
    line_reused = 0;  // reset reuse bit

    // Adaptive insertion policy:
    // - Very confident predictors (SHCT >= THRESHOLD+2) -> strong MRU (RRPV = 0)
    // - Moderately confident (SHCT >= THRESHOLD) -> near-MRU (RRPV = 1)
    // - Weakly confident but not zero -> less favored (RRPV = MAX_RRPV - 1)
    // - Low confidence -> usual victim-friendly (RRPV = MAX_RRPV)
    uint8_t pred = SHCT[newsig];
    if (pred >= (uint8_t)(THRESHOLD + 2)) {
        line_rrpv = 0;
    } else if (pred >= (uint8_t)THRESHOLD) {
        line_rrpv = 1;
    } else if (pred > 0) {
        // give a small chance to survive short-term
        line_rrpv = (MAX_RRPV >= 2) ? MAX_RRPV - 1 : MAX_RRPV;
    } else {
        line_rrpv = MAX_RRPV;
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    std::cout << "=== SHiP-RRIP+ Statistics ===\n";
    std::cout << "  Total Hits    : " << stat_hits   << "\n";
    std::cout << "  Total Misses  : " << stat_misses << "\n";
}
