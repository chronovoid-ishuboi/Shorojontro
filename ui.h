#ifndef COUP_UI_H
#define COUP_UI_H

#include "game.h"

/* All terminal I/O and rules orchestration (challenges/blocks) live here. */

void ui_read_line(char* buf, int cap);
int  ui_read_int_in_range(int lo, int hi);
int  ui_yes_no(const char* prompt);
int  ui_choose_target(const Game* g, int srcIdx);
int  ui_choose_influence_to_lose(const Player* p);
Role ui_choose_reveal_role_for(Player* p, int mustRoleKnown, Role mustRole); // returns ROLE_COUNT if "cannot reveal"
void ui_set_player_names(Game* g);
void ui_show_private_cards(const Game* g, int idx);
void ui_press_enter(void);

/* Turn driver: prompts current player for action, runs challenges/blocks, applies results, advances turn */
void ui_run_game_loop(Game* g);

#endif /* COUP_UI_H */
