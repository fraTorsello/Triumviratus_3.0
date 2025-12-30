#include "threads.h"
#include "search.h"
#include "movegen.h"
#include "misc.h"
#include "evaluation.h"
#include "tt.h"
#include "magic.h"
#include "attacks.h"
#include "see.h"
#include <algorithm>
#include <iostream>
#include "defs.h"
extern U64 nodes;

// Global thread management variables
std::vector<std::thread> search_threads;
std::vector<ThreadData> thread_data;
std::atomic<bool> stop_threads(false);
std::atomic<U64> total_nodes(0);
std::mutex cout_mutex;
int num_threads = 1;

void init_threads(int thread_count) {
    if (thread_count < 1) thread_count = 1;
    if (thread_count > MAX_THREADS) thread_count = MAX_THREADS;

    num_threads = thread_count;
    thread_data.resize(num_threads);

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        memset(thread_data[i].killer_moves, 0, sizeof(thread_data[i].killer_moves));
        memset(thread_data[i].history_moves, 0, sizeof(thread_data[i].history_moves));
        memset(thread_data[i].pv_table, 0, sizeof(thread_data[i].pv_table));
        memset(thread_data[i].pv_length, 0, sizeof(thread_data[i].pv_length));
    }

    printf("info string Initialized %d thread(s)\n", num_threads);
}

void copy_board_to_thread(ThreadData& td) {
    memcpy(td.bitboards, bitboards, sizeof(bitboards));
    memcpy(td.occupancies, occupancies, sizeof(occupancies));
    td.side = side;
    td.enpassant = enpassant;
    td.castle = castle;
    td.hash_key = hash_key;
    td.fifty = fifty;
    memcpy(td.repetition_table, repetition_table, sizeof(repetition_table));
    td.repetition_index = repetition_index;
    td.ply = 0;
    td.nodes = 0;
    td.best_move = 0;
    td.best_score = -infinity;
}

static inline int td_is_square_attacked(ThreadData& td, int square, int attacker_side) {
    if ((attacker_side == white) && (pawn_attacks[black][square] & td.bitboards[P])) return 1;
    if ((attacker_side == black) && (pawn_attacks[white][square] & td.bitboards[p])) return 1;
    if (knight_attacks[square] & ((attacker_side == white) ? td.bitboards[N] : td.bitboards[n])) return 1;
    if (get_bishop_attacks(square, td.occupancies[both]) & ((attacker_side == white) ? td.bitboards[B] : td.bitboards[b])) return 1;
    if (get_rook_attacks(square, td.occupancies[both]) & ((attacker_side == white) ? td.bitboards[R] : td.bitboards[r])) return 1;
    if (get_queen_attacks(square, td.occupancies[both]) & ((attacker_side == white) ? td.bitboards[Q] : td.bitboards[q])) return 1;
    if (king_attacks[square] & ((attacker_side == white) ? td.bitboards[K] : td.bitboards[k])) return 1;
    return 0;
}

static inline void td_generate_moves(ThreadData& td, moves* move_list) {
    move_list->count = 0;
    int source_square, target_square;
    U64 bitboard, attacks;

    for (int piece = P; piece <= k; piece++) {
        bitboard = td.bitboards[piece];

        if (td.side == white) {
            if (piece == P) {
                while (bitboard) {
                    source_square = get_ls1b_index(bitboard);
                    target_square = source_square - 8;
                    if (!(target_square < a8) && !get_bit(td.occupancies[both], target_square)) {
                        if (source_square >= a7 && source_square <= h7) {
                            add_move(move_list, encode_move(source_square, target_square, piece, Q, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, R, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, B, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, N, 0, 0, 0, 0));
                        }
                        else {
                            add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                            if ((source_square >= a2 && source_square <= h2) && !get_bit(td.occupancies[both], target_square - 8))
                                add_move(move_list, encode_move(source_square, target_square - 8, piece, 0, 0, 1, 0, 0));
                        }
                    }
                    attacks = pawn_attacks[td.side][source_square] & td.occupancies[black];
                    while (attacks) {
                        target_square = get_ls1b_index(attacks);
                        if (source_square >= a7 && source_square <= h7) {
                            add_move(move_list, encode_move(source_square, target_square, piece, Q, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, R, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, B, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, N, 1, 0, 0, 0));
                        }
                        else {
                            add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                        }
                        pop_bit(attacks, target_square);
                    }
                    if (td.enpassant != no_sq) {
                        U64 enpassant_attacks = pawn_attacks[td.side][source_square] & (1ULL << td.enpassant);
                        if (enpassant_attacks) {
                            int target_enpassant = get_ls1b_index(enpassant_attacks);
                            add_move(move_list, encode_move(source_square, target_enpassant, piece, 0, 1, 0, 1, 0));
                        }
                    }
                    pop_bit(bitboard, source_square);
                }
            }
            if (piece == K) {
                if (td.castle & wk) {
                    if (!get_bit(td.occupancies[both], f1) && !get_bit(td.occupancies[both], g1)) {
                        if (!td_is_square_attacked(td, e1, black) && !td_is_square_attacked(td, f1, black))
                            add_move(move_list, encode_move(e1, g1, piece, 0, 0, 0, 0, 1));
                    }
                }
                if (td.castle & wq) {
                    if (!get_bit(td.occupancies[both], d1) && !get_bit(td.occupancies[both], c1) && !get_bit(td.occupancies[both], b1)) {
                        if (!td_is_square_attacked(td, e1, black) && !td_is_square_attacked(td, d1, black))
                            add_move(move_list, encode_move(e1, c1, piece, 0, 0, 0, 0, 1));
                    }
                }
            }
        }
        else {
            if (piece == p) {
                while (bitboard) {
                    source_square = get_ls1b_index(bitboard);
                    target_square = source_square + 8;
                    if (!(target_square > h1) && !get_bit(td.occupancies[both], target_square)) {
                        if (source_square >= a2 && source_square <= h2) {
                            add_move(move_list, encode_move(source_square, target_square, piece, q, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, r, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, b, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, n, 0, 0, 0, 0));
                        }
                        else {
                            add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                            if ((source_square >= a7 && source_square <= h7) && !get_bit(td.occupancies[both], target_square + 8))
                                add_move(move_list, encode_move(source_square, target_square + 8, piece, 0, 0, 1, 0, 0));
                        }
                    }
                    attacks = pawn_attacks[td.side][source_square] & td.occupancies[white];
                    while (attacks) {
                        target_square = get_ls1b_index(attacks);
                        if (source_square >= a2 && source_square <= h2) {
                            add_move(move_list, encode_move(source_square, target_square, piece, q, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, r, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, b, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, n, 1, 0, 0, 0));
                        }
                        else {
                            add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                        }
                        pop_bit(attacks, target_square);
                    }
                    if (td.enpassant != no_sq) {
                        U64 enpassant_attacks = pawn_attacks[td.side][source_square] & (1ULL << td.enpassant);
                        if (enpassant_attacks) {
                            int target_enpassant = get_ls1b_index(enpassant_attacks);
                            add_move(move_list, encode_move(source_square, target_enpassant, piece, 0, 1, 0, 1, 0));
                        }
                    }
                    pop_bit(bitboard, source_square);
                }
            }
            if (piece == k) {
                if (td.castle & bk) {
                    if (!get_bit(td.occupancies[both], f8) && !get_bit(td.occupancies[both], g8)) {
                        if (!td_is_square_attacked(td, e8, white) && !td_is_square_attacked(td, f8, white))
                            add_move(move_list, encode_move(e8, g8, piece, 0, 0, 0, 0, 1));
                    }
                }
                if (td.castle & bq) {
                    if (!get_bit(td.occupancies[both], d8) && !get_bit(td.occupancies[both], c8) && !get_bit(td.occupancies[both], b8)) {
                        if (!td_is_square_attacked(td, e8, white) && !td_is_square_attacked(td, d8, white))
                            add_move(move_list, encode_move(e8, c8, piece, 0, 0, 0, 0, 1));
                    }
                }
            }
        }

        if ((td.side == white) ? piece == N : piece == n) {
            while (bitboard) {
                source_square = get_ls1b_index(bitboard);
                attacks = knight_attacks[source_square] & ((td.side == white) ? ~td.occupancies[white] : ~td.occupancies[black]);
                while (attacks) {
                    target_square = get_ls1b_index(attacks);
                    if (!get_bit(((td.side == white) ? td.occupancies[black] : td.occupancies[white]), target_square))
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    else
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    pop_bit(attacks, target_square);
                }
                pop_bit(bitboard, source_square);
            }
        }

        if ((td.side == white) ? piece == B : piece == b) {
            while (bitboard) {
                source_square = get_ls1b_index(bitboard);
                attacks = get_bishop_attacks(source_square, td.occupancies[both]) & ((td.side == white) ? ~td.occupancies[white] : ~td.occupancies[black]);
                while (attacks) {
                    target_square = get_ls1b_index(attacks);
                    if (!get_bit(((td.side == white) ? td.occupancies[black] : td.occupancies[white]), target_square))
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    else
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    pop_bit(attacks, target_square);
                }
                pop_bit(bitboard, source_square);
            }
        }

        if ((td.side == white) ? piece == R : piece == r) {
            while (bitboard) {
                source_square = get_ls1b_index(bitboard);
                attacks = get_rook_attacks(source_square, td.occupancies[both]) & ((td.side == white) ? ~td.occupancies[white] : ~td.occupancies[black]);
                while (attacks) {
                    target_square = get_ls1b_index(attacks);
                    if (!get_bit(((td.side == white) ? td.occupancies[black] : td.occupancies[white]), target_square))
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    else
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    pop_bit(attacks, target_square);
                }
                pop_bit(bitboard, source_square);
            }
        }

        if ((td.side == white) ? piece == Q : piece == q) {
            while (bitboard) {
                source_square = get_ls1b_index(bitboard);
                attacks = get_queen_attacks(source_square, td.occupancies[both]) & ((td.side == white) ? ~td.occupancies[white] : ~td.occupancies[black]);
                while (attacks) {
                    target_square = get_ls1b_index(attacks);
                    if (!get_bit(((td.side == white) ? td.occupancies[black] : td.occupancies[white]), target_square))
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    else
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    pop_bit(attacks, target_square);
                }
                pop_bit(bitboard, source_square);
            }
        }

        if ((td.side == white) ? piece == K : piece == k) {
            while (bitboard) {
                source_square = get_ls1b_index(bitboard);
                attacks = king_attacks[source_square] & ((td.side == white) ? ~td.occupancies[white] : ~td.occupancies[black]);
                while (attacks) {
                    target_square = get_ls1b_index(attacks);
                    if (!get_bit(((td.side == white) ? td.occupancies[black] : td.occupancies[white]), target_square))
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    else
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    pop_bit(attacks, target_square);
                }
                pop_bit(bitboard, source_square);
            }
        }
    }
}

static inline int td_make_move(ThreadData& td, int move, int move_flag) {
    if (move_flag == all_moves) {
        U64 bitboards_copy[12], occupancies_copy[3];
        int side_copy, enpassant_copy, castle_copy, fifty_copy;
        memcpy(bitboards_copy, td.bitboards, 96);
        memcpy(occupancies_copy, td.occupancies, 24);
        side_copy = td.side; enpassant_copy = td.enpassant;
        castle_copy = td.castle; fifty_copy = td.fifty;
        U64 hash_key_copy = td.hash_key;

        int source_square = get_move_source(move);
        int target_square = get_move_target(move);
        int piece = get_move_piece(move);
        int promoted_piece = get_move_promoted(move);
        int capture = get_move_capture(move);
        int double_push = get_move_double(move);
        int enpass = get_move_enpassant(move);
        int castling = get_move_castling(move);

        pop_bit(td.bitboards[piece], source_square);
        set_bit(td.bitboards[piece], target_square);
        td.hash_key ^= piece_keys[piece][source_square];
        td.hash_key ^= piece_keys[piece][target_square];
        td.fifty++;

        if (piece == P || piece == p) td.fifty = 0;

        if (capture) {
            td.fifty = 0;
            int start_piece, end_piece;
            if (td.side == white) { start_piece = p; end_piece = k; }
            else { start_piece = P; end_piece = K; }
            for (int bb_piece = start_piece; bb_piece <= end_piece; bb_piece++) {
                if (get_bit(td.bitboards[bb_piece], target_square)) {
                    pop_bit(td.bitboards[bb_piece], target_square);
                    td.hash_key ^= piece_keys[bb_piece][target_square];
                    break;
                }
            }
        }

        if (promoted_piece) {
            if (td.side == white) {
                pop_bit(td.bitboards[P], target_square);
                td.hash_key ^= piece_keys[P][target_square];
            }
            else {
                pop_bit(td.bitboards[p], target_square);
                td.hash_key ^= piece_keys[p][target_square];
            }
            set_bit(td.bitboards[promoted_piece], target_square);
            td.hash_key ^= piece_keys[promoted_piece][target_square];
        }

        if (enpass) {
            if (td.side == white) {
                pop_bit(td.bitboards[p], target_square + 8);
                td.hash_key ^= piece_keys[p][target_square + 8];
            }
            else {
                pop_bit(td.bitboards[P], target_square - 8);
                td.hash_key ^= piece_keys[P][target_square - 8];
            }
        }

        if (td.enpassant != no_sq) td.hash_key ^= enpassant_keys[td.enpassant];
        td.enpassant = no_sq;

        if (double_push) {
            if (td.side == white) {
                td.enpassant = target_square + 8;
                td.hash_key ^= enpassant_keys[target_square + 8];
            }
            else {
                td.enpassant = target_square - 8;
                td.hash_key ^= enpassant_keys[target_square - 8];
            }
        }

        if (castling) {
            switch (target_square) {
            case (g1):
                pop_bit(td.bitboards[R], h1); set_bit(td.bitboards[R], f1);
                td.hash_key ^= piece_keys[R][h1]; td.hash_key ^= piece_keys[R][f1];
                break;
            case (c1):
                pop_bit(td.bitboards[R], a1); set_bit(td.bitboards[R], d1);
                td.hash_key ^= piece_keys[R][a1]; td.hash_key ^= piece_keys[R][d1];
                break;
            case (g8):
                pop_bit(td.bitboards[r], h8); set_bit(td.bitboards[r], f8);
                td.hash_key ^= piece_keys[r][h8]; td.hash_key ^= piece_keys[r][f8];
                break;
            case (c8):
                pop_bit(td.bitboards[r], a8); set_bit(td.bitboards[r], d8);
                td.hash_key ^= piece_keys[r][a8]; td.hash_key ^= piece_keys[r][d8];
                break;
            }
        }

        td.hash_key ^= castle_keys[td.castle];
        td.castle &= castling_rights[source_square];
        td.castle &= castling_rights[target_square];
        td.hash_key ^= castle_keys[td.castle];

        memset(td.occupancies, 0ULL, 24);
        for (int bb_piece = P; bb_piece <= K; bb_piece++)
            td.occupancies[white] |= td.bitboards[bb_piece];
        for (int bb_piece = p; bb_piece <= k; bb_piece++)
            td.occupancies[black] |= td.bitboards[bb_piece];
        td.occupancies[both] = td.occupancies[white] | td.occupancies[black];

        td.side ^= 1;
        td.hash_key ^= side_key;

        if (td_is_square_attacked(td, (td.side == white) ? get_ls1b_index(td.bitboards[k]) : get_ls1b_index(td.bitboards[K]), td.side)) {
            memcpy(td.bitboards, bitboards_copy, 96);
            memcpy(td.occupancies, occupancies_copy, 24);
            td.side = side_copy; td.enpassant = enpassant_copy;
            td.castle = castle_copy; td.fifty = fifty_copy;
            td.hash_key = hash_key_copy;
            return 0;
        }
        return 1;
    }
    else {
        if (get_move_capture(move))
            return td_make_move(td, move, all_moves);
        else
            return 0;
    }
}

static inline int td_evaluate(ThreadData& td) {
    U64 bitboard;
    int piece, square;
    int pieces[33];
    int squares[33];
    int index = 2;

    for (int bb_piece = P; bb_piece <= k; bb_piece++) {
        bitboard = td.bitboards[bb_piece];
        while (bitboard) {
            piece = bb_piece;
            square = get_ls1b_index(bitboard);
            if (piece == K) {
                pieces[0] = nnue_pieces[piece];
                squares[0] = nnue_squares[square];
            }
            else if (piece == k) {
                pieces[1] = nnue_pieces[piece];
                squares[1] = nnue_squares[square];
            }
            else {
                pieces[index] = nnue_pieces[piece];
                squares[index] = nnue_squares[square];
                index++;
            }
            pop_bit(bitboard, square);
        }
    }
    pieces[index] = 0;
    squares[index] = 0;
    return (evaluate_nnue(td.side, pieces, squares) * (100 - td.fifty) / 100);
}

static inline int td_is_repetition(ThreadData& td) {
    for (int index = 0; index < td.repetition_index; index++)
        if (td.repetition_table[index] == td.hash_key)
            return 1;
    return 0;
}

// FIXED: Hash table now enabled for multi-threaded search
static inline int td_read_hash_entry(ThreadData& td, int alpha, int beta, int* best_move, int depth) {
    tt* hash_entry = &hash_table[td.hash_key % hash_entries];

    if (hash_entry->hash_key == td.hash_key) {
        if (hash_entry->depth >= depth) {
            int score = hash_entry->score;
            if (score < -mate_score) score += td.ply;
            if (score > mate_score) score -= td.ply;
            if (hash_entry->flag == hash_flag_exact) return score;
            if ((hash_entry->flag == hash_flag_alpha) && (score <= alpha)) return alpha;
            if ((hash_entry->flag == hash_flag_beta) && (score >= beta)) return beta;
        }
        *best_move = hash_entry->best_move;
    }
    return no_hash_entry;
}

// FIXED: Hash table now enabled for multi-threaded search
static inline void td_write_hash_entry(ThreadData& td, int score, int best_move, int depth, int hash_flag) {
    tt* hash_entry = &hash_table[td.hash_key % hash_entries];
    if (score < -mate_score) score -= td.ply;
    if (score > mate_score) score += td.ply;
    hash_entry->hash_key = td.hash_key;
    hash_entry->score = score;
    hash_entry->flag = hash_flag;
    hash_entry->depth = depth;
    hash_entry->best_move = best_move;
}

static inline int td_score_move(ThreadData& td, int move) {
    if (get_move_capture(move)) {
        // Use SEE for capture ordering
        int see_value = td_see(td, move);
        
        if (see_value >= 0) {
            // Good captures: MVV-LVA + high base
            int piece = get_move_piece(move);
            int target_piece = P;
            int start_piece, end_piece;
            if (td.side == white) { start_piece = p; end_piece = k; }
            else { start_piece = P; end_piece = K; }
            for (int bb_piece = start_piece; bb_piece <= end_piece; bb_piece++) {
                if (get_bit(td.bitboards[bb_piece], get_move_target(move))) {
                    target_piece = bb_piece;
                    break;
                }
            }
            return mvv_lva[piece][target_piece] + 10000;
        } else {
            // Bad captures: still above quiet moves but below good captures
            return 5000 + see_value;
        }
    }
    else {
        if (td.killer_moves[0][td.ply] == move) return 9000;
        else if (td.killer_moves[1][td.ply] == move) return 8000;
        else return td.history_moves[get_move_piece(move)][get_move_target(move)];
    }
    return 0;
}

static inline void td_sort_moves(ThreadData& td, moves* move_list, int best_move) {
    std::vector<int> move_scores(move_list->count);
    for (int count = 0; count < move_list->count; count++) {
        if (best_move == move_list->moves[count])
            move_scores[count] = 30000;
        else
            move_scores[count] = td_score_move(td, move_list->moves[count]);
    }
    for (int current_move = 0; current_move < move_list->count; current_move++) {
        for (int next_move = current_move + 1; next_move < move_list->count; next_move++) {
            if (move_scores[current_move] < move_scores[next_move]) {
                std::swap(move_scores[current_move], move_scores[next_move]);
                std::swap(move_list->moves[current_move], move_list->moves[next_move]);
            }
        }
    }
}

static int td_quiescence(ThreadData& td, int alpha, int beta) {
    if ((td.nodes & 2047) == 0) {
        if (stop_threads.load(std::memory_order_relaxed)) return 0;
        if (timeset == 1 && get_time_ms() > stoptime) {
            stop_threads.store(true, std::memory_order_relaxed);
            return 0;
        }
    }
    td.nodes++;
    if (td.ply > max_ply - 1) return td_evaluate(td);

    int evaluation = td_evaluate(td);
    if (evaluation >= beta) return beta;
    
    // Delta pruning - very safe
    if (evaluation + 975 < alpha) return alpha;
    
    if (evaluation > alpha) alpha = evaluation;

    moves move_list[1];
    td_generate_moves(td, move_list);
    td_sort_moves(td, move_list, 0);

    for (int count = 0; count < move_list->count; count++) {
        int move = move_list->moves[count];
        
        // Only prune VERY bad captures (like Q takes defended pawn)
        if (get_move_capture(move) && td_see(td, move) < -200)
            continue;
        
        U64 bb_copy[12], occ_copy[3];
        int side_c, ep_c, castle_c, fifty_c;
        U64 hash_c;
        memcpy(bb_copy, td.bitboards, 96);
        memcpy(occ_copy, td.occupancies, 24);
        side_c = td.side; ep_c = td.enpassant; castle_c = td.castle;
        fifty_c = td.fifty; hash_c = td.hash_key;

        td.ply++;
        td.repetition_index++;
        td.repetition_table[td.repetition_index] = td.hash_key;

        if (td_make_move(td, move, only_captures) == 0) {
            td.ply--;
            td.repetition_index--;
            continue;
        }

        int score = -td_quiescence(td, -beta, -alpha);

        td.ply--;
        td.repetition_index--;
        memcpy(td.bitboards, bb_copy, 96);
        memcpy(td.occupancies, occ_copy, 24);
        td.side = side_c; td.enpassant = ep_c; td.castle = castle_c;
        td.fifty = fifty_c; td.hash_key = hash_c;

        if (stop_threads.load(std::memory_order_relaxed)) return 0;

        if (score > alpha) {
            alpha = score;
            if (score >= beta) return beta;
        }
    }
    return alpha;
}

int td_negamax(ThreadData& td, int alpha, int beta, int depth) {
    td.pv_length[td.ply] = td.ply;
    int score;
    int best_move = 0;
    int hash_flag = hash_flag_alpha;

    if (td.ply && (td_is_repetition(td) || td.fifty >= 100)) return 0;

    int pv_node = beta - alpha > 1;

    // Use hash table
    if (td.ply && (score = td_read_hash_entry(td, alpha, beta, &best_move, depth)) != no_hash_entry && pv_node == 0)
        return score;

    if ((td.nodes & 2047) == 0) {
        if (stop_threads.load(std::memory_order_relaxed)) return 0;
        if (timeset == 1 && get_time_ms() > stoptime) {
            stop_threads.store(true, std::memory_order_relaxed);
            return 0;
        }
    }

    if (depth == 0) return td_quiescence(td, alpha, beta);
    if (td.ply > max_ply - 1) return td_evaluate(td);

    td.nodes++;

    int in_check = td_is_square_attacked(td, (td.side == white) ? get_ls1b_index(td.bitboards[K]) : get_ls1b_index(td.bitboards[k]), td.side ^ 1);
    if (in_check) depth++;

    int legal_moves = 0;
    int static_eval = td_evaluate(td);

    // Evaluation pruning
    if (depth < 3 && !pv_node && !in_check && abs(beta - 1) > -infinity + 100) {
        int eval_margin = 120 * depth;
        if (static_eval - eval_margin >= beta)
            return static_eval - eval_margin;
    }

    // Null move pruning
    if (depth >= 3 && in_check == 0 && td.ply) {
        U64 bb_copy[12], occ_copy[3];
        int side_c, ep_c, castle_c, fifty_c;
        U64 hash_c;
        memcpy(bb_copy, td.bitboards, 96);
        memcpy(occ_copy, td.occupancies, 24);
        side_c = td.side; ep_c = td.enpassant; castle_c = td.castle;
        fifty_c = td.fifty; hash_c = td.hash_key;

        td.ply++;
        td.repetition_index++;
        td.repetition_table[td.repetition_index] = td.hash_key;
        if (td.enpassant != no_sq) td.hash_key ^= enpassant_keys[td.enpassant];
        td.enpassant = no_sq;
        td.side ^= 1;
        td.hash_key ^= side_key;

        score = -td_negamax(td, -beta, -beta + 1, depth - 1 - 2);

        td.ply--;
        td.repetition_index--;
        memcpy(td.bitboards, bb_copy, 96);
        memcpy(td.occupancies, occ_copy, 24);
        td.side = side_c; td.enpassant = ep_c; td.castle = castle_c;
        td.fifty = fifty_c; td.hash_key = hash_c;

        if (stop_threads.load(std::memory_order_relaxed)) return 0;
        if (score >= beta) return beta;
    }

    // Razoring
    if (!pv_node && !in_check && depth <= 3) {
        score = static_eval + 125;
        int new_score;
        if (score < beta) {
            if (depth == 1) {
                new_score = td_quiescence(td, alpha, beta);
                return (new_score > score) ? new_score : score;
            }
            score += 175;
            if (score < beta && depth <= 2) {
                new_score = td_quiescence(td, alpha, beta);
                if (new_score < beta)
                    return (new_score > score) ? new_score : score;
            }
        }
    }

    moves move_list[1];
    td_generate_moves(td, move_list);
    td_sort_moves(td, move_list, best_move);

    int moves_searched = 0;

    for (int count = 0; count < move_list->count; count++) {
        U64 bb_copy[12], occ_copy[3];
        int side_c, ep_c, castle_c, fifty_c;
        U64 hash_c;
        memcpy(bb_copy, td.bitboards, 96);
        memcpy(occ_copy, td.occupancies, 24);
        side_c = td.side; ep_c = td.enpassant; castle_c = td.castle;
        fifty_c = td.fifty; hash_c = td.hash_key;

        td.ply++;
        td.repetition_index++;
        td.repetition_table[td.repetition_index] = td.hash_key;

        if (td_make_move(td, move_list->moves[count], all_moves) == 0) {
            td.ply--;
            td.repetition_index--;
            continue;
        }

        legal_moves++;

        if (moves_searched == 0)
            score = -td_negamax(td, -beta, -alpha, depth - 1);
        else {
            // LMR
            if (moves_searched >= 4 && depth >= 3 && in_check == 0 &&
                get_move_capture(move_list->moves[count]) == 0 &&
                get_move_promoted(move_list->moves[count]) == 0)
                score = -td_negamax(td, -alpha - 1, -alpha, depth - 2);
            else
                score = alpha + 1;

            // PVS
            if (score > alpha) {
                score = -td_negamax(td, -alpha - 1, -alpha, depth - 1);
                if ((score > alpha) && (score < beta))
                    score = -td_negamax(td, -beta, -alpha, depth - 1);
            }
        }

        td.ply--;
        td.repetition_index--;
        memcpy(td.bitboards, bb_copy, 96);
        memcpy(td.occupancies, occ_copy, 24);
        td.side = side_c; td.enpassant = ep_c; td.castle = castle_c;
        td.fifty = fifty_c; td.hash_key = hash_c;

        if (stop_threads.load(std::memory_order_relaxed)) return 0;

        moves_searched++;

        if (score > alpha) {
            hash_flag = hash_flag_exact;
            best_move = move_list->moves[count];

            if (get_move_capture(move_list->moves[count]) == 0)
                td.history_moves[get_move_piece(move_list->moves[count])][get_move_target(move_list->moves[count])] += depth;

            alpha = score;

            td.pv_table[td.ply][td.ply] = move_list->moves[count];
            for (int next_ply = td.ply + 1; next_ply < td.pv_length[td.ply + 1]; next_ply++)
                td.pv_table[td.ply][next_ply] = td.pv_table[td.ply + 1][next_ply];
            td.pv_length[td.ply] = td.pv_length[td.ply + 1];

            if (score >= beta) {
                td_write_hash_entry(td, beta, best_move, depth, hash_flag_beta);
                if (get_move_capture(move_list->moves[count]) == 0) {
                    td.killer_moves[1][td.ply] = td.killer_moves[0][td.ply];
                    td.killer_moves[0][td.ply] = move_list->moves[count];
                }
                return beta;
            }
        }
    }

    if (legal_moves == 0) {
        if (in_check) return -mate_value + td.ply;
        else return 0;
    }

    td_write_hash_entry(td, alpha, best_move, depth, hash_flag);
    return alpha;
}

void thread_search(int thread_id, int depth) {
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

        int search_depth = current_depth;
        if (thread_id > 0) {
            search_depth = current_depth + (thread_id % 2);
            if (search_depth > depth) search_depth = depth;
        }

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

void start_search_threads(int depth) {
    stop_threads.store(false, std::memory_order_relaxed);
    total_nodes.store(0, std::memory_order_relaxed);
    search_threads.clear();

    for (int i = 1; i < num_threads; i++) {
        search_threads.emplace_back(thread_search, i, depth);
    }

    thread_search(0, depth);
}

void stop_search_threads() {
    stop_threads.store(true, std::memory_order_relaxed);
}

void wait_for_threads() {
    for (auto& t : search_threads) {
        if (t.joinable())
            t.join();
    }
    search_threads.clear();
}
