#include "../src/def/board_defs.h"
#include "../src/def/rack_defs.h"
#include "../src/ent/board.h"
#include "../src/ent/game.h"
#include "../src/ent/move.h"
#include "../src/ent/premium_square_map.h"
#include "../src/ent/sim_results.h"
#include "../src/ent/validated_move.h"
#include "../src/impl/config.h"
#include "../src/util/io_util.h"
#include "test_constants.h"
#include "test_util.h"
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static void assert_psm_counts(const char *label, const PremiumSquareMap *psm,
                               int row, int col, uint64_t expected_use_count,
                               uint64_t expected_trial_count) {
  if (psm->use_count[row][col] != expected_use_count) {
    printf("premium_square_map use_count mismatch (%s) at (%d, %d):\n", label,
           row, col);
    printf("actual: %" PRIu64 ", expected: %" PRIu64 "\n",
           psm->use_count[row][col], expected_use_count);
    assert(0);
  }
  if (psm->trial_count != expected_trial_count) {
    printf("premium_square_map trial_count mismatch (%s):\n", label);
    printf("actual: %" PRIu64 ", expected: %" PRIu64 "\n", psm->trial_count,
           expected_trial_count);
    assert(0);
  }
}

static void assert_all_use_counts_zero(const PremiumSquareMap *psm) {
  for (int row = 0; row < BOARD_DIM; row++) {
    for (int col = 0; col < BOARD_DIM; col++) {
      if (psm->use_count[row][col] != 0) {
        printf("expected all use_counts to be zero but (%d, %d) = %" PRIu64
               "\n",
               row, col, psm->use_count[row][col]);
        assert(0);
      }
    }
  }
}

static void psm_add_move(PremiumSquareMap *psm, const Game *game,
                         const Board *board, const char *ucgi_move_str) {
  ErrorStack *error_stack = error_stack_create();
  ValidatedMoves *vms =
      validated_moves_create(game, 0, ucgi_move_str, true, true, error_stack);
  if (!error_stack_is_empty(error_stack)) {
    error_stack_print_and_reset(error_stack);
    assert(0);
  }
  premium_square_map_add_move(psm, validated_moves_get_move(vms, 0), board);
  validated_moves_destroy(vms);
  error_stack_destroy(error_stack);
}

void test_premium_square_map(void) {
  Config *config =
      config_create_or_die("set -lex CSW21 -r1 all -r2 all -numplays 1");
  load_and_exec_config_or_die(config, "newgame");
  load_and_exec_config_or_die(config, "r PRETZEL");

  const Game *game = config_get_game(config);
  const Board *board = game_get_board(game);

  PremiumSquareMap *psm = premium_square_map_create();

  // Pass increments trial_count, no use_counts
  psm_add_move(psm, game, board, "pass");
  assert_psm_counts("pass", psm, 0, 0, 0, 1);
  assert_all_use_counts_zero(psm);

  // Exchange increments trial_count, no use_counts
  psm_add_move(psm, game, board, "ex ZEP");
  assert_psm_counts("exchange", psm, 0, 0, 0, 2);
  assert_all_use_counts_zero(psm);

  premium_square_map_reset(psm);
  assert_psm_counts("after reset", psm, 0, 0, 0, 0);
  assert_all_use_counts_zero(psm);

  // Tile placement: "8h ZE" covers (row=7,col=7) and (row=7,col=8).
  // H8 (row=7,col=7) is the center DWS — is_premium=true.
  // I8 (row=7,col=8) is a plain square — is_premium=false.
  psm_add_move(psm, game, board, "8h ZE");
  assert_psm_counts("ZE h8 DWS", psm, 7, 7, 1, 1);
  assert_psm_counts("ZE i8 plain", psm, 7, 8, 0, 1);

  // A second placement on the same premium square accumulates
  psm_add_move(psm, game, board, "8h PRETZEL");
  assert_psm_counts("PRETZEL h8 DWS", psm, 7, 7, 2, 2);

  // get_pct is use_count / trial_count
  assert(premium_square_map_get_pct(psm, 7, 7) == 1.0);
  assert(premium_square_map_get_pct(psm, 7, 8) == 0.0);

  // get_pct with trial_count == 0 returns 0.0
  premium_square_map_reset(psm);
  assert(premium_square_map_get_pct(psm, 7, 7) == 0.0);

  premium_square_map_destroy(psm);
  config_destroy(config);

  // Integration: run a sim with usepremiummap=true and verify PSM data
  {
    Config *sim_config = config_create_or_die(
        "set -lex NWL20 -wmp true -s1 score -s2 score -r1 all -r2 all "
        "-numplays 15 -plies 2 -threads 1 -iter 100 -scond none "
        "-usepremiummap true");
    load_and_exec_config_or_die(sim_config, "cgp " OPENING_CGP);
    load_and_exec_config_or_die(sim_config, "rack AAADERW");
    load_and_exec_config_or_die(sim_config, "gen");

    const error_code_t status = config_simulate_and_return_status(
        sim_config, NULL, NULL, config_get_sim_results(sim_config));
    assert(status == ERROR_STATUS_SUCCESS);

    const SimResults *sim_results = config_get_sim_results(sim_config);
    const int num_plays = sim_results_get_number_of_plays(sim_results);
    assert(num_plays > 0);

    for (int play_idx = 0; play_idx < num_plays; play_idx++) {
      SimmedPlay *sp = sim_results_get_simmed_play(sim_results, play_idx);
      const PremiumSquareMap *play_psm = simmed_play_get_premium_map(sp, 0);
      assert(play_psm != NULL);
      assert(play_psm->trial_count > 0);
      for (int row = 0; row < BOARD_DIM; row++) {
        for (int col = 0; col < BOARD_DIM; col++) {
          assert(play_psm->use_count[row][col] <= play_psm->trial_count);
        }
      }
    }

    config_destroy(sim_config);
  }
}
