#pragma once
#ifndef SEARCH_H
#define SEARCH_H

#include "defs.h"
#include "movegen.h"

// Score bounds for mating scores
#define infinity 50000
#define mate_value 49000
#define mate_score 48000

// max ply that we can reach within a search
#define max_ply 64

// MVV LVA [attacker][victim]
extern int mvv_lva[12][12];

// killer moves [id][ply]
extern int killer_moves[2][max_ply];

// history moves [piece][square]
extern int history_moves[12][64];

// PV length [ply]
extern int pv_length[max_ply];

// PV table [ply][ply]
extern int pv_table[max_ply][max_ply];

// follow PV & score PV move
extern int follow_pv, score_pv;

// Single-threaded search
extern void search_position(int depth);

#endif
