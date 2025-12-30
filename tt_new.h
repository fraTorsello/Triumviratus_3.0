#pragma once
#ifndef TT_NEW_H
#define TT_NEW_H

#include "defs.h"
#include <atomic>

// number hash table entries
extern int hash_entries;

// no hash entry found constant
#define no_hash_entry 100000

// transposition table hash flags
#define hash_flag_exact 0
#define hash_flag_alpha 1
#define hash_flag_beta 2

// Lockless transposition table entry
// Uses XOR technique to detect torn reads/writes
typedef struct {
    U64 key;        // hash_key XOR data (for verification)
    U64 data;       // packed: score(16) | depth(8) | flag(8) | best_move(32)
} tt_entry;

// define TT instance
extern tt_entry* hash_table;

// Pack/unpack functions
inline U64 tt_pack_data(int score, int depth, int flag, int best_move) {
    return ((U64)(score + 32768) << 48) | 
           ((U64)(depth & 0xFF) << 40) | 
           ((U64)(flag & 0xFF) << 32) | 
           ((U64)(best_move & 0xFFFFFFFF));
}

inline void tt_unpack_data(U64 data, int* score, int* depth, int* flag, int* best_move) {
    *score = (int)((data >> 48) & 0xFFFF) - 32768;
    *depth = (int)((data >> 40) & 0xFF);
    *flag = (int)((data >> 32) & 0xFF);
    *best_move = (int)(data & 0xFFFFFFFF);
}

// PROTOTYPES
extern void init_hash_table(int mb);
extern int read_hash_entry(int alpha, int beta, int* best_move, int depth);
extern void write_hash_entry(int score, int best_move, int depth, int hash_flag);
extern void clear_hash_table();

// Thread-safe versions for multi-threaded search
extern int read_hash_entry_mt(U64 key, int ply, int alpha, int beta, int* best_move, int depth);
extern void write_hash_entry_mt(U64 key, int ply, int score, int best_move, int depth, int hash_flag);

#endif
