#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifndef BOARD_SIZE
#define BOARD_SIZE 9
#endif

#ifndef PLAYOUT_COUNT
#define PLAYOUT_COUNT 1000
#endif

#define LOG 0

// Board values
#define EMPTY 0
#define BLACK 1
#define WHITE 2
#define EDGE  3
// these are used in live-group detection
#define BLACK_ALIVE 4
#define WHITE_ALIVE 5
#define ALIVE_OFFSET 3

const char stone_chars[] = ".#o ";

#define NAME(color) ((color) == WHITE ? "white" : "black")

typedef struct board {
    struct col {
        // Each col & row is +2 entries wide to allow for edges.
        // Values defined above
        uint32_t rows[BOARD_SIZE + 2];
    } cols[BOARD_SIZE + 2];
    uint8_t ko_row, ko_col;
} Board;

#define STONE_AT(b, r, c) ((b)->cols[c].rows[r])
#define SET_STONE_AT(b, r, c, v) ((b)->cols[c].rows[r] = v)

#define OPPOSITE(color) ((color == WHITE) ? BLACK : WHITE)

#define IS_NEXT_TO(b, r, c, v)  ((STONE_AT(b, r + 1, c) == (v)) || \
                                 (STONE_AT(b, r - 1, c) == (v)) || \
                                 (STONE_AT(b, r, c + 1) == (v)) || \
                                 (STONE_AT(b, r, c - 1) == (v)))

#define CT_NEXT_TO(b, r, c, v)  ((STONE_AT(b, r + 1, c) == (v)) + \
                                 (STONE_AT(b, r - 1, c) == (v)) + \
                                 (STONE_AT(b, r, c + 1) == (v)) + \
                                 (STONE_AT(b, r, c - 1) == (v)))


// SINGLE EYE == 4 horizontal & vertical neighbors are all the right color, or edge.
#define SINGLE_EYE(b, r, c, color) ((CT_NEXT_TO(b, r, c, color) + CT_NEXT_TO(b, r, c, EDGE)) == 4)


// FALSE_EYE is only valid if SINGLE_EYE is true
// - >= two diagonal neighbors are opposite color, or
// - 1 diagonal neighbor opposite color and at edge
#define DIAG_NEIGHBORS(b, r, c, color) (((STONE_AT(b, r + 1, c + 1) == color) ? 1 : 0) + \
                                        ((STONE_AT(b, r + 1, c - 1) == color) ? 1 : 0) + \
                                        ((STONE_AT(b, r - 1, c + 1) == color) ? 1 : 0) + \
                                        ((STONE_AT(b, r - 1, c - 1) == color) ? 1 : 0))

#define AT_EDGE(b, r, c) IS_NEXT_TO(b, r, c, EDGE)

#define FALSE_EYE(b, r, c, color)  ((DIAG_NEIGHBORS(b, r, c, OPPOSITE(color)) >= 2) || \
                                    ((DIAG_NEIGHBORS(b, r, c, OPPOSITE(color)) == 1) && AT_EDGE(b, r, c)))


// Real single eyes = single eye, and not false.  Fails for some cases
// (see Two-headed dragon @ sensei's library).
#define SINGLE_REAL_EYE(b, r, c, color) (SINGLE_EYE(b, r, c, color) && (! FALSE_EYE(b, r, c, color)))

#define ALIVE(b, row, c, alive_color) (IS_NEXT_TO(b, row, c, EMPTY) || IS_NEXT_TO(b, row, c, alive_color))

#define LONE_ATARI(b, row, c, color) ((CT_NEXT_TO(b, row, c, EMPTY) == 1) && (! (IS_NEXT_TO(b, row, c, color))))


void clear_board(Board *b)
{
    int row, col;

    for (row = 0; row < BOARD_SIZE + 2; row++) {
        SET_STONE_AT(b, row, 0, EDGE);
        for (col = 1; col <= BOARD_SIZE; col++)
            SET_STONE_AT(b, row, col, ((row == 0) || (row == BOARD_SIZE + 1)) ? EDGE : EMPTY);            
        SET_STONE_AT(b, row, BOARD_SIZE + 1, EDGE);
    }
    b->ko_row = 0;
}



// **************************************************
// REMOVE DEAD GROUPS OF A GIVEN COLOR
// **************************************************
void recursive_mark_alive(Board *b, int row, int col, uint8_t color)
{
    SET_STONE_AT(b, row, col, color + ALIVE_OFFSET);
    if (STONE_AT(b, row + 1, col) == color) recursive_mark_alive(b, row + 1, col, color);
    if (STONE_AT(b, row - 1, col) == color) recursive_mark_alive(b, row - 1, col, color);
    if (STONE_AT(b, row, col + 1) == color) recursive_mark_alive(b, row, col + 1, color);
    if (STONE_AT(b, row, col - 1) == color) recursive_mark_alive(b, row, col - 1, color);
}

int remove_dead_groups(Board *b,
                       uint8_t color)
{
    uint8_t alive_color = color + ALIVE_OFFSET;
    // NB: we add because interior space on board is 1 indexed
    int row, col;
    int died = 0;

    for (row = 1; row <= BOARD_SIZE; row++)
        for (col = 1; col <= BOARD_SIZE; col++)
            if ((STONE_AT(b, row, col) == color) && ALIVE(b, row, col, alive_color))
                recursive_mark_alive(b, row, col, color);

    // replace alive stones with stones of that color, and not-alive with empty.
    for (row = 1; row <= BOARD_SIZE; row++)
        for (col = 1; col <= BOARD_SIZE; col++)
            if (STONE_AT(b, row, col) == color) {
                SET_STONE_AT(b, row, col, EMPTY);
                died += 1;
            } else if (STONE_AT(b, row, col) == alive_color)
                SET_STONE_AT(b, row, col, color);

    // find how many stones died
    return died;
}
 
// **************************************************
// REMOVE DEAD GROUPS & KO DETECTION
// 
// returns whether the board changed
// **************************************************
int find_dead_groups(Board *b, int which_row, int which_col, uint8_t color)
{
    int num_killed = 0;
    int num_suicide = 0;

    int check_for_dead = IS_NEXT_TO(b, which_row, which_col, OPPOSITE(color));
    if (check_for_dead) num_killed = remove_dead_groups(b, OPPOSITE(color));

    if ((num_killed == 0) && (! IS_NEXT_TO(b, which_row, which_col, EMPTY)))
        num_suicide = remove_dead_groups(b, color);

    // update ko state
    if ((num_killed == 1) && LONE_ATARI(b, which_row, which_col, color)) {
        if      (STONE_AT(b, which_row + 1, which_col) == EMPTY) { b->ko_row = which_row + 1; b->ko_col = which_col; }
        else if (STONE_AT(b, which_row - 1, which_col) == EMPTY) { b->ko_row = which_row - 1; b->ko_col = which_col; }
        else if (STONE_AT(b, which_row, which_col + 1) == EMPTY) { b->ko_row = which_row;     b->ko_col = which_col + 1; }
        else                                                     { b->ko_row = which_row;     b->ko_col = which_col - 1; }
    } else 
        b->ko_row = b->ko_col = 0;

    // Return whether the board changed.
    // The only way that didn't happen is if this was a single-stone suicide play.
    // NB: all threads will return the same value.
    return (num_suicide != 1);
}


// **************************************************
// Makes a random move and returns true if the board changed.
// **************************************************
int make_random_move(Board *b,
                     uint8_t color)
{
    int row, col;
    int total_valid = 0;
    int which_to_make, which_row, which_col;
    int offsets[BOARD_SIZE * BOARD_SIZE][2];

    // is this row/col a valid move?
    for (row = 1; row <= BOARD_SIZE; row++)
        for (col = 1; col <= BOARD_SIZE; col++) {
            offsets[total_valid][0] = row;
            offsets[total_valid][1] = col;
            total_valid += ((STONE_AT(b, row, col) == EMPTY) &&
                            (! SINGLE_REAL_EYE(b, row, col, color)) &&
                            ((b->ko_row != row) || (b->ko_col != col)));
        }
    
    // if there were no possible moves, return that the board cannot change.
    if (total_valid == 0) return 0;

    which_to_make = random() % total_valid;
    which_row = offsets[which_to_make][0];
    which_col = offsets[which_to_make][1];
    SET_STONE_AT(b, which_row, which_col, color);
    return find_dead_groups(b, which_row, which_col, color);
}

void play_out(Board *start_board,
              Board *dest_board,
              uint8_t first_move_color,
              int max_moves,
              int max_unchanged)
{
    int move_count = 0;
    int unchanged_count = 0;
    uint8_t current_color = first_move_color;

    *dest_board = *start_board;

    while ((move_count < max_moves) && 
           (unchanged_count < max_unchanged)) {
        int board_changed = make_random_move(dest_board, current_color);
        if (! board_changed) unchanged_count++;
        current_color = OPPOSITE(current_color);
        move_count++;
    }
}

void play_moves(Board *b, char *moves)
{
    int idx = 0;
    while (moves[idx] != '\0') {
        printf("move: %c%c%c\n", moves[idx], moves[idx+1], moves[idx+2]);
        uint32_t color = (moves[idx] == 'B') ? BLACK : WHITE;
        int which_col = moves[idx + 1] - 'a' + 1;
        int which_row = moves[idx + 2] - 'a' + 1;

        idx += 3;
        // passes are encoded as moves outside the board
        if ((which_row > BOARD_SIZE) || (which_col > BOARD_SIZE)) {
            b->ko_row = b->ko_col = 0;
            continue;
        }
        assert (STONE_AT(b, which_row, which_col) == EMPTY);
        SET_STONE_AT(b, which_row, which_col, color);
        find_dead_groups(b, which_row, which_col, color);
    }
}

void sum_boards(Board *boards,
                int num_boards,
                Board *dest_board)
{
    int row, col, i;

    for (row = 0; row <= BOARD_SIZE + 1; row++)
        for (col = 0; col <= BOARD_SIZE + 1; col++)
            SET_STONE_AT(dest_board, row, col, 0);


    for (i = 0; i < num_boards; i++)
        for (row = 1; row <= BOARD_SIZE; row++)
            for (col = 1; col <= BOARD_SIZE; col++) {
                int color = STONE_AT(boards + i, row, col);
                if ((color == BLACK) || ((color == EMPTY) &&
                                         IS_NEXT_TO(boards + i, row, col, BLACK)))
                    STONE_AT(dest_board, row, col)++; // abuse of macros
            }
}

void print_board(Board *b)
{
    int row, col;

    for (row = 0; row <= BOARD_SIZE + 1; row++) {
        for (col = 0; col <= BOARD_SIZE + 1; col++)
            printf("%c ", stone_chars[STONE_AT(b, row, col)]);
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    Board start_board, sum_board;
    Board playouts[PLAYOUT_COUNT];
    int i, row, col;
    
    // **************************************************
    // PARSE ARGUMENTS
    // **************************************************
    if (argc != 5) {
        printf("usage: board SIZE KOMI MOVES TO_PLAY\n");
        exit(1);
    }

    assert (atoi(argv[1]) == BOARD_SIZE);
    float komi = atof(argv[2]);
    char *moves = argv[3];
    int color_to_play = (*argv[4] == 'W') ? WHITE : BLACK;

    srandom(time(0));


    clear_board(&start_board);
    play_moves(&start_board, moves);
    print_board(&start_board);

    for (i = 0; i < PLAYOUT_COUNT; i++)
        play_out(&start_board, &(playouts[i]), color_to_play, 1000, 100);
    print_board(playouts);
    sum_boards(&(playouts[0]), PLAYOUT_COUNT, &sum_board);

    for (row = 1; row <= BOARD_SIZE; row++) {
        printf("[");
        for (col = 1; col <= BOARD_SIZE; col++) {
            printf("%d, ", STONE_AT(&sum_board, row, col));
        }
        printf("]\n");
    }

    exit(0);
}
