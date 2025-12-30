#pragma once
#ifndef SEE_NEW_H
#define SEE_NEW_H

#include "defs.h"
#include "threads_new.h"

// Piece values for SEE
extern const int see_piece_values[12];

// Static Exchange Evaluation for thread-local state
int td_see(ThreadData& td, int move);

#endif
