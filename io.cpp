#include "defs.h"
#include <cstdio>

// print bitboard
void print_bitboard(U64 bitboard)
{
    printf("\n");

    for (int rank = 0; rank < 8; rank++)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;
            if (!file)
                printf("  %d ", 8 - rank);
            printf(" %d", get_bit(bitboard, square) ? 1 : 0);
        }
        printf("\n");
    }

    printf("\n     a b c d e f g h\n\n");
    printf("     Bitboard: %llud\n\n", bitboard);
}

// print board
void print_board()
{
    printf("\n");

    for (int rank = 0; rank < 8; rank++)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;
            if (!file)
                printf("  %d ", 8 - rank);

            int piece = -1;

            for (int bb_piece = P; bb_piece <= k; bb_piece++)
            {
                if (get_bit(bitboards[bb_piece], square))
                    piece = bb_piece;
            }

            printf(" %c", (piece == -1) ? '.' : ascii_pieces[piece]);
        }
        printf("\n");
    }

    printf("\n     a b c d e f g h\n\n");
    printf("     Side:     %s\n", !side ? "white" : "black");
    printf("     Enpassant:   %s\n", (enpassant != no_sq) ? square_to_coordinates[enpassant] : "no");
    printf("     Castling:  %c%c%c%c\n\n", (castle & wk) ? 'K' : '-',
        (castle & wq) ? 'Q' : '-',
        (castle & bk) ? 'k' : '-',
        (castle & bq) ? 'q' : '-');
    printf("     Hash key:  %llx\n", hash_key);
    printf("     Fifty move: %d\n\n", fifty);
}

// reset board variables
void reset_board()
{
    memset(bitboards, 0ULL, sizeof(bitboards));
    memset(occupancies, 0ULL, sizeof(occupancies));
    side = 0;
    enpassant = no_sq;
    castle = 0;
    repetition_index = 0;
    fifty = 0;
    memset(repetition_table, 0ULL, sizeof(repetition_table));
}

// parse FEN string
void parse_fen(const char* fen)
{
    reset_board();

    for (int rank = 0; rank < 8; rank++)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;

            if ((*fen >= 'a' && *fen <= 'z') || (*fen >= 'A' && *fen <= 'Z'))
            {
                int piece = mapCharToPiece(*fen);
                set_bit(bitboards[piece], square);
                fen++;
            }

            if (*fen >= '0' && *fen <= '9')
            {
                int offset = *fen - '0';
                int piece = -1;

                for (int bb_piece = P; bb_piece <= k; bb_piece++)
                {
                    if (get_bit(bitboards[bb_piece], square))
                        piece = bb_piece;
                }

                if (piece == -1)
                    file--;

                file += offset;
                fen++;
            }

            if (*fen == '/')
                fen++;
        }
    }

    fen++;
    (*fen == 'w') ? (side = white) : (side = black);
    fen += 2;

    while (*fen != ' ')
    {
        switch (*fen)
        {
        case 'K': castle |= wk; break;
        case 'Q': castle |= wq; break;
        case 'k': castle |= bk; break;
        case 'q': castle |= bq; break;
        case '-': break;
        }
        fen++;
    }

    fen++;

    if (*fen != '-')
    {
        int file = fen[0] - 'a';
        int rank = 8 - (fen[1] - '0');
        enpassant = rank * 8 + file;
    }
    else
        enpassant = no_sq;

    for (int piece = P; piece <= K; piece++)
        occupancies[white] |= bitboards[piece];

    for (int piece = p; piece <= k; piece++)
        occupancies[black] |= bitboards[piece];

    occupancies[both] |= occupancies[white];
    occupancies[both] |= occupancies[black];

    hash_key = generate_hash_key();
}
