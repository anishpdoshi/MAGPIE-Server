#include "../docs/swagger_ui.h"
#include "../vendor/cJSON/cJSON.h"
#include "../vendor/mongoose/mongoose.h"

#include "../src/def/board_defs.h"
#include "../src/def/game_history_defs.h"
#include "../src/ent/board.h"
#include "../src/ent/equity.h"
#include "../src/ent/game.h"
#include "../src/ent/letter_distribution.h"
#include "../src/ent/move.h"
#include "../src/ent/player.h"
#include "../src/ent/premium_square_map.h"
#include "../src/ent/rack.h"
#include "../src/ent/sim_results.h"
#include "../src/ent/stats.h"
#include "../src/impl/cmd_api.h"
#include "../src/impl/config.h"
#include "../src/str/move_string.h"
#include "../src/util/string_util.h"
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Magpie *g_magpie = NULL;
static pthread_mutex_t g_magpie_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char *g_data_paths = "./data:./testdata";
static char *g_openapi_yaml = NULL;
static volatile sig_atomic_t g_running = 1;

static char *read_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)len + 1);
  if (buf) {
    size_t read_len = fread(buf, 1, (size_t)len, f);
    buf[read_len] = '\0';
  }
  fclose(f);
  return buf;
}

static void signal_handler(int sig) {
  (void)sig;
  g_running = 0;
}

static char *get_json_string(const cJSON *json, const char *key,
                             const char *default_val) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
  if (cJSON_IsString(item) && item->valuestring != NULL) {
    return item->valuestring;
  }
  return (char *)default_val;
}

static int get_json_int(const cJSON *json, const char *key, int default_val) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
  if (cJSON_IsNumber(item)) {
    return item->valueint;
  }
  return default_val;
}

static bool get_json_bool(const cJSON *json, const char *key,
                          bool default_val) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
  if (cJSON_IsBool(item)) {
    return cJSON_IsTrue(item);
  }
  return default_val;
}

static void send_json_error(struct mg_connection *conn, int status_code,
                            const char *message) {
  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "error", message);
  char *body = cJSON_PrintUnformatted(resp);
  mg_http_reply(conn, status_code, "Content-Type: application/json\r\n", "%s",
                body);
  free(body);
  cJSON_Delete(resp);
}

static void send_json_response(struct mg_connection *conn, cJSON *resp) {
  char *body = cJSON_PrintUnformatted(resp);
  mg_http_reply(conn, 200, "Content-Type: application/json\r\n", "%s", body);
  free(body);
  cJSON_Delete(resp);
}

static cmd_exit_code run_cmd(Magpie *mp, const char *cmd) {
  cmd_exit_code code = magpie_run_sync(mp, cmd);
  if (code != 0) {
    char *err = magpie_get_and_clear_error(mp);
    fprintf(stderr, "magpie command failed: %s -> %s\n", cmd, err ? err : "?");
    free(err);
  }
  return code;
}

static char *format_move_ucgi(const Move *move, const Board *board,
                              const LetterDistribution *ld) {
  StringBuilder *sb = string_builder_create();
  string_builder_add_ucgi_move(sb, move, board, ld);
  char *result = string_duplicate(string_builder_peek(sb));
  string_builder_destroy(sb);
  return result;
}

static char *format_move_human(const Move *move,
                               const LetterDistribution *ld) {
  StringBuilder *sb = string_builder_create();
  string_builder_add_move_description(sb, move, ld);
  char *result = string_duplicate(string_builder_peek(sb));
  string_builder_destroy(sb);
  return result;
}

static char *format_leave(const Rack *rack, const Move *move,
                          const LetterDistribution *ld) {
  StringBuilder *sb = string_builder_create();
  string_builder_add_move_leave(sb, rack, move, ld);
  char *result = string_duplicate(string_builder_peek(sb));
  string_builder_destroy(sb);
  return result;
}

static cJSON *move_to_json(const Move *move, const Board *board,
                           const LetterDistribution *ld, const Rack *rack) {
  cJSON *obj = cJSON_CreateObject();
  game_event_t type = move_get_type(move);

  char *ucgi = format_move_ucgi(move, board, ld);
  char *human = format_move_human(move, ld);
  char *leave = format_leave(rack, move, ld);
  cJSON_AddStringToObject(obj, "move", ucgi);
  cJSON_AddStringToObject(obj, "description", human);
  cJSON_AddStringToObject(obj, "leave", leave);
  free(ucgi);
  free(human);
  free(leave);

  if (type == GAME_EVENT_TILE_PLACEMENT_MOVE) {
    cJSON_AddStringToObject(obj, "type", "tile_placement");
    cJSON_AddNumberToObject(obj, "score",
                            equity_to_int(move_get_score(move)));
    cJSON_AddNumberToObject(obj, "row", move_get_row_start(move));
    cJSON_AddNumberToObject(obj, "col", move_get_col_start(move));
    cJSON_AddNumberToObject(obj, "direction", move_get_dir(move));
    cJSON_AddNumberToObject(obj, "tiles_played", move_get_tiles_played(move));
  } else if (type == GAME_EVENT_PASS) {
    cJSON_AddStringToObject(obj, "type", "pass");
    cJSON_AddNumberToObject(obj, "score", 0);
  } else if (type == GAME_EVENT_EXCHANGE) {
    cJSON_AddStringToObject(obj, "type", "exchange");
    cJSON_AddNumberToObject(obj, "score", 0);
  }

  cJSON_AddNumberToObject(obj, "equity",
                          equity_to_double(move_get_equity(move)));
  return obj;
}

static cJSON *simmed_play_to_json(const SimmedPlay *display_sp,
                                  SimmedPlay *real_sp, const Board *board,
                                  const LetterDistribution *ld,
                                  const Rack *rack, int num_plies,
                                  bool include_premium_map) {
  const Move *move = simmed_play_get_move(display_sp);
  cJSON *obj = move_to_json(move, board, ld, rack);

  const Stat *eq_stat = simmed_play_get_equity_stat(display_sp);
  const Stat *wp_stat = simmed_play_get_win_pct_stat(display_sp);
  cJSON_AddNumberToObject(obj, "sim_equity_mean", stat_get_mean(eq_stat));
  cJSON_AddNumberToObject(obj, "sim_equity_stdev", stat_get_stdev(eq_stat));
  cJSON_AddNumberToObject(obj, "win_pct", stat_get_mean(wp_stat));
  cJSON_AddNumberToObject(obj, "iterations",
                          (double)stat_get_num_samples(wp_stat));

  cJSON *plies_arr = cJSON_AddArrayToObject(obj, "plies");
  for (int ply_idx = 0; ply_idx < num_plies; ply_idx++) {
    cJSON *ply_obj = cJSON_CreateObject();
    const Stat *score_stat = simmed_play_get_score_stat(display_sp, ply_idx);
    const Stat *bingo_stat = simmed_play_get_bingo_stat(display_sp, ply_idx);
    cJSON_AddNumberToObject(ply_obj, "score_mean", stat_get_mean(score_stat));
    cJSON_AddNumberToObject(ply_obj, "bingo_pct", stat_get_mean(bingo_stat));

    if (include_premium_map && real_sp) {
      PremiumSquareMap *psm = simmed_play_get_premium_map(real_sp, ply_idx);
      if (psm && psm->trial_count > 0) {
        cJSON *psm_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(psm_obj, "trial_count",
                                (double)psm->trial_count);
        cJSON *squares = cJSON_AddArrayToObject(psm_obj, "squares");
        for (int row = 0; row < BOARD_DIM; row++) {
          for (int col = 0; col < BOARD_DIM; col++) {
            if (psm->use_count[row][col] > 0) {
              cJSON *sq = cJSON_CreateObject();
              cJSON_AddNumberToObject(sq, "row", row);
              cJSON_AddNumberToObject(sq, "col", col);
              cJSON_AddNumberToObject(
                  sq, "pct", premium_square_map_get_pct(psm, row, col));
              cJSON_AddItemToArray(squares, sq);
            }
          }
        }
        cJSON_AddItemToObject(ply_obj, "premium_square_map", psm_obj);
      }
    }
    cJSON_AddItemToArray(plies_arr, ply_obj);
  }

  return obj;
}

static void handle_docs(struct mg_connection *conn) {
  mg_http_reply(conn, 200, "Content-Type: text/html\r\n", "%s",
                swagger_ui_html);
}

static void handle_openapi_yaml(struct mg_connection *conn) {
  if (g_openapi_yaml) {
    mg_http_reply(conn, 200,
                  "Content-Type: text/yaml\r\n"
                  "Access-Control-Allow-Origin: *\r\n",
                  "%s", g_openapi_yaml);
  } else {
    send_json_error(conn, 404, "openapi.yaml not found");
  }
}

static void handle_health(struct mg_connection *conn) {
  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "status", "ok");
  send_json_response(conn, resp);
}

static void handle_movegen(struct mg_connection *conn,
                           struct mg_http_message *hm) {
  cJSON *req = cJSON_ParseWithLength(hm->body.buf, hm->body.len);
  if (!req) {
    send_json_error(conn, 400, "invalid JSON body");
    return;
  }

  const char *cgp = get_json_string(req, "cgp", NULL);
  const char *rack = get_json_string(req, "rack", NULL);
  const char *lexicon = get_json_string(req, "lexicon", "CSW21");
  int num_plays = get_json_int(req, "num_plays", 15);

  if (!cgp || !rack) {
    send_json_error(conn, 400, "cgp and rack are required");
    cJSON_Delete(req);
    return;
  }

  pthread_mutex_lock(&g_magpie_mutex);

  char cmd_buf[4096];
  snprintf(cmd_buf, sizeof(cmd_buf),
           "set -lex %s -wmp true -s1 equity -s2 equity "
           "-r1 all -r2 all -numplays %d "
           "-useheatmap false -usepremiummap false",
           lexicon, num_plays);
  if (run_cmd(g_magpie, cmd_buf) != 0) {
    pthread_mutex_unlock(&g_magpie_mutex);
    send_json_error(conn, 500, "failed to configure engine");
    cJSON_Delete(req);
    return;
  }

  snprintf(cmd_buf, sizeof(cmd_buf), "cgp %s", cgp);
  if (run_cmd(g_magpie, cmd_buf) != 0) {
    pthread_mutex_unlock(&g_magpie_mutex);
    send_json_error(conn, 500, "failed to load position");
    cJSON_Delete(req);
    return;
  }

  snprintf(cmd_buf, sizeof(cmd_buf), "rack %s", rack);
  if (run_cmd(g_magpie, cmd_buf) != 0) {
    pthread_mutex_unlock(&g_magpie_mutex);
    send_json_error(conn, 500, "failed to set rack");
    cJSON_Delete(req);
    return;
  }

  if (run_cmd(g_magpie, "generate") != 0) {
    pthread_mutex_unlock(&g_magpie_mutex);
    send_json_error(conn, 500, "move generation failed");
    cJSON_Delete(req);
    return;
  }

  const Config *config = magpie_get_config(g_magpie);
  const Game *game = config_get_game(config);
  const Board *board = game_get_board(game);
  const LetterDistribution *ld = game_get_ld(game);
  const MoveList *ml = config_get_move_list(config);
  const Rack *game_rack =
      player_get_rack(game_get_player(game, game_get_player_on_turn_index(game)));
  int count = move_list_get_count(ml);

  cJSON *resp = cJSON_CreateObject();
  cJSON *moves_arr = cJSON_AddArrayToObject(resp, "moves");
  for (int move_idx = 0; move_idx < count && move_idx < num_plays;
       move_idx++) {
    const Move *move = move_list_get_move(ml, move_idx);
    cJSON *move_obj = move_to_json(move, board, ld, game_rack);
    cJSON_AddItemToArray(moves_arr, move_obj);
  }
  cJSON_AddNumberToObject(resp, "num_moves", count);

  pthread_mutex_unlock(&g_magpie_mutex);
  send_json_response(conn, resp);
  cJSON_Delete(req);
}

static void handle_sim(struct mg_connection *conn,
                       struct mg_http_message *hm) {
  cJSON *req = cJSON_ParseWithLength(hm->body.buf, hm->body.len);
  if (!req) {
    send_json_error(conn, 400, "invalid JSON body");
    return;
  }

  const char *cgp = get_json_string(req, "cgp", NULL);
  const char *rack = get_json_string(req, "rack", NULL);
  const char *lexicon = get_json_string(req, "lexicon", "CSW21");
  int num_plays = get_json_int(req, "num_plays", 15);
  int plies = get_json_int(req, "plies", 2);
  int iterations = get_json_int(req, "iterations", 1000);
  int threads = get_json_int(req, "threads", 4);
  bool use_premium_map = get_json_bool(req, "use_premium_map", false);

  if (!cgp || !rack) {
    send_json_error(conn, 400, "cgp and rack are required");
    cJSON_Delete(req);
    return;
  }

  pthread_mutex_lock(&g_magpie_mutex);

  char cmd_buf[4096];
  int min_iterations = get_json_int(req, "min_iterations", 100);
  snprintf(cmd_buf, sizeof(cmd_buf),
           "set -lex %s -wmp true -s1 equity -s2 equity "
           "-r1 all -r2 all -numplays %d -plies %d -iter %d "
           "-minp %d -threads %d -scond none -thres gk16 -sr tt "
           "-cutoff 0 -useheatmap false -usepremiummap %s",
           lexicon, num_plays, plies, iterations, min_iterations, threads,
           use_premium_map ? "true" : "false");
  if (run_cmd(g_magpie, cmd_buf) != 0) {
    pthread_mutex_unlock(&g_magpie_mutex);
    send_json_error(conn, 500, "failed to configure engine");
    cJSON_Delete(req);
    return;
  }

  snprintf(cmd_buf, sizeof(cmd_buf), "cgp %s", cgp);
  if (run_cmd(g_magpie, cmd_buf) != 0) {
    pthread_mutex_unlock(&g_magpie_mutex);
    send_json_error(conn, 500, "failed to load position");
    cJSON_Delete(req);
    return;
  }

  snprintf(cmd_buf, sizeof(cmd_buf), "rack %s", rack);
  if (run_cmd(g_magpie, cmd_buf) != 0) {
    pthread_mutex_unlock(&g_magpie_mutex);
    send_json_error(conn, 500, "failed to set rack");
    cJSON_Delete(req);
    return;
  }

  if (run_cmd(g_magpie, "gsimulate") != 0) {
    pthread_mutex_unlock(&g_magpie_mutex);
    send_json_error(conn, 500, "simulation failed");
    cJSON_Delete(req);
    return;
  }

  const Config *config = magpie_get_config(g_magpie);
  const Game *game = config_get_game(config);
  const Board *board = game_get_board(game);
  const LetterDistribution *ld = game_get_ld(game);
  SimResults *sim_results = config_get_sim_results(config);
  const Rack *game_rack =
      player_get_rack(game_get_player(game, game_get_player_on_turn_index(game)));
  int num_simmed = sim_results_get_number_of_plays(sim_results);
  int sim_plies = sim_results_get_num_plies(sim_results);

  bool sorted =
      sim_results_lock_and_sort_display_simmed_plays(sim_results);

  cJSON *resp = cJSON_CreateObject();
  cJSON *plays_arr = cJSON_AddArrayToObject(resp, "plays");

  if (sorted) {
    for (int play_idx = 0; play_idx < num_simmed; play_idx++) {
      const SimmedPlay *display_sp =
          sim_results_get_display_simmed_play(sim_results, play_idx);
      int orig_idx = simmed_play_get_play_index_by_sort_type(display_sp);
      SimmedPlay *real_sp =
          sim_results_get_simmed_play(sim_results, orig_idx);
      cJSON *play_obj = simmed_play_to_json(
          display_sp, real_sp, board, ld, game_rack, sim_plies,
          use_premium_map);
      cJSON_AddItemToArray(plays_arr, play_obj);
    }
    sim_results_unlock_display_infos(sim_results);
  }

  cJSON_AddNumberToObject(resp, "num_plays", num_simmed);
  cJSON_AddNumberToObject(resp, "iterations",
                          (double)sim_results_get_iteration_count(sim_results));

  pthread_mutex_unlock(&g_magpie_mutex);
  send_json_response(conn, resp);
  cJSON_Delete(req);
}

static void http_handler(struct mg_connection *conn, int ev, void *ev_data) {
  if (ev != MG_EV_HTTP_MSG) {
    return;
  }
  struct mg_http_message *hm = (struct mg_http_message *)ev_data;

  if (mg_match(hm->uri, mg_str("/api/docs/openapi.yaml"), NULL)) {
    handle_openapi_yaml(conn);
  } else if (mg_match(hm->uri, mg_str("/api/docs"), NULL) ||
             mg_match(hm->uri, mg_str("/api/docs/"), NULL)) {
    handle_docs(conn);
  } else if (mg_match(hm->uri, mg_str("/api/health"), NULL)) {
    handle_health(conn);
  } else if (mg_match(hm->uri, mg_str("/api/movegen"), NULL)) {
    if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
      send_json_error(conn, 405, "method not allowed");
      return;
    }
    handle_movegen(conn, hm);
  } else if (mg_match(hm->uri, mg_str("/api/sim"), NULL)) {
    if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
      send_json_error(conn, 405, "method not allowed");
      return;
    }
    handle_sim(conn, hm);
  } else {
    send_json_error(conn, 404, "not found");
  }
}

int main(int argc, char *argv[]) {
  static char addr_buf[64];
  const char *listen_addr = "http://0.0.0.0:8080";
  const char *data_paths = g_data_paths;

  const char *env_port = getenv("PORT");
  if (env_port) {
    snprintf(addr_buf, sizeof(addr_buf), "http://0.0.0.0:%s", env_port);
    listen_addr = addr_buf;
  }

  for (int arg_idx = 1; arg_idx < argc; arg_idx++) {
    if (strcmp(argv[arg_idx], "--port") == 0 && arg_idx + 1 < argc) {
      snprintf(addr_buf, sizeof(addr_buf), "http://0.0.0.0:%s",
               argv[++arg_idx]);
      listen_addr = addr_buf;
    } else if (strcmp(argv[arg_idx], "--data") == 0 && arg_idx + 1 < argc) {
      data_paths = argv[++arg_idx];
    }
  }

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  g_magpie = magpie_create(data_paths);
  if (!g_magpie) {
    fprintf(stderr, "failed to create magpie instance\n");
    return 1;
  }

  if (run_cmd(g_magpie, "set -lex CSW21 -wmp true") != 0) {
    fprintf(stderr, "failed initial engine setup\n");
    magpie_destroy(g_magpie);
    return 1;
  }

  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  struct mg_connection *conn = mg_http_listen(&mgr, listen_addr, http_handler, NULL);
  if (!conn) {
    fprintf(stderr, "failed to listen on %s\n", listen_addr);
    magpie_destroy(g_magpie);
    return 1;
  }

  g_openapi_yaml = read_file("docs/openapi.yaml");
  if (!g_openapi_yaml) {
    fprintf(stderr, "warning: docs/openapi.yaml not found, /api/docs disabled\n");
  }

  fprintf(stdout, "MAGPIE API server listening on %s\n", listen_addr);
  fprintf(stdout, "API docs: %s/api/docs\n", listen_addr);
  fflush(stdout);

  while (g_running) {
    mg_mgr_poll(&mgr, 100);
  }

  fprintf(stdout, "\nshutting down...\n");
  mg_mgr_free(&mgr);
  magpie_destroy(g_magpie);
  free(g_openapi_yaml);
  return 0;
}
