/*
 * Multi-threaded search implementation using Lazy SMP
 * With full UCI info output during iterative deepening
 */

#include "threads.h"
#include "search.h"
#include "misc.h"
#include "movegen.h"
#include "tt.h"
#include <iostream>
#include "defs.h"

extern U64 nodes;

// Print UCI info line - called after each depth
static void print_search_info(int depth, int score, U64 nodes_count, int time_ms, ThreadData& td) {
    // Calculate NPS
    U64 nps = 0;
    if (time_ms > 0) {
        nps = (nodes_count * 1000) / time_ms;
    }
    
    // Print score (mate or centipawns)
    if (score > -mate_value && score < -mate_score) {
        printf("info depth %d score mate %d nodes %llu nps %llu time %d pv ", 
               depth, -(score + mate_value) / 2 - 1, nodes_count, nps, time_ms);
    }
    else if (score > mate_score && score < mate_value) {
        printf("info depth %d score mate %d nodes %llu nps %llu time %d pv ", 
               depth, (mate_value - score) / 2 + 1, nodes_count, nps, time_ms);
    }
    else {
        printf("info depth %d score cp %d nodes %llu nps %llu time %d pv ", 
               depth, score, nodes_count, nps, time_ms);
    }
    
    // Print PV line
    for (int count = 0; count < td.pv_length[0]; count++) {
        print_move(td.pv_table[0][count]);
        printf(" ");
    }
    printf("\n");
    fflush(stdout);
}

// Thread search function with info output (for main thread)
static void main_thread_search(int depth, int start_time) {
    ThreadData& td = thread_data[0];
    copy_board_to_thread(td);

    memset(td.killer_moves, 0, sizeof(td.killer_moves));
    memset(td.history_moves, 0, sizeof(td.history_moves));
    memset(td.pv_table, 0, sizeof(td.pv_table));
    memset(td.pv_length, 0, sizeof(td.pv_length));

    int alpha = -infinity;
    int beta = infinity;

    for (int current_depth = 1; current_depth <= depth; current_depth++) {
        if (stop_threads.load(std::memory_order_relaxed)) break;

        int score = td_negamax(td, alpha, beta, current_depth);

        if (stop_threads.load(std::memory_order_relaxed)) break;

        // Aspiration window fail - research with full window
        if ((score <= alpha) || (score >= beta)) {
            alpha = -infinity;
            beta = infinity;
            current_depth--;
            continue;
        }

        // Set aspiration window for next iteration
        alpha = score - 50;
        beta = score + 50;

        // Update best move and print info
        if (td.pv_length[0] > 0) {
            td.best_move = td.pv_table[0][0];
            td.best_score = score;
            td.depth = current_depth;
            
            // Print info after each completed depth
            int elapsed = get_time_ms() - start_time;
            U64 current_nodes = total_nodes.load() + td.nodes;
            print_search_info(current_depth, score, current_nodes, elapsed, td);
        }
    }

    total_nodes.fetch_add(td.nodes, std::memory_order_relaxed);
}

// Helper thread function (no info output, just searches)
static void helper_thread_search(int thread_id, int depth) {
    ThreadData& td = thread_data[thread_id];
    copy_board_to_thread(td);

    memset(td.killer_moves, 0, sizeof(td.killer_moves));
    memset(td.history_moves, 0, sizeof(td.history_moves));
    memset(td.pv_table, 0, sizeof(td.pv_table));
    memset(td.pv_length, 0, sizeof(td.pv_length));

    int alpha = -infinity;
    int beta = infinity;

    for (int current_depth = 1; current_depth <= depth; current_depth++) {
        if (stop_threads.load(std::memory_order_relaxed)) break;

        // Helper threads search at slightly different depths for diversity
        int search_depth = current_depth + (thread_id % 2);
        if (search_depth > depth) search_depth = depth;

        int score = td_negamax(td, alpha, beta, search_depth);

        if (stop_threads.load(std::memory_order_relaxed)) break;

        if ((score <= alpha) || (score >= beta)) {
            alpha = -infinity;
            beta = infinity;
            current_depth--;
            continue;
        }

        alpha = score - 50;
        beta = score + 50;

        if (td.pv_length[0] > 0) {
            td.best_move = td.pv_table[0][0];
            td.best_score = score;
            td.depth = current_depth;
        }
    }

    total_nodes.fetch_add(td.nodes, std::memory_order_relaxed);
}

// Multi-threaded search position with full UCI output
void search_position_mt(int depth) {
    int start = get_time_ms();
    
    // Reset global state
    nodes = 0;
    stopped = 0;
    stop_threads.store(false, std::memory_order_relaxed);
    total_nodes.store(0, std::memory_order_relaxed);
    
    // Initialize thread data and copy board state to all threads
    for (int i = 0; i < num_threads; i++) {
        copy_board_to_thread(thread_data[i]);
    }
    
    // Clear previous search threads
    search_threads.clear();
    
    // Start helper threads (1 to num_threads-1)
    for (int i = 1; i < num_threads; i++) {
        search_threads.emplace_back(helper_thread_search, i, depth);
    }
    
    // Main thread (thread 0) searches with info output
    main_thread_search(depth, start);
    
    // Wait for helper threads to complete
    for (auto& t : search_threads) {
        if (t.joinable())
            t.join();
    }
    search_threads.clear();
    
    // Get results from main thread
    ThreadData& main_thread = thread_data[0];
    
    // Print best move
    printf("bestmove ");
    
    if (main_thread.best_move) {
        print_move(main_thread.best_move);
    } else if (main_thread.pv_table[0][0]) {
        print_move(main_thread.pv_table[0][0]);
    } else {
        printf("(none)");
    }
    
    printf("\n");
    fflush(stdout);
}
