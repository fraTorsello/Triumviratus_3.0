#include "defs.h"
#include "movegen.h"
#include "evaluation.h"
#include "nnue_eval.h"

// position evaluation
int evaluate()
{
    U64 bitboard;
    int piece, square;
    int pieces[33];
    int squares[33];
    int index = 2;

    for (int bb_piece = P; bb_piece <= k; bb_piece++)
    {
        bitboard = bitboards[bb_piece];

        while (bitboard)
        {
            piece = bb_piece;
            square = get_ls1b_index(bitboard);

            if (piece == K)
            {
                pieces[0] = nnue_pieces[piece];
                squares[0] = nnue_squares[square];
            }
            else if (piece == k)
            {
                pieces[1] = nnue_pieces[piece];
                squares[1] = nnue_squares[square];
            }
            else
            {
                pieces[index] = nnue_pieces[piece];
                squares[index] = nnue_squares[square];
                index++;
            }

            pop_bit(bitboard, square);
        }
    }

    pieces[index] = 0;
    squares[index] = 0;

    return (evaluate_nnue(side, pieces, squares) * (100 - fifty) / 100);
}
