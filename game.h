#ifndef COUP_GAME_H
#define COUP_GAME_H

#include <stddef.h>

#define MAX_PLAYERS 6
#define MAX_NAME_LEN 48
#define DECK_COPIES_PER_ROLE 3
#define ROLE_COUNT 5
#define DECK_SIZE (ROLE_COUNT * DECK_COPIES_PER_ROLE)

typedef enum {
    ROLE_DUKE = 0,
    ROLE_ASSASSIN = 1,
    ROLE_CAPTAIN = 2,
    ROLE_AMBASSADOR = 3,
    ROLE_CONTESSA = 4
} Role;

typedef enum {
    ACT_INCOME = 1,        // +1 (never blocked / challengeable = no)
    ACT_FOREIGN_AID = 2,   // +2 (blockable by Duke)
    ACT_TAX = 3,           // +3 (claim Duke, challengeable)
    ACT_ASSASSINATE = 4,   // pay 3, target loses 1 (claim Assassin, challengeable; blockable by Contessa)
    ACT_STEAL = 5,         // take up to 2 from target (claim Captain, challengeable; blockable by Captain or Ambassador)
    ACT_EXCHANGE = 6,      // draw 2, keep 2 (claim Ambassador, challengeable)
    ACT_COUP = 7           // pay 7, target loses 1 (forced if coins >= 10)
} ActionKind;

typedef struct {
    char name[MAX_NAME_LEN];
    int coins;
    Role influences[2];      // hidden cards (alive ones)
    int influenceAlive[2];   // 1 = alive, 0 = lost
    int isDead;              // 1 if eliminated
} Player;

typedef struct {
    Player players[MAX_PLAYERS];
    int playerCount;

    Role deck[DECK_SIZE];
    int deckTop;             // index of next card to draw (deckTop points to top-1; we draw deck[--deckTop])

    int current;             // index of current player
    int aliveCount;

    // scratch / flow flags (text UI drives all)
} Game;

/* ==== Core game lifecycle ==== */
void game_init(Game* g, int playerCount);
void game_shuffle_deck(Game* g);
void game_deal(Game* g);
void game_print_public(const Game* g);
int  game_alive(const Player* p);
int  game_influence_count(const Player* p);
int  game_all_but(const Game* g, int exceptIdx, int* outIdx, int cap);
int  game_next_alive_after(const Game* g, int idx);
void game_eliminate_if_zero(Game* g, int idx);

/* ==== Turn / actions ==== */
int  game_force_coup_required(const Game* g, int idx);
int  game_can_afford(const Game* g, int idx, ActionKind act);

void game_do_income(Game* g, int src);
void game_do_foreign_aid(Game* g, int src, int wasBlocked);
void game_do_tax(Game* g, int src);
int  game_do_coup(Game* g, int src, int target);        // returns 1 if applied
int  game_do_assassinate(Game* g, int src, int target); // cost is handled here (returns 1 if goes through to target lose step)
void game_do_steal(Game* g, int src, int target, int wasBlocked);
void game_do_exchange(Game* g, int src, int keepA, int keepB, Role drawnA, Role drawnB);

/* ==== Challenge / reveal helpers ==== */
int  game_player_has_role(const Player* p, Role r);
int  game_reveal_and_replace(Game* g, int playerIdx, Role revealed); // returns 1 if revealed card existed; also replaces it
int  game_lose_influence(Game* g, int playerIdx, int whichSlot);     // 0/1
int  game_choose_alive_slot(const Player* p); // utility for UI auto choice if only one alive


const char* role_name(Role r);

#endif
