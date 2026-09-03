#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ===== Basic input helpers ===== */

void ui_read_line(char* buf, int cap) {
    if (!fgets(buf, cap, stdin)) { buf[0]=0; return; }
    size_t n = strlen(buf);
    if (n && buf[n-1]=='\n') buf[n-1]=0;
}

int ui_read_int_in_range(int lo, int hi) {
    char line[128];
    int v;
    for (;;) {
        printf("> ");
        ui_read_line(line, sizeof(line));
        if (sscanf(line, "%d", &v) == 1 && v >= lo && v <= hi) return v;
        printf("Please enter a number in [%d..%d].\n", lo, hi);
    }
}

int ui_yes_no(const char* prompt) {
    char line[64];
    for (;;) {
        printf("%s (y/n): ", prompt);
        ui_read_line(line, sizeof(line));
        if (line[0]=='y' || line[0]=='Y') return 1;
        if (line[0]=='n' || line[0]=='N') return 0;
        printf("Please answer y or n.\n");
    }
}

void ui_press_enter(void) {
    printf("(press Enter to continue) ");
    fflush(stdout);
    char tmp[8]; ui_read_line(tmp, sizeof(tmp));
}

/* ===== Player helpers ===== */

void ui_set_player_names(Game* g) {
    printf("Enter number of players (2..%d):\n", MAX_PLAYERS);
    int n = ui_read_int_in_range(2, MAX_PLAYERS);
    game_init(g, n); // also deals cards etc., with default names first

    printf("Enter each player's name:\n");
    for (int i=0;i<g->playerCount;i++) {
        printf("Player %d name: ", i);
        ui_read_line(g->players[i].name, MAX_NAME_LEN);
        if (strlen(g->players[i].name) == 0) {
            snprintf(g->players[i].name, MAX_NAME_LEN, "Player%d", i+1);
        }
    }
    puts("");
}

void ui_show_private_cards(const Game* g, int idx) {
    const Player* p = &g->players[idx];
    printf("Your cards: ");
    for (int s=0;s<2;s++) {
        if (p->influenceAlive[s]) printf("[%s] ", role_name(p->influences[s]));
        else printf("[X] ");
    }
    puts("");
}

int ui_choose_target(const Game* g, int srcIdx) {
    printf("Choose a target:\n");
    for (int i=0;i<g->playerCount;i++) {
        if (i==srcIdx) continue;
        const Player* p = &g->players[i];
        if (!p->isDead) printf("  %d) %s (coins:%d, alive:%d)\n", i, p->name, p->coins, game_influence_count(p));
    }
    return ui_read_int_in_range(0, g->playerCount-1);
}

int ui_choose_influence_to_lose(const Player* p) {
    int a = game_influence_count(p);
    if (a == 0) return -1;
    if (a == 1) return game_choose_alive_slot(p);
    printf("Choose which influence to lose:\n");
    for (int i=0;i<2;i++) if (p->influenceAlive[i])
        printf("  %d) %s\n", i, role_name(p->influences[i]));
    return ui_read_int_in_range(0, 1);
}

// If mustRoleKnown=1, player must reveal that exact role or fail
Role ui_choose_reveal_role_for(Player* p, int mustRoleKnown, Role mustRole) {
    int options[2]; int k=0;
    for (int i=0;i<2;i++) if (p->influenceAlive[i]) options[k++] = i;
    if (k==0) return (Role)ROLE_COUNT;

    printf("Reveal a card");
    if (mustRoleKnown) printf(" (must be %s)", role_name(mustRole));
    printf(" or type 'x' to fail.\n");

    for (int j=0;j<k;j++) {
        int slot = options[j];
        printf("  %d) %s\n", slot, role_name(p->influences[slot]));
    }
    printf("  x) cannot reveal\n");

    char line[32];
    for (;;) {
        printf("> ");
        ui_read_line(line, sizeof(line));
        if (line[0]=='x' || line[0]=='X') return (Role)ROLE_COUNT;
        int choice;
        if (sscanf(line, "%d", &choice)==1) {
            for (int j=0;j<k;j++) if (options[j]==choice) {
                return p->influences[choice];
            }
        }
        printf("Invalid choice.\n");
    }
}

/* ===== Challenge & block orchestration =====
   Rules implemented:
   - Foreign Aid: blockable by Duke (target = none; any opponent may block -> we prompt each alive non-source if they want to block as Duke; if someone blocks, that block can be challenged)
   - Tax: challengeable (claim Duke)
   - Assassinate: challengeable (claim Assassin); blockable by Contessa (target only). Cost 3 on declare. If claim false (challenged successfully), source loses an influence; cost is not refunded. If claim true, assassination proceeds unless blocked; if block is true (Contessa), and block claim is true after challenge, assassination is stopped.
   - Steal: challengeable (Captain); blockable by Captain or Ambassador (target only).
   - Exchange: challengeable (Ambassador). If claim true, draw two and pick two to keep.
   - Coup: forced at 10+ coins, costs 7, cannot be blocked or challenged.
*/

static int prompt_any_challenge(const Game* g, int exceptA, int exceptB, Role claimedRole, int* outChallengerIdx) {
    // iterate alive players (not exceptA/exceptB), first who says yes becomes challenger
    for (int i=0;i<g->playerCount;i++) {
        if (i==exceptA || i==exceptB) continue;
        const Player* p = &g->players[i];
        if (p->isDead) continue;
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "%s: Do you CHALLENGE the claim of %s?",
                 p->name, role_name(claimedRole));
        if (ui_yes_no(prompt)) { *outChallengerIdx = i; return 1; }
    }
    return 0;
}

static int resolve_challenge(Game* g, int challengee, int challenger, Role claimedRole) {
    printf("*** Challenge! %s vs %s (claim: %s)\n",
           g->players[challenger].name, g->players[challengee].name, role_name(claimedRole));

    // Challengee tries to reveal that role
    Role revealed = ui_choose_reveal_role_for(&g->players[challengee], 1, claimedRole);
    if (revealed == claimedRole && game_reveal_and_replace(g, challengee, revealed)) {
        // Challenge failed: challenger loses influence
        printf("Challenge FAILED. %s did reveal %s.\n", g->players[challengee].name, role_name(claimedRole));
        int slot = ui_choose_influence_to_lose(&g->players[challenger]);
        if (slot >= 0) game_lose_influence(g, challenger, slot);
        return 0; // action/block stands
    } else {
        // Challenge succeeded: challengee loses influence, claim is false
        printf("Challenge SUCCEEDED. %s could not reveal %s.\n", g->players[challengee].name, role_name(claimedRole));
        int slot = ui_choose_influence_to_lose(&g->players[challengee]);
        if (slot >= 0) game_lose_influence(g, challengee, slot);
        return 1; // action/block is cancelled
    }
}

static int prompt_target_block_and_resolve(Game* g, int targetIdx, Role blockRole, int* outBlockerIdx) {
    if (g->players[targetIdx].isDead) return 0;
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: Do you BLOCK with %s?", g->players[targetIdx].name, role_name(blockRole));
    if (!ui_yes_no(msg)) return 0;

    // Others may challenge this block claim
    int challenger=-1;
    if (prompt_any_challenge(g, targetIdx, -1, blockRole, &challenger)) {
        int cancelled = resolve_challenge(g, targetIdx, challenger, blockRole);
        if (cancelled) {
            printf("Block was a lie -> Block FAILS.\n");
            return 0;
        } else {
            printf("Block stands after failed challenge.\n");
            *outBlockerIdx = targetIdx;
            return 1;
        }
    }
    // no challenge, block stands
    *outBlockerIdx = targetIdx;
    return 1;
}

static int prompt_anyone_block_duke_and_resolve(Game* g, int sourceIdx, int* outBlockerIdx) {
    // Any other alive player may block Foreign Aid by claiming Duke
    for (int i=0;i<g->playerCount;i++) {
        if (i==sourceIdx) continue;
        if (g->players[i].isDead) continue;
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: Do you BLOCK Foreign Aid with Duke?", g->players[i].name);
        if (ui_yes_no(msg)) {
            // Others may challenge this block
            int challenger=-1;
            if (prompt_any_challenge(g, i, -1, ROLE_DUKE, &challenger)) {
                int cancelled = resolve_challenge(g, i, challenger, ROLE_DUKE);
                if (cancelled) {
                    printf("Duke block was a lie -> block fails.\n");
                    return 0;
                } else {
                    printf("Duke block stands after failed challenge.\n");
                    *outBlockerIdx = i;
                    return 1;
                }
            }
            *outBlockerIdx = i;
            return 1;
        }
    }
    return 0;
}

/* ===== Main loop driver ===== */

static void do_turn(Game* g) {
    int i = g->current;
    Player* me = &g->players[i];
    if (me->isDead) return;

    printf("\n======================= TURN: %s =======================\n", me->name);
    game_print_public(g);
    ui_show_private_cards(g, i);

    // Forced coup at 10+
    if (game_force_coup_required(g, i)) {
        printf("Rule: You have 10+ coins, you MUST COUP.\n");
        int target = ui_choose_target(g, i);
        if (target == i || g->players[target].isDead) {
            printf("Invalid target.\n");
        } else if (game_do_coup(g, i, target)) {
            int slot = ui_choose_influence_to_lose(&g->players[target]);
            if (slot >= 0) game_lose_influence(g, target, slot);
        }
        g->current = game_next_alive_after(g, g->current);
        return;
    }

    // Choose action
    puts("Choose action:");
    puts(" 1) Income (+1)");
    puts(" 2) Foreign Aid (+2) [Duke can block]");
    puts(" 3) Tax (claim Duke: +3) [challengeable]");
    puts(" 4) Assassinate (claim Assassin, pay 3, target loses 1) [challengeable, Contessa can block]");
    puts(" 5) Steal (claim Captain, take up to 2 from target) [challengeable, target can block by Captain or Ambassador]");
    puts(" 6) Exchange (claim Ambassador) [challengeable]");
    puts(" 7) Coup (pay 7, target loses 1)");
    int act = ui_read_int_in_range(1,7);

    // Affordability check
    if (!game_can_afford(g, i, (ActionKind)act)) {
        printf("You cannot afford that action.\n");
        return; // lose your turn if you try something illegal
    }

    int target = -1, blocker = -1, challenger = -1;

    switch ((ActionKind)act) {
        case ACT_INCOME:
            game_do_income(g, i);
            break;

        case ACT_FOREIGN_AID: {
            // offer block by any opponent as Duke
            if (prompt_anyone_block_duke_and_resolve(g, i, &blocker)) {
                game_do_foreign_aid(g, i, 1);
            } else {
                game_do_foreign_aid(g, i, 0);
            }
        } break;

        case ACT_TAX: {
            // claim Duke -> others may challenge
            if (prompt_any_challenge(g, i, -1, ROLE_DUKE, &challenger)) {
                int cancelled = resolve_challenge(g, i, challenger, ROLE_DUKE);
                if (!cancelled) { // challenge failed, action stands
                    game_do_tax(g, i);
                } else {
                    printf("TAX cancelled due to successful challenge.\n");
                }
            } else {
                game_do_tax(g, i);
            }
        } break;

        case ACT_ASSASSINATE: {
            target = ui_choose_target(g, i);
            if (target == i || g->players[target].isDead) { puts("Invalid target."); break; }

            // First: claim Assassin can be challenged
            if (prompt_any_challenge(g, i, target, ROLE_ASSASSIN, &challenger)) {
                int cancelled = resolve_challenge(g, i, challenger, ROLE_ASSASSIN);
                if (cancelled) {
                    // source lied: loses an influence (handled in resolve), assassination does NOT proceed,
                    // cost stays spent (standard variant keeps cost spent).
                    break;
                } else {
                    // claim true: proceed unless target blocks with Contessa
                }
            }
            // pay and attempt (cost handled inside)
            if (!game_do_assassinate(g, i, target)) { puts("Not enough coins."); break; }

            // Target may block with Contessa (and that block can be challenged)
            if (prompt_target_block_and_resolve(g, target, ROLE_CONTESSA, &blocker)) {
                printf("Assassination BLOCKED by %s.\n", g->players[target].name);
            } else {
                // target loses an influence
                int slot = ui_choose_influence_to_lose(&g->players[target]);
                if (slot >= 0) game_lose_influence(g, target, slot);
            }
        } break;

        case ACT_STEAL: {
            target = ui_choose_target(g, i);
            if (target == i || g->players[target].isDead) { puts("Invalid target."); break; }

            // Claim Captain can be challenged
            if (prompt_any_challenge(g, i, target, ROLE_CAPTAIN, &challenger)) {
                int cancelled = resolve_challenge(g, i, challenger, ROLE_CAPTAIN);
                if (cancelled) {
                    printf("STEAL cancelled due to successful challenge.\n");
                    break;
                }
            }
            // Target may block with Captain OR Ambassador (either is valid)
            // We ask if they block; if yes, they must pick which role they claim.
            if (!g->players[target].isDead && ui_yes_no("Target: Do you BLOCK the steal?")) {
                // choose role to claim for block
                puts("Choose block role:");
                puts(" 1) Captain");
                puts(" 2) Ambassador");
                int br = ui_read_int_in_range(1,2);
                Role blockRole = (br==1)?ROLE_CAPTAIN:ROLE_AMBASSADOR;

                int chal=-1;
                if (prompt_any_challenge(g, target, i, blockRole, &chal)) {
                    int cancelled = resolve_challenge(g, target, chal, blockRole);
                    if (cancelled) {
                        // block was a lie -> steal goes through
                        game_do_steal(g, i, target, 0);
                    } else {
                        // block stands
                        game_do_steal(g, i, target, 1);
                    }
                } else {
                    // nobody challenged -> block stands
                    game_do_steal(g, i, target, 1);
                }
            } else {
                // no block -> proceed
                game_do_steal(g, i, target, 0);
            }
        } break;

        case ACT_EXCHANGE: {
            // Claim Ambassador can be challenged
            if (prompt_any_challenge(g, i, -1, ROLE_AMBASSADOR, &challenger)) {
                int cancelled = resolve_challenge(g, i, challenger, ROLE_AMBASSADOR);
                if (cancelled) { printf("EXCHANGE cancelled by successful challenge.\n"); break; }
            }

            // Draw two from deck
            if (g->deckTop < 2) { printf("Deck too small; reshuffling.\n"); game_shuffle_deck(g); }
            Role dA = g->deck[--g->deckTop];
            Role dB = g->deck[--g->deckTop];

            // Build choice pool = current alive influences + drawn
            Role pool[4]; int mapSlotToPoolIdx[2]; int aliveIdx=0;
            for (int s=0;s<2;s++) if (me->influenceAlive[s]) {
                pool[aliveIdx] = me->influences[s];
                mapSlotToPoolIdx[s] = aliveIdx;
                aliveIdx++;
            }
            pool[aliveIdx++] = dA;
            pool[aliveIdx++] = dB;

            printf("You drew: [%s] and [%s]\n", role_name(dA), role_name(dB));
            printf("Choose TWO to keep from this list:\n");
            for (int t=0;t<4;t++) printf(" %d) %s\n", t, role_name(pool[t]));

            int k1 = ui_read_int_in_range(0,3);
            int k2 = ui_read_int_in_range(0,3);
            while (k2 == k1) {
                printf("Pick two distinct choices.\n");
                k2 = ui_read_int_in_range(0,3);
            }
            game_do_exchange(g, i, k1, k2, dA, dB);
        } break;

        case ACT_COUP: {
            target = ui_choose_target(g, i);
            if (target == i || g->players[target].isDead) { puts("Invalid target."); break; }
            if (game_do_coup(g, i, target)) {
                int slot = ui_choose_influence_to_lose(&g->players[target]);
                if (slot >= 0) game_lose_influence(g, target, slot);
            } else {
                puts("Not enough coins to coup.");
            }
        } break;
    }

    // Advance turn
    g->current = game_next_alive_after(g, g->current);
}

void ui_run_game_loop(Game* g) {
    while (g->aliveCount > 1) {
        do_turn(g);
    }
    // winner
    for (int i=0;i<g->playerCount;i++) if (!g->players[i].isDead) {
        printf("\n===== WINNER: %s =====\n", g->players[i].name);
        break;
    }
}
