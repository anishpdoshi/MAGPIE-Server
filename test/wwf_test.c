#include "../src/def/board_defs.h"
#include "../src/ent/board.h"
#include "../src/ent/bonus_square.h"
#include "../src/ent/game.h"
#include "../src/ent/letter_distribution.h"
#include "../src/ent/move.h"
#include "../src/ent/player.h"
#include "../src/ent/rack.h"
#include "../src/impl/config.h"
#include "../src/impl/gameplay.h"
#include "../src/impl/move_gen.h"
#include "../src/util/io_util.h"
#include "test_util.h"
#include <assert.h>

static void test_wwf_board_layout(void) {
  Config *config = config_create_or_die(
      "set -lex ENABLE -ld wwf_english -bdn wwf15 "
      "-s1 score -s2 score -r1 all -r2 all -numplays 1");
  Game *game = config_game_create(config);
  const Board *board = game_get_board(game);

  // WWF center (7,7) has no bonus
  BonusSquare center = board_get_bonus_square(board, 7, 7);
  assert(bonus_square_get_word_multiplier(center) == 1);
  assert(bonus_square_get_letter_multiplier(center) == 1);

  // TW at (0,3) and (3,0) — not corners like Scrabble
  BonusSquare tw_0_3 = board_get_bonus_square(board, 0, 3);
  assert(bonus_square_get_word_multiplier(tw_0_3) == 3);
  BonusSquare tw_3_0 = board_get_bonus_square(board, 3, 0);
  assert(bonus_square_get_word_multiplier(tw_3_0) == 3);

  // Corners are normal in WWF
  BonusSquare corner = board_get_bonus_square(board, 0, 0);
  assert(bonus_square_get_word_multiplier(corner) == 1);
  assert(bonus_square_get_letter_multiplier(corner) == 1);

  // DW at (7,3) and (7,11)
  BonusSquare dw_7_3 = board_get_bonus_square(board, 7, 3);
  assert(bonus_square_get_word_multiplier(dw_7_3) == 2);
  BonusSquare dw_7_11 = board_get_bonus_square(board, 7, 11);
  assert(bonus_square_get_word_multiplier(dw_7_11) == 2);

  // TL at (0,6) and (6,0)
  BonusSquare tl_0_6 = board_get_bonus_square(board, 0, 6);
  assert(bonus_square_get_letter_multiplier(tl_0_6) == 3);
  BonusSquare tl_6_0 = board_get_bonus_square(board, 6, 0);
  assert(bonus_square_get_letter_multiplier(tl_6_0) == 3);

  // DL at (1,2) and (2,1)
  BonusSquare dl_1_2 = board_get_bonus_square(board, 1, 2);
  assert(bonus_square_get_letter_multiplier(dl_1_2) == 2);
  BonusSquare dl_2_1 = board_get_bonus_square(board, 2, 1);
  assert(bonus_square_get_letter_multiplier(dl_2_1) == 2);

  // Symmetry: TW at (14,11) mirrors (0,3)
  BonusSquare tw_14_11 = board_get_bonus_square(board, 14, 11);
  assert(bonus_square_get_word_multiplier(tw_14_11) == 3);

  game_destroy(game);
  config_destroy(config);
}

static void test_wwf_letter_distribution(void) {
  Config *config = config_create_or_die(
      "set -lex ENABLE -ld wwf_english -bdn wwf15 "
      "-s1 score -s2 score -r1 all -r2 all -numplays 1");
  Game *game = config_game_create(config);
  const LetterDistribution *ld = game_get_ld(game);

  assert(ld_get_total_tiles(ld) == 104);

  // Spot-check WWF point values (differ from Scrabble)
  // B=4 in WWF (3 in Scrabble)
  MachineLetter ml_b = ld_hl_to_ml(ld, "B");
  assert(equity_to_int(ld_get_score(ld, ml_b)) == 4);

  // H=3 in WWF (4 in Scrabble)
  MachineLetter ml_h = ld_hl_to_ml(ld, "H");
  assert(equity_to_int(ld_get_score(ld, ml_h)) == 3);

  // J=10 in WWF (8 in Scrabble)
  MachineLetter ml_j = ld_hl_to_ml(ld, "J");
  assert(equity_to_int(ld_get_score(ld, ml_j)) == 10);

  // V=5 in WWF (4 in Scrabble)
  MachineLetter ml_v = ld_hl_to_ml(ld, "V");
  assert(equity_to_int(ld_get_score(ld, ml_v)) == 5);

  // S has 5 tiles in WWF (4 in Scrabble)
  MachineLetter ml_s = ld_hl_to_ml(ld, "S");
  assert(ld_get_dist(ld, ml_s) == 5);

  // E has 13 tiles in WWF (12 in Scrabble)
  MachineLetter ml_e = ld_hl_to_ml(ld, "E");
  assert(ld_get_dist(ld, ml_e) == 13);

  game_destroy(game);
  config_destroy(config);
}

static void test_wwf_movegen(void) {
  Config *config = config_create_or_die(
      "set -lex ENABLE -ld wwf_english -bdn wwf15 -wmp true "
      "-s1 score -s2 score -r1 all -r2 all -numplays 10");
  Game *game = config_game_create(config);
  const LetterDistribution *ld = game_get_ld(game);

  const Player *player =
      game_get_player(game, game_get_player_on_turn_index(game));
  Rack *player_rack = player_get_rack(player);
  rack_set_to_string(ld, player_rack, "AEINRST");

  MoveList *move_list = move_list_create(10);
  const MoveGenArgs move_gen_args = {
      .game = game,
      .move_list = move_list,
      .thread_index = 0,
      .eq_margin_movegen = 0,
      .target_equity = EQUITY_MAX_VALUE,
      .target_leave_size_for_exchange_cutoff = UNSET_LEAVE_SIZE,
  };
  generate_moves_for_game(&move_gen_args);

  int count = move_list_get_count(move_list);
  assert(count > 0);

  const Move *top_move = move_list_get_move(move_list, 0);
  assert(move_get_type(top_move) == GAME_EVENT_TILE_PLACEMENT_MOVE);
  int top_score = equity_to_int(move_get_score(top_move));
  assert(top_score > 0);

  // Opening move must cross center (7,7)
  int row = move_get_row_start(top_move);
  int col = move_get_col_start(top_move);
  int dir = move_get_dir(top_move);
  int tiles = move_get_tiles_played(top_move);
  if (dir == 0) {
    assert(row == 7);
    assert(col <= 7 && col + tiles > 7);
  } else {
    assert(col == 7);
    assert(row <= 7 && row + tiles > 7);
  }

  move_list_destroy(move_list);
  game_destroy(game);
  config_destroy(config);
}

void test_wwf(void) {
  test_wwf_board_layout();
  test_wwf_letter_distribution();
  test_wwf_movegen();
}
