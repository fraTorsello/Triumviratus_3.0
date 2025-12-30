#include <vector>
#include "search.h"
#include "defs.h"
#include "tt.h"
#include "movegen.h"
#include "misc.h"
#include "evaluation.h"
#include "perft.h"
#include <algorithm>

const int full_depth_moves = 4;
const int reduction_limit = 3;

// enable PV move scoring
static inline void enable_pv_scoring(moves* move_list)
{
    follow_pv = 0;

    for (int count = 0; count < move_list->count; count++)
    {
        if (pv_table[0][ply] == move_list->moves[count])
        {
            score_pv = 1;
            follow_pv = 1;
        }
    }
}

// score moves
static inline int score_move(int move)
{
    if (score_pv)
    {
        if (pv_table[0][ply] == move)
        {
            score_pv = 0;
            return 20000;
        }
    }

    if (get_move_capture(move))
    {
        int piece = get_move_piece(move);
        int target_piece = P;
        int start_piece, end_piece;

        if (side == white) { start_piece = p; end_piece = k; }
        else { start_piece = P; end_piece = K; }

        for (int bb_piece = start_piece; bb_piece <= end_piece; bb_piece++)
        {
            if (get_bit(bitboards[bb_piece], get_move_target(move)))
            {
                target_piece = bb_piece;
                break;
            }
        }

        return mvv_lva[piece][target_piece] + 10000;
    }
    else
    {
        if (killer_moves[0][ply] == move)
            return 9000;
        else if (killer_moves[1][ply] == move)
            return 8000;
        else
            return history_moves[get_move_piece(move)][get_move_target(move)];
    }

    return 0;
}

// sort moves in descending order
static inline int sort_moves(moves* move_list, int best_move)
{
    std::vector<int> move_scores(move_list->count);

    for (int count = 0; count < move_list->count; count++)
    {
        if (best_move == move_list->moves[count])
            move_scores[count] = 30000;
        else
            move_scores[count] = score_move(move_list->moves[count]);
    }

    for (int current_move = 0; current_move < move_list->count; current_move++)
    {
        for (int next_move = current_move + 1; next_move < move_list->count; next_move++)
        {
            if (move_scores[current_move] < move_scores[next_move])
            {
                std::swap(move_scores[current_move], move_scores[next_move]);
                std::swap(move_list->moves[current_move], move_list->moves[next_move]);
            }
        }
    }
    return 0;
}

// position repetition detection
static inline int is_repetition()
{
    for (int index = 0; index < repetition_index; index++)
        if (repetition_table[index] == hash_key)
            return 1;
    return 0;
}

// quiescence search
static inline int quiescence(int alpha, int beta)
{
    if ((nodes & 2047) == 0)
        communicate();

    nodes++;

    if (ply > max_ply - 1)
        return evaluate();

    int evaluation = evaluate();

    if (evaluation >= beta)
        return beta;

    if (evaluation > alpha)
        alpha = evaluation;

    moves move_list[1];
    generate_moves(move_list);
    sort_moves(move_list, 0);

    for (int count = 0; count < move_list->count; count++)
    {
        copy_board();

        ply++;
        repetition_index++;
        repetition_table[repetition_index] = hash_key;

        if (make_move(move_list->moves[count], only_captures) == 0)
        {
            ply--;
            repetition_index--;
            continue;
        }

        int score = -quiescence(-beta, -alpha);

        ply--;
        repetition_index--;
        take_back();

        if (stopped == 1) return 0;

        if (score > alpha)
        {
            alpha = score;
            if (score >= beta)
                return beta;
        }
    }

    return alpha;
}

// negamax alpha beta search
static inline int negamax(int alpha, int beta, int depth)
{
    pv_length[ply] = ply;

    int score;
    int best_move = 0;
    int hash_flag = hash_flag_alpha;

    if (ply && is_repetition() || fifty >= 100)
        return 0;

    int pv_node = beta - alpha > 1;

    if (ply && (score = read_hash_entry(alpha, beta, &best_move, depth)) != no_hash_entry && pv_node == 0)
        return score;

    if ((nodes & 2047) == 0)
        communicate();

    if (depth == 0)
        return quiescence(alpha, beta);

    if (ply > max_ply - 1)
        return evaluate();

    nodes++;

    int in_check = is_square_attacked((side == white) ? get_ls1b_index(bitboards[K]) :
        get_ls1b_index(bitboards[k]), side ^ 1);

    if (in_check) depth++;

    int legal_moves = 0;
    int static_eval = evaluate();

    // evaluation pruning
    if (depth < 3 && !pv_node && !in_check && abs(beta - 1) > -infinity + 100)
    {
        int eval_margin = 120 * depth;
        if (static_eval - eval_margin >= beta)
            return static_eval - eval_margin;
    }

    // null move pruning
    if (depth >= 3 && in_check == 0 && ply)
    {
        copy_board();

        ply++;
        repetition_index++;
        repetition_table[repetition_index] = hash_key;

        if (enpassant != no_sq) hash_key ^= enpassant_keys[enpassant];
        enpassant = no_sq;
        side ^= 1;
        hash_key ^= side_key;

        score = -negamax(-beta, -beta + 1, depth - 1 - 2);

        ply--;
        repetition_index--;
        take_back();

        if (stopped == 1) return 0;

        if (score >= beta)
            return beta;
    }

    // razoring
    if (!pv_node && !in_check && depth <= 3)
    {
        score = static_eval + 125;
        int new_score;

        if (score < beta)
        {
            if (depth == 1)
            {
                new_score = quiescence(alpha, beta);
                return (new_score > score) ? new_score : score;
            }

            score += 175;

            if (score < beta && depth <= 2)
            {
                new_score = quiescence(alpha, beta);
                if (new_score < beta)
                    return (new_score > score) ? new_score : score;
            }
        }
    }

    moves move_list[1];
    generate_moves(move_list);

    if (follow_pv)
        enable_pv_scoring(move_list);

    sort_moves(move_list, best_move);

    int moves_searched = 0;

    for (int count = 0; count < move_list->count; count++)
    {
        copy_board();

        ply++;
        repetition_index++;
        repetition_table[repetition_index] = hash_key;

        if (make_move(move_list->moves[count], all_moves) == 0)
        {
            ply--;
            repetition_index--;
            continue;
        }

        legal_moves++;

        if (moves_searched == 0)
            score = -negamax(-beta, -alpha, depth - 1);
        else
        {
            // LMR
            if (moves_searched >= full_depth_moves &&
                depth >= reduction_limit &&
                in_check == 0 &&
                get_move_capture(move_list->moves[count]) == 0 &&
                get_move_promoted(move_list->moves[count]) == 0)
                score = -negamax(-alpha - 1, -alpha, depth - 2);
            else
                score = alpha + 1;

            // PVS
            if (score > alpha)
            {
                score = -negamax(-alpha - 1, -alpha, depth - 1);
                if ((score > alpha) && (score < beta))
                    score = -negamax(-beta, -alpha, depth - 1);
            }
        }

        ply--;
        repetition_index--;
        take_back();

        if (stopped == 1) return 0;

        moves_searched++;

        if (score > alpha)
        {
            hash_flag = hash_flag_exact;
            best_move = move_list->moves[count];

            if (get_move_capture(move_list->moves[count]) == 0)
                history_moves[get_move_piece(move_list->moves[count])][get_move_target(move_list->moves[count])] += depth;

            alpha = score;

            pv_table[ply][ply] = move_list->moves[count];

            for (int next_ply = ply + 1; next_ply < pv_length[ply + 1]; next_ply++)
                pv_table[ply][next_ply] = pv_table[ply + 1][next_ply];

            pv_length[ply] = pv_length[ply + 1];

            if (score >= beta)
            {
                write_hash_entry(beta, best_move, depth, hash_flag_beta);

                if (get_move_capture(move_list->moves[count]) == 0)
                {
                    killer_moves[1][ply] = killer_moves[0][ply];
                    killer_moves[0][ply] = move_list->moves[count];
                }

                return beta;
            }
        }
    }

    if (legal_moves == 0)
    {
        if (in_check)
            return -mate_value + ply;
        else
            return 0;
    }

    write_hash_entry(alpha, best_move, depth, hash_flag);

    return alpha;
}

// search position for the best move
void search_position(int depth)
{
    int start = get_time_ms();
    int score = 0;
    nodes = 0;
    stopped = 0;
    follow_pv = 0;
    score_pv = 0;

    memset(killer_moves, 0, sizeof(killer_moves));
    memset(history_moves, 0, sizeof(history_moves));
    memset(pv_table, 0, sizeof(pv_table));
    memset(pv_length, 0, sizeof(pv_length));

    int alpha = -infinity;
    int beta = infinity;

    for (int current_depth = 1; current_depth <= depth; current_depth++)
    {
        if (stopped == 1)
            break;

        follow_pv = 1;

        score = negamax(alpha, beta, current_depth);

        if ((score <= alpha) || (score >= beta)) {
            alpha = -infinity;
            beta = infinity;
            continue;
        }

        alpha = score - 50;
        beta = score + 50;

        if (pv_length[0])
        {
            if (score > -mate_value && score < -mate_score)
                printf("info score mate %d depth %d nodes %lld time %d pv ", -(score + mate_value) / 2 - 1, current_depth, nodes, get_time_ms() - start);
            else if (score > mate_score && score < mate_value)
                printf("info score mate %d depth %d nodes %lld time %d pv ", (mate_value - score) / 2 + 1, current_depth, nodes, get_time_ms() - start);
            else
                printf("info score cp %d depth %d nodes %lld time %d pv ", score, current_depth, nodes, get_time_ms() - start);

            for (int count = 0; count < pv_length[0]; count++)
            {
                print_move(pv_table[0][count]);
                printf(" ");
            }

            printf("\n");
        }
    }

    printf("bestmove ");

    if (pv_table[0][0])
        print_move(pv_table[0][0]);
    else
        printf("(none)");

    printf("\n");
}
