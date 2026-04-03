#ifndef PREMIUM_SQUARE_MAP_H
#define PREMIUM_SQUARE_MAP_H

#include "../def/board_defs.h"
#include "../def/game_defs.h"
#include "../ent/board.h"
#include "../ent/bonus_square.h"
#include "../ent/move.h"
#include "../util/io_util.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct PremiumSquareMap {
  uint64_t trial_count;
  uint64_t use_count[BOARD_DIM][BOARD_DIM];
} PremiumSquareMap;

static inline PremiumSquareMap *premium_square_map_create(void) {
  return (PremiumSquareMap *)calloc_or_die(1, sizeof(PremiumSquareMap));
}

static inline void premium_square_map_destroy(PremiumSquareMap *psm) {
  if (!psm) {
    return;
  }
  free(psm);
}

static inline void premium_square_map_reset(PremiumSquareMap *psm) {
  psm->trial_count = 0;
  memset(psm->use_count, 0, sizeof(psm->use_count));
}

static inline void premium_square_map_add_move(PremiumSquareMap *psm,
                                               const Move *move,
                                               const Board *board) {
  psm->trial_count++;
  if (move_get_type(move) != GAME_EVENT_TILE_PLACEMENT_MOVE) {
    return;
  }
  const int row_inc = move_get_dir(move) == BOARD_VERTICAL_DIRECTION;
  const int col_inc = move_get_dir(move) == BOARD_HORIZONTAL_DIRECTION;
  int row = move_get_row_start(move);
  int col = move_get_col_start(move);
  const int move_len = move_get_tiles_length(move);
  for (int tile_idx = 0; tile_idx < move_len; tile_idx++) {
    const MachineLetter ml = move_get_tile(move, tile_idx);
    if (ml != PLAYED_THROUGH_MARKER) {
      const BonusSquare bsq = board_get_bonus_square(board, row, col);
      const bool is_premium =
          bonus_square_get_letter_multiplier(bsq) > 1 ||
          bonus_square_get_word_multiplier(bsq) > 1;
      if (is_premium) {
        psm->use_count[row][col]++;
      }
    }
    row += row_inc;
    col += col_inc;
  }
}

static inline double premium_square_map_get_pct(const PremiumSquareMap *psm,
                                                int row, int col) {
  if (psm->trial_count == 0) {
    return 0.0;
  }
  return (double)psm->use_count[row][col] / (double)psm->trial_count;
}

#endif
