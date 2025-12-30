/*
 * Triumviratus Chess Engine - Main Entry Point
 * UCI compliant - no debug output on startup
 */

#include "defs.h"
#include "io.h"
#include "tt.h"
#include "uci.h"
#include "threads.h"

int main()
{
    // Initialize all bitboards and attack tables (silent)
    init_bitboards();
    
    // Initialize hash table with default 64 MB (silent)
    init_hash_table(64);
    
    // Initialize NNUE (silent for UCI compliance)
    init_nnue("nn-eba324f53044.nnue");
    
    // Run UCI loop
    uci_loop();
    
    return 0;
}
