#include "tt.h"
#include "defs.h"
#include "search.h"
#include <stdlib.h>
#include <string.h>

// Global TT variables
int hash_entries = 0;
tt* hash_table = NULL;

void init_hash_table(int mb)
{
    int hash_size = 0x100000 * mb;
    hash_entries = hash_size / sizeof(tt);

    if (hash_table != NULL)
    {
        free(hash_table);
    }

    hash_table = (tt*)malloc(hash_entries * sizeof(tt));

    if (hash_table == NULL)
    {
        // Try with smaller size
        init_hash_table(mb / 2);
    }
    else
    {
        clear_hash_table();
    }
}

// read hash entry data
int read_hash_entry(int alpha, int beta, int* best_move, int depth)
{
    tt* hash_entry = &hash_table[hash_key % hash_entries];

    if (hash_entry->hash_key == hash_key)
    {
        if (hash_entry->depth >= depth)
        {
            int score = hash_entry->score;

            if (score < -mate_score) score += ply;
            if (score > mate_score) score -= ply;

            if (hash_entry->flag == hash_flag_exact)
                return score;

            if ((hash_entry->flag == hash_flag_alpha) && (score <= alpha))
                return alpha;

            if ((hash_entry->flag == hash_flag_beta) && (score >= beta))
                return beta;
        }

        *best_move = hash_entry->best_move;
    }

    return no_hash_entry;
}

// write hash entry data
void write_hash_entry(int score, int best_move, int depth, int hash_flag)
{
    tt* hash_entry = &hash_table[hash_key % hash_entries];

    if (score < -mate_score) score -= ply;
    if (score > mate_score) score += ply;

    hash_entry->hash_key = hash_key;
    hash_entry->score = score;
    hash_entry->flag = hash_flag;
    hash_entry->depth = depth;
    hash_entry->best_move = best_move;
}

// clear TT (hash table)
void clear_hash_table()
{
    tt* hash_entry;

    for (hash_entry = hash_table; hash_entry < hash_table + hash_entries; hash_entry++)
    {
        hash_entry->hash_key = 0;
        hash_entry->depth = 0;
        hash_entry->flag = 0;
        hash_entry->score = 0;
        hash_entry->best_move = 0;
    }
}
