#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


static void swap_roles(Role* a, Role* b) { Role t=*a; *a=*b; *b=t; }

const char* role_name(Role r) {
    switch (r) {
        case ROLE_DUKE: return "Bir Bikram";
        case ROLE_ASSASSIN: return "Brohmodoitto";
        case ROLE_CAPTAIN: return "Kalu Dakat";
        case ROLE_AMBASSADOR: return "Petuk Chondro";
        case ROLE_CONTESSA: return "Jiner Badshah";
        default: return "?";
    }
}

int game_alive(const Player* p) { return !p->isDead; }

int game_influence_count(const Player* p) {
    int c = 0;
    for (int i=0;i<2;i++) if (p->influenceAlive[i]) c++;
    return c;
}

int game_all_but(const Game* g, int exceptIdx, int* outIdx, int cap) {
    int c=0;
    for (int i=0;i<g->playerCount && c<cap;i++) {
        if (i==exceptIdx) continue;
        if (g->players[i].isDead) continue;
        outIdx[c++] = i;
    }
    return c;
}

int game_next_alive_after(const Game* g, int idx) {
    int n = g->playerCount;
    for (int i=1;i<=n;i++) {
        int j = (idx + i) % n;
        if (!g->players[j].isDead) return j;
    }
    return idx; // should not happen if at least one alive
}


void game_shuffle_deck(Game* g) {
    for (int i=g->deckTop-1;i>0;i--) {
        int j = rand() % (i+1);
        swap_roles(&g->deck[i], &g->deck[j]);
    }
}

void game_init(Game* g, int playerCount) {
    memset(g, 0, sizeof(*g));
    if (playerCount < 2) playerCount = 2;
    if (playerCount > MAX_PLAYERS) playerCount = MAX_PLAYERS;
    g->playerCount = playerCount;

   
    int k=0;
    for (int r=0;r<ROLE_COUNT;r++) {
        for (int c=0;c<DECK_COPIES_PER_ROLE;c++) {
            g->deck[k++] = (Role)r;
        }
    }
    g->deckTop = DECK_SIZE;
    game_shuffle_deck(g);

    
    for (int i=0;i<playerCount;i++) {
        snprintf(g->players[i].name, MAX_NAME_LEN, "Player%d", i+1);
    }

    game_deal(g);
    g->current = 0;
    g->aliveCount = playerCount;
}

void game_deal(Game* g) {
    for (int i=0;i<g->playerCount;i++) {
        Player* p = &g->players[i];
        p->coins = 2;
        p->isDead = 0;
        for (int s=0;s<2;s++) {
            p->influenceAlive[s] = 1;
            p->influences[s] = g->deck[--g->deckTop];
        }
    }
}

void game_print_public(const Game* g) {
    puts("--------------------------------------------------");
    for (int i=0;i<g->playerCount;i++) {
        const Player* p = &g->players[i];
        printf("[%d] %-12s | coins: %2d | alive cards: %d%s\n",
               i, p->name, p->coins, game_influence_count(p),
               p->isDead ? " (OUT)" : "");
    }
    puts("--------------------------------------------------");
}

/* ===== Influence loss / reveal ===== */

int game_player_has_role(const Player* p, Role r) {
    for (int i=0;i<2;i++) if (p->influenceAlive[i] && p->influences[i] == r) return 1;
    return 0;
}

// Return 1 if revealed existed; also replace revealed with a fresh card (shuffle top into deck for randomness)
int game_reveal_and_replace(Game* g, int playerIdx, Role revealed) {
    Player* p = &g->players[playerIdx];
    for (int i=0;i<2;i++) {
        if (p->influenceAlive[i] && p->influences[i] == revealed) {
            // Put revealed back, shuffle, draw replacement
            g->deck[g->deckTop++] = revealed;
            game_shuffle_deck(g);
            p->influences[i] = g->deck[--g->deckTop];
            return 1;
        }
    }
    return 0;
}

int game_choose_alive_slot(const Player* p) {
    if (p->influenceAlive[0] && !p->influenceAlive[1]) return 0;
    if (p->influenceAlive[1] && !p->influenceAlive[0]) return 1;
    // both alive -> caller should ask the user which to lose
    return -1;
}

int game_lose_influence(Game* g, int playerIdx, int whichSlot) {
    Player* p = &g->players[playerIdx];
    if (whichSlot < 0 || whichSlot > 1) return 0;
    if (!p->influenceAlive[whichSlot]) return 0;

    Role lost = p->influences[whichSlot];
    p->influenceAlive[whichSlot] = 0;

    // Return the lost card to deck (face-down), shuffle
    g->deck[g->deckTop++] = lost;
    game_shuffle_deck(g);

    printf(">>> %s loses an influence.\n", p->name);
    game_eliminate_if_zero(g, playerIdx);
    return 1;
}

void game_eliminate_if_zero(Game* g, int idx) {
    Player* p = &g->players[idx];
    if (game_influence_count(p) == 0 && !p->isDead) {
        p->isDead = 1;
        p->coins = 0;
        g->aliveCount--;
        printf("*** %s is out of the game! ***\n", p->name);
    }
}

/* ===== Turn helpers ===== */

int game_force_coup_required(const Game* g, int idx) {
    return g->players[idx].coins >= 10;
}

int game_can_afford(const Game* g, int idx, ActionKind act) {
    int c = g->players[idx].coins;
    switch (act) {
        case ACT_ASSASSINATE: return c >= 3;
        case ACT_COUP:        return c >= 7;
        default:              return 1;
    }
}

/* ===== Actions (no UI) ===== */

void game_do_income(Game* g, int src) {
    g->players[src].coins += 1;
    printf("%s takes INCOME (+1)\n", g->players[src].name);
}

void game_do_foreign_aid(Game* g, int src, int wasBlocked) {
    if (wasBlocked) {
        printf("%s's FOREIGN AID was BLOCKED by Bir Bikram.\n", g->players[src].name);
        return;
    }
    g->players[src].coins += 2;
    printf("%s takes FOREIGN AID (+2)\n", g->players[src].name);
}

void game_do_tax(Game* g, int src) {
    g->players[src].coins += 3;
    printf("%s (Bir Bikram) takes TAX (+3)\n", g->players[src].name);
}

int game_do_coup(Game* g, int src, int target) {
    Player* s = &g->players[src];
    if (s->coins < 7) return 0;
    s->coins -= 7;
    printf("%s COUPS %s! (pay 7)\n", s->name, g->players[target].name);
    return 1; // caller will make target lose one influence
}

int game_do_assassinate(Game* g, int src, int target) {
    Player* s = &g->players[src];
    if (s->coins < 3) return 0;
    s->coins -= 3;
    printf("%s attempts ASSASSINATE on %s (pay 3)\n", s->name, g->players[target].name);
    return 1; // action proceeds unless blocked/challenged by UI flow
}

void game_do_steal(Game* g, int src, int target, int wasBlocked) {
    if (wasBlocked) {
        printf("%s's STEAL was BLOCKED by Kalu Dakat/Petuk Chondro.\n", g->players[src].name);
        return;
    }
    int can = g->players[target].coins >= 2 ? 2 : g->players[target].coins;
    g->players[target].coins -= can;
    g->players[src].coins += can;
    printf("%s STEALS %d coin(s) from %s.\n", g->players[src].name, can, g->players[target].name);
}

// Exchange logic: you pass in which two positions among (current two alive + drawnA, drawnB) to keep
void game_do_exchange(Game* g, int src, int keepA, int keepB, Role drawnA, Role drawnB) {
    Player* p = &g->players[src];

    // Build temp array of available cards
    Role pool[4];
    int idx=0;
    for (int i=0;i<2;i++) if (p->influenceAlive[i]) pool[idx++] = p->influences[i];
    pool[idx++] = drawnA;
    pool[idx++] = drawnB;

    Role newTwo[2] = { pool[keepA], pool[keepB] };

    // Return all currently alive influences to deck
    for (int i=0;i<2;i++) {
        if (p->influenceAlive[i]) {
            g->deck[g->deckTop++] = p->influences[i];
            p->influenceAlive[i] = 0;
        }
    }

    // Shuffle deck before drawing replacements to keep randomness fair
    game_shuffle_deck(g);

    // Assign the two chosen to player (reset alive markers)
    p->influences[0] = newTwo[0]; p->influenceAlive[0] = 1;
    p->influences[1] = newTwo[1]; p->influenceAlive[1] = 1;

    // Put unkept (from drawn + possibly originals) back into deck and shuffle
    // We already returned originals; we must also return drawnA & drawnB if not kept
    int keepDrawnA = (newTwo[0]==drawnA || newTwo[1]==drawnA);
    int keepDrawnB = (newTwo[0]==drawnB || newTwo[1]==drawnB);
    if (!keepDrawnA) g->deck[g->deckTop++] = drawnA;
    if (!keepDrawnB) g->deck[g->deckTop++] = drawnB;
    game_shuffle_deck(g);

    printf("%s exchanges cards via Petuk Chondro.\n", p->name);
}
