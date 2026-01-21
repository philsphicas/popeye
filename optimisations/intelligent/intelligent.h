/******************** MODIFICATIONS to pyint.h **************************
**
** Date       Who  What
**
** 1997/04/04 TLi  Original
**
**************************** End of List ******************************/

#if !defined(OPTIMISATIONS_INTELLIGENT_INTELLIGENT_H)
#define OPTIMISATIONS_INTELLIGENT_INTELLIGENT_H

#include "pieces/pieces.h"
#include "position/position.h"
#include "position/pieceid.h"
#include "stipulation/goals/goals.h"
#include "solving/machinery/solve.h"
#include "solving/ply.h"
#include "optimisations/intelligent/piece_usage.h"

typedef struct
{
    square diagram_square;
    Flags flags;
    piece_walk_type type;
    piece_usage usage;
} PIECE;

extern PIECE white[nr_squares_on_board];
extern PIECE black[nr_squares_on_board];
extern PIECE target_position[MaxPieceId+1];

enum { index_of_king = 0 };

extern unsigned int MaxPiece[nr_sides];
extern unsigned int CapturesLeft[maxply+1];

extern boolean solutions_found;

extern goal_type goal_to_be_reached;

extern unsigned int nr_reasons_for_staying_empty[maxsquare+4];

extern unsigned int moves_to_white_prom[nr_squares_on_board];

extern boolean testcastling;

extern unsigned int MovesRequired[nr_sides][maxply+1];

extern unsigned int PieceId2index[MaxPieceId+1];

void IntelligentRegulargoal_types(slice_index si);

void solve_target_position(slice_index si);

boolean black_pawn_attacks_king(square from);

void remember_to_keep_rider_line_open(square from, square to,
                                      int dir, int delta);

/* Detrmine whether some line is empty
 * @param start start of line
 * @param end end of line
 * @param dir direction from start to end
 * @return true iff the line is empty
 */
boolean is_line_empty(square start, square end, int dir);

/* Initialize intelligent mode if the user or the stipulation asks for
 * it
 * @param si identifies slice where to start
 * @return false iff the user asks for intelligent mode, but the
 * stipulation doesn't support it
 */
boolean init_intelligent_mode(slice_index si);

/* MinBlockers constraint for intelligent mode
 * If min_blockers_count > 0, only consider target positions
 * that require at least min_blockers_count flight blockers
 */
extern unsigned int min_blockers_count;

/* Set minimum blockers constraint */
void set_min_blockers_constraint(unsigned int count);

/* MatingSquare constraint for intelligent mode
 * When mating_square_constrained is true, only consider target positions
 * where the black king ends up on an allowed square
 */
extern boolean mating_square_constrained;
extern boolean mating_square_allowed[nr_squares_on_board];

/* Reset mating square constraints (allow all squares) */
void reset_mating_square_constraints(void);

/* Allow only edge squares as mating squares */
void mating_square_allow_edge(void);

/* Allow only corner squares as mating squares */
void mating_square_allow_corner(void);

/* Allow only middle (non-edge) squares as mating squares */
void mating_square_allow_middle(void);

/* Allow a specific square as a mating square */
void mating_square_allow_square(square sq);

/* Partition support for parallel solving
 *
 * The partition system allows dividing the search space for distribution
 * across multiple workers. The search space is:
 *   king_square (64) × checker_piece (up to 15) × check_square (64)
 *   = up to 61,440 combinations
 *
 * Partitions are numbered 0 to partition_total-1.
 * With king_square varying fastest, progress is visible across all
 * king squares early in the search.
 *
 * Mapping: combo_index = check_sq_idx * (64 * 15) + checker_idx * 64 + king_idx
 * A combination belongs to partition: combo_index % partition_total
 */
extern unsigned int partition_index;
extern unsigned int partition_total;

/* Current king index for partition checking in nested loops */
extern unsigned int current_king_index;

/* Set partition N of M (0-indexed) */
void set_partition(unsigned int index, unsigned int total);

/* Reset partition (disabled) */
void reset_partition(void);

/* Check if a (king, checker, check_sq) combination is in current partition */
boolean is_in_partition(unsigned int king_idx,
                        unsigned int checker_idx,
                        unsigned int check_sq_idx);

#endif
