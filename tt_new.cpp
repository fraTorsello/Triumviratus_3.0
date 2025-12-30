#include "tt_new.h"
#include "defs.h"
#include "search.h"
#include <stdlib.h>
#include <string.h>

// Global TT variables
int hash_entries = 0;
tt_entry* hash_table = NULL;

void init_hash_table(int mb)
{
    int hash_size = 0x100000 * mb;
    hash_entries = hash_size / sizeof(tt_entry);

    if (hash_table != NULL)
    {
        free(hash_table);
    }

    hash_table = (tt_entry*)malloc(hash_entries * sizeof(tt_entry));

    if (hash_table == NULL)
    {
        // Try with smaller size
        if (mb > 1)
            init_hash_table(mb / 2);
    }
    else
    {
        clear_hash_table();
    }
}

// Clear TT (hash table)
void clear_hash_table()
{
    if (hash_table == NULL) return;
    memset(hash_table, 0, hash_entries * sizeof(tt_entry));
}

// Read hash entry - single threaded version (uses global hash_key and ply)
int read_hash_entry(int alpha, int beta, int* best_move, int depth)
{
    return read_hash_entry_mt(hash_key, ply, alpha, beta, best_move, depth);
}

// Write hash entry - single threaded version
void write_hash_entry(int score, int best_move, int depth, int hash_flag)
{
    write_hash_entry_mt(hash_key, ply, score, best_move, depth, hash_flag);
}

// Thread-safe read using XOR verification
int read_hash_entry_mt(U64 key, int current_ply, int alpha, int beta, int* best_move, int depth)
{
    tt_entry* entry = &hash_table[key % hash_entries];
    
    // Read both values
    U64 stored_key = entry->key;
    U64 data = entry->data;
    
    // Verify entry integrity using XOR
    if ((stored_key ^ data) != key)
    {
        return no_hash_entry;
    }
    
    // Unpack data
    int stored_score, stored_depth, stored_flag, stored_move;
    tt_unpack_data(data, &stored_score, &stored_depth, &stored_flag, &stored_move);
    
    // Always return best move for move ordering
    *best_move = stored_move;
    
    if (stored_depth >= depth)
    {
        int score = stored_score;
        
        // Adjust mate scores relative to current ply
        if (score < -mate_score) score += current_ply;
        if (score > mate_score) score -= current_ply;
        
        if (stored_flag == hash_flag_exact)
            return score;
        
        if ((stored_flag == hash_flag_alpha) && (score <= alpha))
            return alpha;
        
        if ((stored_flag == hash_flag_beta) && (score >= beta))
            return beta;
    }
    
    return no_hash_entry;
}

// Thread-safe write using XOR technique
void write_hash_entry_mt(U64 key, int current_ply, int score, int best_move, int depth, int hash_flag)
{
    tt_entry* entry = &hash_table[key % hash_entries];
    
    // Adjust mate scores for storage
    int stored_score = score;
    if (stored_score < -mate_score) stored_score -= current_ply;
    if (stored_score > mate_score) stored_score += current_ply;
    
    // Pack data
    U64 data = tt_pack_data(stored_score, depth, hash_flag, best_move);
    
    // Store with XOR for verification
    entry->data = data;
    entry->key = key ^ data;
}
