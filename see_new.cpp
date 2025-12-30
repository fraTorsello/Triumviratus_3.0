/*
 * Static Exchange Evaluation (SEE)
 * Determines if a capture wins or loses material
 */

#include "see.h"
#include "defs.h"
#include "movegen.h"
#include "magic.h"
#include "attacks.h"

// Piece values for SEE
const int see_piece_values[12] = {
    100, 320, 330, 500, 900, 20000,  // P N B R Q K
    100, 320, 330, 500, 900, 20000   // p n b r q k
};

// Get the least valuable attacker of a square
static inline int get_smallest_attacker(U64 bb[12], U64 occ[3], int square, int side, int* from_sq) {
    U64 attackers;
    
    // Pawns (least valuable)
    if (side == white) {
        attackers = pawn_attacks[black][square] & bb[P];
    } else {
        attackers = pawn_attacks[white][square] & bb[p];
    }
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return (side == white) ? P : p;
    }
    
    // Knights
    int knight = (side == white) ? N : n;
    attackers = knight_attacks[square] & bb[knight];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return knight;
    }
    
    // Bishops
    int bishop = (side == white) ? B : b;
    attackers = get_bishop_attacks(square, occ[both]) & bb[bishop];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return bishop;
    }
    
    // Rooks
    int rook_piece = (side == white) ? R : r;
    attackers = get_rook_attacks(square, occ[both]) & bb[rook_piece];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return rook_piece;
    }
    
    // Queens
    int queen = (side == white) ? Q : q;
    attackers = get_queen_attacks(square, occ[both]) & bb[queen];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return queen;
    }
    
    // King
    int king = (side == white) ? K : k;
    attackers = king_attacks[square] & bb[king];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return king;
    }
    
    return -1; // No attacker
}

// Static Exchange Evaluation
int td_see(ThreadData& td, int move) {
    int from = get_move_source(move);
    int to = get_move_target(move);
    int piece = get_move_piece(move);
    int captured = -1;
    
    // Find the captured piece
    int start = (td.side == white) ? p : P;
    int end = (td.side == white) ? k : K;
    
    for (int pc = start; pc <= end; pc++) {
        if (get_bit(td.bitboards[pc], to)) {
            captured = pc;
            break;
        }
    }
    
    // Handle en passant
    if (get_move_enpassant(move)) {
        captured = (td.side == white) ? p : P;
    }
    
    // Not a capture
    if (captured == -1) return 0;
    
    // Make local copies for simulation
    U64 bb[12], occ[3];
    memcpy(bb, td.bitboards, sizeof(td.bitboards));
    memcpy(occ, td.occupancies, sizeof(td.occupancies));
    
    int gain[32];
    int d = 0;
    int current_side = td.side;
    
    // Initial material gain
    gain[0] = see_piece_values[captured];
    
    // Remove attacker from source square
    pop_bit(bb[piece], from);
    pop_bit(occ[current_side], from);
    pop_bit(occ[both], from);
    
    // Remove captured piece from target square
    pop_bit(bb[captured], to);
    pop_bit(occ[current_side ^ 1], to);
    
    // Place attacker on target square
    set_bit(bb[piece], to);
    set_bit(occ[current_side], to);
    set_bit(occ[both], to);
    
    int attacker_value = see_piece_values[piece];
    current_side ^= 1;
    
    // Simulate the exchange
    while (d < 31) {
        d++;
        
        int from_sq;
        int attacker = get_smallest_attacker(bb, occ, to, current_side, &from_sq);
        
        // No more attackers
        if (attacker == -1) break;
        
        // Calculate gain for this capture
        gain[d] = attacker_value - gain[d - 1];
        
        // Stand-pat: if we're already winning, we can stop
        if (-gain[d - 1] < 0 && gain[d] < 0) break;
        
        attacker_value = see_piece_values[attacker];
        
        // Remove this attacker from its square
        pop_bit(bb[attacker], from_sq);
        pop_bit(occ[current_side], from_sq);
        pop_bit(occ[both], from_sq);
        
        // Remove the current piece on target (it gets captured)
        for (int pc = P; pc <= k; pc++) {
            if (get_bit(bb[pc], to)) {
                pop_bit(bb[pc], to);
                break;
            }
        }
        
        // Place attacker on target
        set_bit(bb[attacker], to);
        set_bit(occ[current_side], to);
        set_bit(occ[both], to);
        
        current_side ^= 1;
    }
    
    // Negamax the gain array
    while (--d > 0) {
        gain[d - 1] = -((-gain[d - 1] > gain[d]) ? -gain[d - 1] : gain[d]);
    }
    
    return gain[0];
}
