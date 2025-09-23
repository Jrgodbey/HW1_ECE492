#include <vector> 
#include <cstdint> 
#include <iostream> 
#include "../inc/champsim_crc2.h" 

#define NUM_CORE 1 
#define LLC_SETS (NUM_CORE * 2048) 
#define LLC_WAYS 16 

// RRPV configuration (3 bits → values 0..7)
 static const int RRPV_BITS = 3; 
 static const int MAX_RRPV = (1 << RRPV_BITS) - 1; 
 
// SHiP configuration 
static const int SHCT_SIZE = 1024; // must be power of two 
static const int SHCT_MAX = 7; // 3-bit counter max 
static const int SHCT_INIT = 4; // initial counter value 
static const int THRESHOLD = SHCT_INIT; // reuse threshold 
static const int SIGN_SHIFT = 4; // signature = (PC>>SHIFT) & (SHCT_SIZE-1) 

// Replacement state per block 
static uint8_t repl_rrpv [NUM_CORE][LLC_SETS][LLC_WAYS]; 
static uint16_t repl_sig [NUM_CORE][LLC_SETS][LLC_WAYS]; // PC signature index 
static uint8_t repl_reused [NUM_CORE][LLC_SETS][LLC_WAYS]; // reuse bit 

// Global SHCT: per‐signature saturating counters 
static uint8_t SHCT[SHCT_SIZE]; 

// Statistics 
static uint64_t stat_hits; 
static uint64_t stat_misses; 

// Initialize replacement state 
void InitReplacementState() { 
	stat_hits = 0; 
	stat_misses = 0; 
	// Initialize RRPVs, signatures, reuse bits 
	for (int c = 0; c < NUM_CORE; c++) { 
		for (int s = 0; s < LLC_SETS; s++) { 
			for (int w = 0; w < LLC_WAYS; w++) { 
				repl_rrpv[c][s][w] = MAX_RRPV; 
				repl_sig[c][s][w] = 0; 
				repl_reused[c][s][w] = 0; 
			} 
		} 
	} 
	// Initialize SHCT to mid‐value 
	for (int i = 0; i < SHCT_SIZE; i++) { 
		SHCT[i] = SHCT_INIT; 
	} 
} 

// SRRIP victim selection 
uint32_t GetVictimInSet( 
	uint32_t cpu, 
	uint32_t set, 
	const BLOCK *current_set, 
	uint64_t PC, 
	uint64_t paddr, 
	uint32_t type 
) { 
	// Find way with RRPV == MAX_RRPV, aging if necessary 
	while (true) {
 		for (uint32_t w = 0; w < LLC_WAYS; w++) { 
 			if (repl_rrpv[cpu][set][w] == MAX_RRPV) { 
 				return w; 
 			} 
 		} 
 		// Age all blocks by +1 (saturating at MAX_RRPV) 
 		for (uint32_t w = 0; w < LLC_WAYS; w++) { 
 			if (repl_rrpv[cpu][set][w] < MAX_RRPV) { 
 				repl_rrpv[cpu][set][w]++;
  			} 
  		} 
  	} 
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
		uint8_t hit 
	) { 
	   if (hit) { 
		  // On hit: mark reused and promote to MRU 
		  stat_hits++; 
		  repl_reused[cpu][set][way] = 1; 
		  repl_rrpv [cpu][set][way] = 0; 
		  return; 
} 
// On miss: we have just evicted the old block at (set,way) 
stat_misses++; 
uint16_t old_sig = repl_sig [cpu][set][way]; 
uint8_t old_r = repl_reused[cpu][set][way]; 
// Update SHCT for the evicted signature 
if (old_r) { 
	if (SHCT[old_sig] < SHCT_MAX) SHCT[old_sig]++; 
} else {
    if (SHCT[old_sig] > 0)    SHCT[old_sig]--; 
} 
// Compute new signature for incoming block 
uint16_t newsig = (uint32_t)(PC >> SIGN_SHIFT) & (SHCT_SIZE - 1); 
repl_sig [cpu][set][way] = newsig; 
repl_reused[cpu][set][way] = 0; // reset reuse bit 
// Insert: strong predictor → RRPV=0, else RRPV=MAX_RRPV-1 
if (SHCT[newsig] >= THRESHOLD) { 
	repl_rrpv[cpu][set][way] = 0; 
} else {
 	repl_rrpv[cpu][set][way] = MAX_RRPV - 1; 
 } 
} 

// Print end-of-simulation statistics 
void PrintStats() { 
	std::cout << "=== SHiP-RRIP Statistics ===\n"; 
	std::cout << " Total Hits : " << stat_hits << "\n"; 
	std::cout << " Total Misses : " << stat_misses << "\n"; 
} 

// Print periodic (heartbeat) statistics 
void PrintStats_Heartbeat() { 
	PrintStats(); 
}