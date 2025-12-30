/*
 * UCI Protocol Implementation for Triumviratus Chess Engine
 * Fully compliant with UCI specification
 * https://www.shredderchess.com/chess-features/uci-universal-chess-interface.html
 */

#include "defs.h"
#include "uci.h"
#include "movegen.h"
#include "search.h"
#include "tt.h"
#include "misc.h"
#include "io.h"
#include "threads.h"
#include <thread>
#include <string.h>

// parse user/GUI move string input (e.g. "e7e8q")
int parse_move(char* move_string)
{
    moves move_list[1];
    generate_moves(move_list);

    int source_square = (move_string[0] - 'a') + (8 - (move_string[1] - '0')) * 8;
    int target_square = (move_string[2] - 'a') + (8 - (move_string[3] - '0')) * 8;

    for (int move_count = 0; move_count < move_list->count; move_count++)
    {
        int move = move_list->moves[move_count];

        if (source_square == get_move_source(move) && target_square == get_move_target(move))
        {
            int promoted_piece = get_move_promoted(move);

            if (promoted_piece)
            {
                if ((promoted_piece == Q || promoted_piece == q) && move_string[4] == 'q')
                    return move;
                else if ((promoted_piece == R || promoted_piece == r) && move_string[4] == 'r')
                    return move;
                else if ((promoted_piece == B || promoted_piece == b) && move_string[4] == 'b')
                    return move;
                else if ((promoted_piece == N || promoted_piece == n) && move_string[4] == 'n')
                    return move;
                continue;
            }
            return move;
        }
    }
    return 0;
}

// parse UCI "position" command
void parse_position(char* command)
{
    command += 9;
    char* current_char = command;

    if (strncmp(command, "startpos", 8) == 0)
        parse_fen(start_position);
    else
    {
        current_char = strstr(command, "fen");
        if (current_char == NULL)
            parse_fen(start_position);
        else
        {
            current_char += 4;
            parse_fen(current_char);
        }
    }

    current_char = strstr(command, "moves");

    if (current_char != NULL)
    {
        current_char += 6;

        while (*current_char)
        {
            int move = parse_move(current_char);

            if (move == 0)
                break;

            repetition_index++;
            repetition_table[repetition_index] = hash_key;

            make_move(move, all_moves);

            while (*current_char && *current_char != ' ') current_char++;
            current_char++;
        }
    }
}

// reset time control variables
void reset_time_control()
{
    quit = 0;
    movestogo = 30;
    movetime = -1;
    time_uci = -1;
    inc = 0;
    starttime = 0;
    stoptime = 0;
    timeset = 0;
    stopped = 0;
}

// parse UCI command "go"
void parse_go(char* command)
{
    reset_time_control();

    int depth = -1;
    char* argument = NULL;

    if ((argument = strstr(command, "infinite"))) {}

    if ((argument = strstr(command, "binc")) && side == black)
        inc = atoi(argument + 5);

    if ((argument = strstr(command, "winc")) && side == white)
        inc = atoi(argument + 5);

    if ((argument = strstr(command, "wtime")) && side == white)
        time_uci = atoi(argument + 6);

    if ((argument = strstr(command, "btime")) && side == black)
        time_uci = atoi(argument + 6);

    if ((argument = strstr(command, "movestogo")))
        movestogo = atoi(argument + 10);

    if ((argument = strstr(command, "movetime")))
        movetime = atoi(argument + 9);

    if ((argument = strstr(command, "depth")))
        depth = atoi(argument + 6);

    if (movetime != -1)
    {
        time_uci = movetime;
        movestogo = 1;
    }

    starttime = get_time_ms();

    if (time_uci != -1)
    {
        timeset = 1;
        time_uci /= movestogo;
        time_uci -= 50;

        if (time_uci < 0)
        {
            time_uci = 0;
            inc -= 50;
            if (inc < 0) inc = 1;
        }

        stoptime = starttime + time_uci + inc;
    }

    if (depth == -1)
        depth = 64;

    // Start search
    search_position_mt(depth);
}

// main UCI loop - fully compliant with UCI protocol
void uci_loop()
{
    // Input buffer
    static char input[10000];
    
    // Engine settings
    int max_hash = 1024;
    int mb = 64;
    
    // Detect available threads
    int max_threads = std::thread::hardware_concurrency();
    if (max_threads < 1) max_threads = 1;
    if (max_threads > MAX_THREADS) max_threads = MAX_THREADS;
    
    // Initialize with 1 thread (silent)
    num_threads = 1;
    thread_data.resize(num_threads);
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        memset(thread_data[i].killer_moves, 0, sizeof(thread_data[i].killer_moves));
        memset(thread_data[i].history_moves, 0, sizeof(thread_data[i].history_moves));
        memset(thread_data[i].pv_table, 0, sizeof(thread_data[i].pv_table));
        memset(thread_data[i].pv_length, 0, sizeof(thread_data[i].pv_length));
    }

    // Disable I/O buffering for UCI compliance
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    // Main UCI loop
    while (1)
    {
        memset(input, 0, sizeof(input));
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            continue;

        if (input[0] == '\n')
            continue;

        // Remove newline
        size_t len = strlen(input);
        if (len > 0 && input[len-1] == '\n')
            input[len-1] = '\0';

        // UCI command: "uci"
        if (strncmp(input, "uci", 3) == 0)
        {
            printf("id name %s %s\n", NAME, VERSION);
            printf("id author %s\n", AUTHOR);
            printf("option name Hash type spin default 64 min 1 max %d\n", max_hash);
            printf("option name Threads type spin default 1 min 1 max %d\n", max_threads);
            printf("uciok\n");
            fflush(stdout);
        }

        // UCI command: "isready"
        else if (strncmp(input, "isready", 7) == 0)
        {
            printf("readyok\n");
            fflush(stdout);
        }

        // UCI command: "ucinewgame"
        else if (strncmp(input, "ucinewgame", 10) == 0)
        {
            parse_fen(start_position);
            clear_hash_table();
        }

        // UCI command: "position"
        else if (strncmp(input, "position", 8) == 0)
        {
            parse_position(input);
            clear_hash_table();
        }

        // UCI command: "go"
        else if (strncmp(input, "go", 2) == 0)
        {
            parse_go(input);
        }

        // UCI command: "stop"
        else if (strncmp(input, "stop", 4) == 0)
        {
            stop_search_threads();
            stopped = 1;
        }

        // UCI command: "quit"
        else if (strncmp(input, "quit", 4) == 0)
        {
            stop_search_threads();
            wait_for_threads();
            break;
        }

        // UCI command: "setoption name Hash value X"
        else if (strncmp(input, "setoption name Hash value ", 26) == 0)
        {
            mb = atoi(input + 26);
            if (mb < 1) mb = 1;
            if (mb > max_hash) mb = max_hash;
            init_hash_table(mb);
        }

        // UCI command: "setoption name Threads value X"
        else if (strncmp(input, "setoption name Threads value ", 29) == 0)
        {
            int threads = atoi(input + 29);
            if (threads < 1) threads = 1;
            if (threads > max_threads) threads = max_threads;
            
            // Silent thread initialization for UCI compliance
            num_threads = threads;
            thread_data.resize(num_threads);
            for (int i = 0; i < num_threads; i++) {
                thread_data[i].thread_id = i;
                memset(thread_data[i].killer_moves, 0, sizeof(thread_data[i].killer_moves));
                memset(thread_data[i].history_moves, 0, sizeof(thread_data[i].history_moves));
                memset(thread_data[i].pv_table, 0, sizeof(thread_data[i].pv_table));
                memset(thread_data[i].pv_length, 0, sizeof(thread_data[i].pv_length));
            }
        }

        // Debug command: "d" - print board (non-UCI, but useful)
        else if (strncmp(input, "d", 1) == 0 && strlen(input) == 1)
        {
            print_board();
        }
    }
}
