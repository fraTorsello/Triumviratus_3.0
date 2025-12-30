#pragma once
#ifndef THREADS_H
#define THREADS_H

#include "defs.h"
#include "search.h"
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>

#define MAX_THREADS 64

// Thread-local data structure
struct ThreadData {
    int thread_id;
    
    // Board state copy
    U64 bitboards[12];
    U64 occupancies[3];
    int side;
    int enpassant;
    int castle;
    U64 hash_key;
    int fifty;
    
    // Repetition detection
    U64 repetition_table[1000];
    int repetition_index;
    
    // Search state
    int ply;
    U64 nodes;
    
    // Move ordering
    int killer_moves[2][max_ply];
    int history_moves[12][64];
    
    // PV table
    int pv_length[max_ply];
    int pv_table[max_ply][max_ply];
    
    // Results
    int best_move;
    int best_score;
    int depth;
};

// Global thread management
extern std::vector<std::thread> search_threads;
extern std::vector<ThreadData> thread_data;
extern std::atomic<bool> stop_threads;
extern std::atomic<U64> total_nodes;
extern std::mutex cout_mutex;
extern int num_threads;

// Thread management functions
extern void init_threads(int thread_count);
extern void copy_board_to_thread(ThreadData& td);
extern void start_search_threads(int depth);
extern void stop_search_threads();
extern void wait_for_threads();

// Thread-local search function (exposed for search_mt.cpp)
extern int td_negamax(ThreadData& td, int alpha, int beta, int depth);

// Multi-threaded search
extern void search_position_mt(int depth);

#endif
