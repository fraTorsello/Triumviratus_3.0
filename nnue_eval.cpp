/*
 * NNUE Evaluation Wrapper
 * Silent initialization for UCI compliance
 */

#include "nnue.h"
#include "nnue_eval.h"

// init NNUE - silent for UCI
void init_nnue(const char* filename)
{
    nnue_init(filename);
}

// get NNUE score directly
int evaluate_nnue(int player, int* pieces, int* squares)
{
    return nnue_evaluate(player, pieces, squares);
}

// get NNUE score from FEN input
int evaluate_fen_nnue(char* fen)
{
    return nnue_evaluate_fen(fen);
}
