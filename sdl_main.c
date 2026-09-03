#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"

#define SCREEN_WIDTH 1600
#define SCREEN_HEIGHT 900
#define CARD_WIDTH 140
#define CARD_HEIGHT 196
#define MAX_LOGS 12

typedef enum {
    STATE_MENU,
    STATE_PASS_DEVICE,
    STATE_WAIT_FOR_ACTION,
    STATE_CHOOSE_TARGET,
    STATE_CHALLENGE_PROMPT,
    STATE_CHOOSE_CHALLENGER,
    STATE_REVEAL_FOR_CHALLENGE,
    STATE_BLOCK_PROMPT,          
    STATE_ANYONE_BLOCK_PROMPT,   
    STATE_CHOOSE_BLOCK_ROLE,     
    STATE_CHOOSE_BLOCKER,        
    STATE_BLOCK_CHALLENGE_PROMPT,
    STATE_CHOOSE_INFLUENCE_TO_LOSE,
    STATE_EXCHANGE_CHOICE,
    STATE_GAME_OVER
} UIState;

typedef enum {
    NEXT_RESUME_ACTION,
    NEXT_RESUME_ACTION_BLOCKED,
    NEXT_ADVANCE_TURN,
    NEXT_TARGET_BLOCK_PROMPT,
    NEXT_DO_EXCHANGE_DRAW,
    NEXT_RESOLVE_ASSASSINATION_TARGET_LOSS
} NextState;

typedef struct {
    Game game;
    UIState state;
    NextState nextStateAfterLoseInfluence;
    
    int activeUIPlayer;
    
    ActionKind pendingAction;
    int pendingTarget;
    Role pendingRole; 
    
    int isChallengingBlock;
    int blocker;
    Role pendingBlockRole;
    int challenger;
    
    Role exchangePool[4];
    int exchangePoolCount;
    int exchangeKeep[2];
    int exchangeKeepCount;
    Role exchangeDrawn[2];
    
    char logs[MAX_LOGS][128];
    int logCount;
    
    int showLog;
    int showHelp;
    
    SDL_Texture* texCardBack;
    SDL_Texture* texRoles[ROLE_COUNT];
    SDL_Texture* texCoin;
    SDL_Texture* texMenuBg;
    SDL_Texture* texLogo;
    SDL_Texture* texManual;
    TTF_Font* font;
    TTF_Font* fontLarge;
    
    int running;
} AppContext;

AppContext app;

void add_log(const char* msg) {
    if (app.logCount < MAX_LOGS) {
        strcpy(app.logs[app.logCount++], msg);
    } else {
        for (int i=0; i<MAX_LOGS-1; i++) strcpy(app.logs[i], app.logs[i+1]);
        strcpy(app.logs[MAX_LOGS-1], msg);
    }
    printf("LOG: %s\n", msg);
}

void advance_turn() {
    if (app.game.aliveCount <= 1) {
        app.state = STATE_GAME_OVER;
        return;
    }
    game_eliminate_if_zero(&app.game, app.game.current);
    app.game.current = game_next_alive_after(&app.game, app.game.current);
    app.activeUIPlayer = app.game.current;
    
    if (app.game.aliveCount <= 1) {
        app.state = STATE_GAME_OVER;
    } else {
        app.state = STATE_PASS_DEVICE;
    }
}

void execute_pending_action(int blocked) {
    int src = app.game.current;
    int tgt = app.pendingTarget;
    
    if (blocked) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Action blocked by %s.", app.game.players[app.blocker].name);
        add_log(msg);
        advance_turn();
        return;
    }
    
    char msg[128];
    switch (app.pendingAction) {
        case ACT_INCOME:
            game_do_income(&app.game, src);
            add_log("Took Income.");
            advance_turn();
            break;
        case ACT_FOREIGN_AID:
            game_do_foreign_aid(&app.game, src, 0);
            add_log("Took Foreign Aid.");
            advance_turn();
            break;
        case ACT_TAX:
            game_do_tax(&app.game, src);
            add_log("Collected Tax.");
            advance_turn();
            break;
        case ACT_ASSASSINATE:
            if (!game_do_assassinate(&app.game, src, tgt)) {
                add_log("Not enough coins to assassinate!");
                advance_turn();
            } else {
                snprintf(msg, sizeof(msg), "%s assassinated %s.", app.game.players[src].name, app.game.players[tgt].name);
                add_log(msg);
                app.activeUIPlayer = tgt;
                app.nextStateAfterLoseInfluence = NEXT_ADVANCE_TURN;
                app.state = STATE_CHOOSE_INFLUENCE_TO_LOSE;
            }
            break;
        case ACT_STEAL:
            game_do_steal(&app.game, src, tgt, 0);
            snprintf(msg, sizeof(msg), "%s stole from %s.", app.game.players[src].name, app.game.players[tgt].name);
            add_log(msg);
            advance_turn();
            break;
        case ACT_COUP:
            if (!game_do_coup(&app.game, src, tgt)) {
                add_log("Not enough coins to coup!");
                advance_turn();
            } else {
                snprintf(msg, sizeof(msg), "%s couped %s.", app.game.players[src].name, app.game.players[tgt].name);
                add_log(msg);
                app.activeUIPlayer = tgt;
                app.nextStateAfterLoseInfluence = NEXT_ADVANCE_TURN;
                app.state = STATE_CHOOSE_INFLUENCE_TO_LOSE;
            }
            break;
        case ACT_EXCHANGE:
            // handled via do_exchange_draw
            break;
    }
}

void do_exchange_draw() {
    if (app.game.deckTop < 2) {
        game_shuffle_deck(&app.game);
    }
    app.exchangeDrawn[0] = app.game.deck[--app.game.deckTop];
    app.exchangeDrawn[1] = app.game.deck[--app.game.deckTop];
    
    app.exchangePoolCount = 0;
    for(int s=0; s<2; s++) {
        if(app.game.players[app.game.current].influenceAlive[s]) {
            app.exchangePool[app.exchangePoolCount++] = app.game.players[app.game.current].influences[s];
        }
    }
    app.exchangePool[app.exchangePoolCount++] = app.exchangeDrawn[0];
    app.exchangePool[app.exchangePoolCount++] = app.exchangeDrawn[1];
    
    app.exchangeKeepCount = 0;
    app.activeUIPlayer = app.game.current;
    app.state = STATE_EXCHANGE_CHOICE;
}

void render_text(SDL_Renderer* ren, const char* text, int x, int y, TTF_Font* font, SDL_Color color) {
    SDL_Surface* srf = TTF_RenderText_Blended(font, text, color);
    if (srf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, srf);
        SDL_Rect r = { x, y, srf->w, srf->h };
        SDL_RenderCopy(ren, tex, NULL, &r);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(srf);
    }
}

int render_button(SDL_Renderer* ren, const char* text, SDL_Rect rect, int is_hover, int is_disabled) {
    if (is_disabled) {
        SDL_SetRenderDrawColor(ren, 100, 100, 100, 255);
    } else if (is_hover) {
        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
    } else {
        SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
    }
    SDL_RenderFillRect(ren, &rect);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderDrawRect(ren, &rect);
    
    SDL_Color tc = {0,0,0,255};
    if (is_disabled) tc = (SDL_Color){50,50,50,255};
    render_text(ren, text, rect.x + 10, rect.y + 10, app.font, tc);
    
    return (!is_disabled && is_hover);
}

void render_card(SDL_Renderer* ren, Role role, int x, int y, int face_up, int is_dead) {
    SDL_Rect r = { x, y, CARD_WIDTH, CARD_HEIGHT };
    if (is_dead) {
        SDL_SetTextureColorMod(app.texRoles[role], 100, 100, 100);
        SDL_RenderCopy(ren, app.texRoles[role], NULL, &r);
        SDL_SetTextureColorMod(app.texRoles[role], 255, 255, 255); // reset
    } else if (face_up) {
        SDL_RenderCopy(ren, app.texRoles[role], NULL, &r);
    } else {
        SDL_RenderCopy(ren, app.texCardBack, NULL, &r);
    }
}

void get_player_pos(int pidx, int myidx, int count, int* px, int* py) {
    int cx = SCREEN_WIDTH/2;
    int cy = SCREEN_HEIGHT/2;
    if (pidx == myidx) {
        *px = cx; *py = cy + 140;
    } else {
        int rel = (pidx - myidx + count) % count;
        if (count == 2) {
            *px = cx; *py = cy - 300;
        } else if (count == 3) {
            if (rel==1) { *px = cx - 400; *py = cy - 100; }
            else        { *px = cx + 400; *py = cy - 100; }
        } else if (count == 4) {
            if (rel==1)      { *px = cx - 450; *py = cy - 50; }
            else if (rel==2) { *px = cx;       *py = cy - 320; }
            else             { *px = cx + 450; *py = cy - 50; }
        } else {
            // fallback generic circle
            float angle = 3.14159f/2.0f + (rel * 2.0f * 3.14159f / count);
            *px = cx + (int)(400 * cos(angle));
            *py = cy - (int)(300 * sin(angle));
        }
    }
}

void draw_overlay_prompt(SDL_Renderer* ren, const char* prompt, const char* btn1, const char* btn2, int mx, int my, int* clicked1, int* clicked2, int m_click) {
    SDL_Rect bg = { SCREEN_WIDTH/2 - 250, SCREEN_HEIGHT/2 - 100, 500, 200 };
    SDL_SetRenderDrawColor(ren, 40, 40, 40, 240);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_RenderFillRect(ren, &bg);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderDrawRect(ren, &bg);
    
    SDL_Color c = {255,255,255,255};
    render_text(ren, prompt, bg.x + 20, bg.y + 40, app.fontLarge, c);
    
    if (btn1) {
        SDL_Rect r1 = { bg.x + 50, bg.y + 120, 150, 50 };
        int h1 = (mx >= r1.x && mx <= r1.x+r1.w && my >= r1.y && my <= r1.y+r1.h);
        if (render_button(ren, btn1, r1, h1, 0) && m_click) *clicked1 = 1;
    }
    if (btn2) {
        SDL_Rect r2 = { bg.x + 300, bg.y + 120, 150, 50 };
        int h2 = (mx >= r2.x && mx <= r2.x+r2.w && my >= r2.y && my <= r2.y+r2.h);
        if (render_button(ren, btn2, r2, h2, 0) && m_click) *clicked2 = 1;
    }
}

void draw_game_screen(SDL_Renderer* ren, int mx, int my, int m_click) {
    SDL_SetRenderDrawColor(ren, 20, 30, 20, 255);
    SDL_RenderClear(ren);

    // Menu block (visual only)
    int lw = 360, lh = 120;
    if (app.texLogo) {
        SDL_QueryTexture(app.texLogo, NULL, NULL, &lw, &lh);
    }
    int renderW = 360;
    int renderH = (lw > 0) ? (lh * renderW) / lw : 120;
    SDL_Rect logoRect = {20, 20, renderW, renderH};
    SDL_RenderCopy(ren, app.texLogo, NULL, &logoRect);
    SDL_Color tcWhite = {255,255,255,255};
    
    int bx = 30;
    int by = 20 + renderH + 10;
    SDL_Rect rHome = {bx, by, 70, 30};
    int hHome = (mx>=rHome.x && mx<=rHome.x+rHome.w && my>=rHome.y && my<=rHome.y+rHome.h);
    if (render_button(ren, "Home", rHome, hHome, 0) && m_click) {
        app.state = STATE_MENU;
    }
    bx += 80;
    
    SDL_Rect rLog = {bx, by, 60, 30};
    int hLog = (mx>=rLog.x && mx<=rLog.x+rLog.w && my>=rLog.y && my<=rLog.y+rLog.h);
    if (render_button(ren, "Log", rLog, hLog, 0) && m_click) {
        app.showLog = !app.showLog;
    }
    bx += 70;
    
    SDL_Rect rHelp = {bx, by, 60, 30};
    int hHelp = (mx>=rHelp.x && mx<=rHelp.x+rHelp.w && my>=rHelp.y && my<=rHelp.y+rHelp.h);
    if (render_button(ren, "Help", rHelp, hHelp, 0) && m_click) {
        app.showHelp = !app.showHelp;
    }
    bx += 70;
    
    SDL_Rect rSet = {bx, by, 80, 30};
    int hSet = (mx>=rSet.x && mx<=rSet.x+rSet.w && my>=rSet.y && my<=rSet.y+rSet.h);
    if (render_button(ren, "Settings", rSet, hSet, 0) && m_click) {
        // Settings clicked (placeholder)
    }

    // Log block
    if (app.showLog) {
        SDL_Rect logRect = {SCREEN_WIDTH - 320, 20, 300, 300};
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 50, 150, 255, 200);
        SDL_RenderFillRect(ren, &logRect);
        render_text(ren, "Game Log", logRect.x + 100, logRect.y + 10, app.fontLarge, tcWhite);
        for(int i=0; i<app.logCount; i++) {
            render_text(ren, app.logs[i], logRect.x + 10, logRect.y + 50 + i*20, app.font, tcWhite);
        }
    }
    


    if (app.state == STATE_MENU) {
        // Draw the poster background image
        int bg_w = (int)(SCREEN_HEIGHT * (1166.0f / 1654.0f)); // rough aspect ratio
        SDL_Rect bgR = { SCREEN_WIDTH/2 - bg_w/2, 0, bg_w, SCREEN_HEIGHT };
        SDL_RenderCopy(ren, app.texMenuBg, NULL, &bgR);
        
        SDL_Rect r = { SCREEN_WIDTH/2 - 100, SCREEN_HEIGHT - 100, 200, 50 };
        int h = (mx>=r.x && mx<=r.x+r.w && my>=r.y && my<=r.y+r.h);
        if (render_button(ren, "Start Game", r, h, 0) && m_click) {
            game_init(&app.game, 4);
            add_log("Game started.");
            app.game.current = 0;
            app.activeUIPlayer = 0;
            app.state = STATE_PASS_DEVICE;
        }
        SDL_RenderPresent(ren);
        return;
    }

    if (app.state == STATE_GAME_OVER) {
        char msg[128] = "Game Over!";
        for(int i=0;i<app.game.playerCount;i++) {
            if(!app.game.players[i].isDead) snprintf(msg, sizeof(msg), "%s Wins!", app.game.players[i].name);
        }
        int c1=0, c2=0;
        draw_overlay_prompt(ren, msg, "Exit", NULL, mx, my, &c1, &c2, m_click);
        if (c1) app.running = 0;
        SDL_RenderPresent(ren);
        return;
    }

    // Draw players
    for (int i=0; i<app.game.playerCount; i++) {
        Player* p = &app.game.players[i];
        int px, py;
        get_player_pos(i, app.activeUIPlayer, app.game.playerCount, &px, &py);
        
        // Target selection highlight
        if (app.state == STATE_CHOOSE_TARGET && i != app.activeUIPlayer && !p->isDead) {
            SDL_Rect tr = { px - CARD_WIDTH - 15, py - 30, CARD_WIDTH*2 + 30, CARD_HEIGHT + 70 };
            int h = (mx>=tr.x && mx<=tr.x+tr.w && my>=tr.y && my<=tr.y+tr.h);
            SDL_SetRenderDrawColor(ren, h? 255:100, h? 255:100, 0, 100);
            SDL_RenderFillRect(ren, &tr);
            if (h && m_click) {
                app.pendingTarget = i;
                if (app.pendingAction == ACT_ASSASSINATE) {
                    app.pendingRole = ROLE_ASSASSIN;
                    app.state = STATE_CHALLENGE_PROMPT;
                } else if (app.pendingAction == ACT_STEAL) {
                    app.pendingRole = ROLE_CAPTAIN;
                    app.state = STATE_CHALLENGE_PROMPT;
                } else if (app.pendingAction == ACT_COUP) {
                    execute_pending_action(0);
                }
            }
        }
        
        render_text(ren, p->name, px - 30, py - 25, app.font, tcWhite);
        
        // Coins
        SDL_Rect cr = { px - 15, py + CARD_HEIGHT + 5, 30, 30 };
        SDL_RenderCopy(ren, app.texCoin, NULL, &cr);
        char cbuf[16]; snprintf(cbuf, sizeof(cbuf), "%d", p->coins);
        render_text(ren, cbuf, px + 20, py + CARD_HEIGHT + 10, app.font, tcWhite);
        
        // Cards
        int faceUp = (i == app.activeUIPlayer && app.state != STATE_PASS_DEVICE);
        for (int s=0; s<2; s++) {
            int cx = px - CARD_WIDTH - 5 + (s * (CARD_WIDTH + 10));
            int cy = py;
            if (p->influenceAlive[s] || faceUp) { // if dead, always show role
                render_card(ren, p->influences[s], cx, cy, (faceUp || !p->influenceAlive[s]), !p->influenceAlive[s]);
            } else {
                render_card(ren, 0, cx, cy, 0, 0); // back
            }
            
            // Interaction for Revealing / Losing influence
            if (!p->isDead && p->influenceAlive[s] && i == app.activeUIPlayer) {
                SDL_Rect crr = { cx, cy, CARD_WIDTH, CARD_HEIGHT };
                int ch = (mx>=crr.x && mx<=crr.x+crr.w && my>=crr.y && my<=crr.y+crr.h);
                if (ch) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 0, 100);
                    SDL_RenderFillRect(ren, &crr);
                    if (m_click) {
                        if (app.state == STATE_REVEAL_FOR_CHALLENGE) {
                            Role revealed = p->influences[s];
                            Role expected = app.isChallengingBlock ? app.pendingBlockRole : app.pendingRole;
                            
                            char lmsg[128];
                            snprintf(lmsg, sizeof(lmsg), "%s revealed %s.", p->name, role_name(revealed));
                            add_log(lmsg);
                            
                            if (revealed == expected) { // Success
                                game_reveal_and_replace(&app.game, i, revealed);
                                app.activeUIPlayer = app.challenger;
                                app.state = STATE_CHOOSE_INFLUENCE_TO_LOSE;
                                app.nextStateAfterLoseInfluence = app.isChallengingBlock ? NEXT_RESUME_ACTION_BLOCKED : (app.pendingAction == ACT_ASSASSINATE || app.pendingAction == ACT_STEAL ? NEXT_TARGET_BLOCK_PROMPT : (app.pendingAction == ACT_EXCHANGE ? NEXT_DO_EXCHANGE_DRAW : NEXT_RESUME_ACTION));
                            } else { // Failed bluff
                                game_lose_influence(&app.game, i, s); // Lose this card
                                if (app.isChallengingBlock) {
                                    app.nextStateAfterLoseInfluence = NEXT_RESUME_ACTION; // block failed, so resume normal action
                                    // wait, the player who bluffed block already lost, we just transition
                                    app.state = STATE_PASS_DEVICE; // bounce out to process
                                    // Actually we can just manually trigger it:
                                    execute_pending_action(0);
                                } else {
                                    advance_turn(); // claim failed
                                }
                            }
                        } else if (app.state == STATE_CHOOSE_INFLUENCE_TO_LOSE) {
                            game_lose_influence(&app.game, i, s);
                            char lmsg[128]; snprintf(lmsg, sizeof(lmsg), "%s lost an influence.", p->name); add_log(lmsg);
                            
                            switch (app.nextStateAfterLoseInfluence) {
                                case NEXT_RESUME_ACTION: execute_pending_action(0); break;
                                case NEXT_RESUME_ACTION_BLOCKED: execute_pending_action(1); break;
                                case NEXT_ADVANCE_TURN: advance_turn(); break;
                                case NEXT_TARGET_BLOCK_PROMPT: app.state = STATE_BLOCK_PROMPT; break;
                                case NEXT_DO_EXCHANGE_DRAW: do_exchange_draw(); break;
                                case NEXT_RESOLVE_ASSASSINATION_TARGET_LOSS:
                                    // They lost 1 for bluffing contessa, now lose 2nd for assassination
                                    if (game_influence_count(p) > 0) {
                                        app.state = STATE_CHOOSE_INFLUENCE_TO_LOSE;
                                        app.nextStateAfterLoseInfluence = NEXT_ADVANCE_TURN;
                                    } else {
                                        advance_turn();
                                    }
                                    break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (app.state == STATE_PASS_DEVICE) {
        int c1=0, c2=0;
        char msg[128]; snprintf(msg, sizeof(msg), "Pass device to %s", app.game.players[app.activeUIPlayer].name);
        draw_overlay_prompt(ren, msg, "Continue", NULL, mx, my, &c1, &c2, m_click);
        if (c1) {
            // we were passing it to them to start their turn
            app.state = STATE_WAIT_FOR_ACTION;
        }
    }
    else if (app.state == STATE_WAIT_FOR_ACTION) {
        int cx = SCREEN_WIDTH/2;
        int btnY = SCREEN_HEIGHT - 80;
        int bw = 100;
        
        Player* cur = &app.game.players[app.game.current];
        int forced_coup = (cur->coins >= 10);
        
        struct { char* name; ActionKind act; int cost; } acts[] = {
            {"Income", ACT_INCOME, 0},
            {"Foreign Aid", ACT_FOREIGN_AID, 0},
            {"Tax", ACT_TAX, 0},
            {"Assassinate", ACT_ASSASSINATE, 3},
            {"Steal", ACT_STEAL, 0},
            {"Exchange", ACT_EXCHANGE, 0},
            {"Coup", ACT_COUP, 7}
        };
        
        int startX = cx - (7 * (bw+10))/2;
        for (int i=0; i<7; i++) {
            SDL_Rect r = { startX + i*(bw+10), btnY, bw, 50 };
            int disabled = (cur->coins < acts[i].cost) || (forced_coup && acts[i].act != ACT_COUP);
            int h = (mx>=r.x && mx<=r.x+r.w && my>=r.y && my<=r.y+r.h);
            if (render_button(ren, acts[i].name, r, h, disabled) && m_click) {
                app.pendingAction = acts[i].act;
                if (acts[i].act == ACT_INCOME || acts[i].act == ACT_FOREIGN_AID || acts[i].act == ACT_TAX || acts[i].act == ACT_EXCHANGE) {
                    if (acts[i].act == ACT_INCOME) {
                        execute_pending_action(0);
                    } else if (acts[i].act == ACT_FOREIGN_AID) {
                        app.state = STATE_ANYONE_BLOCK_PROMPT;
                    } else if (acts[i].act == ACT_TAX) {
                        app.pendingRole = ROLE_DUKE;
                        app.state = STATE_CHALLENGE_PROMPT;
                    } else if (acts[i].act == ACT_EXCHANGE) {
                        app.pendingRole = ROLE_AMBASSADOR;
                        app.state = STATE_CHALLENGE_PROMPT;
                    }
                } else {
                    app.state = STATE_CHOOSE_TARGET;
                }
            }
        }
    }
    else if (app.state == STATE_CHALLENGE_PROMPT) {
        int c1=0, c2=0;
        char msg[128]; snprintf(msg, sizeof(msg), "%s claimed %s. Challenge?", app.game.players[app.game.current].name, role_name(app.pendingRole));
        draw_overlay_prompt(ren, msg, "Challenge", "Pass", mx, my, &c1, &c2, m_click);
        if (c1) { app.state = STATE_CHOOSE_CHALLENGER; app.isChallengingBlock = 0; }
        if (c2) {
            if (app.pendingAction == ACT_TAX) execute_pending_action(0);
            else if (app.pendingAction == ACT_EXCHANGE) do_exchange_draw();
            else if (app.pendingAction == ACT_ASSASSINATE || app.pendingAction == ACT_STEAL) app.state = STATE_BLOCK_PROMPT;
        }
    }
    else if (app.state == STATE_BLOCK_CHALLENGE_PROMPT) {
        int c1=0, c2=0;
        char msg[128]; snprintf(msg, sizeof(msg), "%s blocks with %s. Challenge?", app.game.players[app.blocker].name, role_name(app.pendingBlockRole));
        draw_overlay_prompt(ren, msg, "Challenge", "Pass", mx, my, &c1, &c2, m_click);
        if (c1) { app.state = STATE_CHOOSE_CHALLENGER; app.isChallengingBlock = 1; }
        if (c2) { execute_pending_action(1); } // Block succeeds
    }
    else if (app.state == STATE_CHOOSE_CHALLENGER) {
        SDL_Rect bg = { SCREEN_WIDTH/2 - 250, SCREEN_HEIGHT/2 - 100, 500, 200 };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 240);
        SDL_RenderFillRect(ren, &bg);
        render_text(ren, "Who is challenging?", bg.x + 20, bg.y + 20, app.fontLarge, tcWhite);
        
        int claimer = app.isChallengingBlock ? app.blocker : app.game.current;
        int bx = bg.x + 20;
        for (int i=0; i<app.game.playerCount; i++) {
            if (i != claimer && !app.game.players[i].isDead) {
                SDL_Rect r = { bx, bg.y + 80, 100, 50 };
                int h = (mx>=r.x && mx<=r.x+r.w && my>=r.y && my<=r.y+r.h);
                if (render_button(ren, app.game.players[i].name, r, h, 0) && m_click) {
                    app.challenger = i;
                    app.activeUIPlayer = claimer;
                    app.state = STATE_REVEAL_FOR_CHALLENGE;
                }
                bx += 110;
            }
        }
    }
    else if (app.state == STATE_REVEAL_FOR_CHALLENGE) {
        int c1=0, c2=0;
        draw_overlay_prompt(ren, "You were challenged! Click a card to reveal.", NULL, NULL, mx, my, &c1, &c2, m_click);
    }
    else if (app.state == STATE_BLOCK_PROMPT) {
        int c1=0, c2=0;
        char msg[128]; snprintf(msg, sizeof(msg), "%s, do you block?", app.game.players[app.pendingTarget].name);
        draw_overlay_prompt(ren, msg, "Block", "Pass", mx, my, &c1, &c2, m_click);
        if (c1) {
            app.blocker = app.pendingTarget;
            if (app.pendingAction == ACT_ASSASSINATE) {
                app.pendingBlockRole = ROLE_CONTESSA;
                app.state = STATE_BLOCK_CHALLENGE_PROMPT;
            } else if (app.pendingAction == ACT_STEAL) {
                app.state = STATE_CHOOSE_BLOCK_ROLE;
            }
        }
        if (c2) {
            execute_pending_action(0);
        }
    }
    else if (app.state == STATE_ANYONE_BLOCK_PROMPT) {
        int c1=0, c2=0;
        draw_overlay_prompt(ren, "Does anyone block with Bir Bikram?", "Yes", "No", mx, my, &c1, &c2, m_click);
        if (c1) app.state = STATE_CHOOSE_BLOCKER;
        if (c2) execute_pending_action(0);
    }
    else if (app.state == STATE_CHOOSE_BLOCKER) {
        SDL_Rect bg = { SCREEN_WIDTH/2 - 250, SCREEN_HEIGHT/2 - 100, 500, 200 };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 240);
        SDL_RenderFillRect(ren, &bg);
        render_text(ren, "Who is blocking?", bg.x + 20, bg.y + 20, app.fontLarge, tcWhite);
        int bx = bg.x + 20;
        for (int i=0; i<app.game.playerCount; i++) {
            if (i != app.game.current && !app.game.players[i].isDead) {
                SDL_Rect r = { bx, bg.y + 80, 100, 50 };
                int h = (mx>=r.x && mx<=r.x+r.w && my>=r.y && my<=r.y+r.h);
                if (render_button(ren, app.game.players[i].name, r, h, 0) && m_click) {
                    app.blocker = i;
                    app.pendingBlockRole = ROLE_DUKE;
                    app.state = STATE_BLOCK_CHALLENGE_PROMPT;
                }
                bx += 110;
            }
        }
    }
    else if (app.state == STATE_CHOOSE_BLOCK_ROLE) {
        int c1=0, c2=0;
        draw_overlay_prompt(ren, "Block with which role?", "Kalu Dakat", "Petuk Chondro", mx, my, &c1, &c2, m_click);
        if (c1) { app.pendingBlockRole = ROLE_CAPTAIN; app.state = STATE_BLOCK_CHALLENGE_PROMPT; }
        if (c2) { app.pendingBlockRole = ROLE_AMBASSADOR; app.state = STATE_BLOCK_CHALLENGE_PROMPT; }
    }
    else if (app.state == STATE_CHOOSE_INFLUENCE_TO_LOSE) {
        int c1=0,c2=0;
        char msg[128]; snprintf(msg, sizeof(msg), "%s, click a card to discard.", app.game.players[app.activeUIPlayer].name);
        draw_overlay_prompt(ren, msg, NULL, NULL, mx, my, &c1, &c2, m_click);
    }
    else if (app.state == STATE_EXCHANGE_CHOICE) {
        SDL_Rect bg = { SCREEN_WIDTH/2 - 400, SCREEN_HEIGHT/2 - 150, 800, 300 };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 240);
        SDL_RenderFillRect(ren, &bg);
        
        char msg[128]; snprintf(msg, sizeof(msg), "Select 2 cards to keep (%d/2)", app.exchangeKeepCount);
        render_text(ren, msg, bg.x + 20, bg.y + 20, app.fontLarge, tcWhite);
        
        int startX = bg.x + 50;
        for (int i=0; i<app.exchangePoolCount; i++) {
            int selected = 0;
            for(int k=0; k<app.exchangeKeepCount; k++) if (app.exchangeKeep[k] == i) selected = 1;
            
            SDL_Rect cr = { startX + i*(CARD_WIDTH+20), bg.y + 80, CARD_WIDTH, CARD_HEIGHT };
            render_card(ren, app.exchangePool[i], cr.x, cr.y, 1, 0);
            
            if (selected) {
                SDL_SetRenderDrawColor(ren, 0, 255, 0, 100);
                SDL_RenderFillRect(ren, &cr);
            }
            
            int h = (mx>=cr.x && mx<=cr.x+cr.w && my>=cr.y && my<=cr.y+cr.h);
            if (h && m_click) {
                if (selected) {
                    // deselect
                    if (app.exchangeKeep[0] == i) { app.exchangeKeep[0] = app.exchangeKeep[1]; app.exchangeKeepCount--; }
                    else if (app.exchangeKeep[1] == i) { app.exchangeKeepCount--; }
                } else if (app.exchangeKeepCount < 2) {
                    app.exchangeKeep[app.exchangeKeepCount++] = i;
                }
            }
        }
        
        if (app.exchangeKeepCount == 2) {
            SDL_Rect br = { bg.x + 300, bg.y + 250, 200, 40 };
            int h = (mx>=br.x && mx<=br.x+br.w && my>=br.y && my<=br.y+br.h);
            if (render_button(ren, "Confirm Exchange", br, h, 0) && m_click) {
                game_do_exchange(&app.game, app.game.current, app.exchangeKeep[0], app.exchangeKeep[1], app.exchangeDrawn[0], app.exchangeDrawn[1]);
                add_log("Completed Exchange.");
                advance_turn();
            }
        }
    }

    // Help block overlay
    if (app.showHelp) {
        int hw = 800;
        int hh = 600;
        SDL_Rect helpRect = {SCREEN_WIDTH/2 - hw/2, SCREEN_HEIGHT/2 - hh/2, hw, hh};
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 250);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(ren, &helpRect);
        if (app.texManual) {
            SDL_RenderCopy(ren, app.texManual, NULL, &helpRect);
        } else {
            render_text(ren, "Please save the manual image as 'manual.png' in the folder.", helpRect.x + 20, helpRect.y + 20, app.fontLarge, tcWhite);
        }
        
        SDL_Rect closeBtn = {helpRect.x + hw - 40, helpRect.y + 10, 30, 30};
        int hClose = (mx>=closeBtn.x && mx<=closeBtn.x+closeBtn.w && my>=closeBtn.y && my<=closeBtn.y+closeBtn.h);
        if (render_button(ren, "X", closeBtn, hClose, 0) && m_click) {
            app.showHelp = 0;
        }
    }

    SDL_RenderPresent(ren);
}


int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    SDL_Window* win = SDL_CreateWindow("Shorojontro", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    memset(&app, 0, sizeof(app));
    app.running = 1;
    app.state = STATE_MENU;
    app.showLog = 1;
    app.font = TTF_OpenFont("C:\\Windows\\Fonts\\arial.ttf", 16);
    app.fontLarge = TTF_OpenFont("C:\\Windows\\Fonts\\arial.ttf", 24);

    app.texCardBack = IMG_LoadTexture(ren, "card_back.png");
    app.texRoles[ROLE_DUKE] = IMG_LoadTexture(ren, "bir_bikram.png");
    app.texRoles[ROLE_ASSASSIN] = IMG_LoadTexture(ren, "brohmodaitto.png");
    app.texRoles[ROLE_CAPTAIN] = IMG_LoadTexture(ren, "kalu_dakat.png");
    app.texRoles[ROLE_AMBASSADOR] = IMG_LoadTexture(ren, "petukchondro.png");
    app.texRoles[ROLE_CONTESSA] = IMG_LoadTexture(ren, "jiner_badshah.png");
    app.texCoin = IMG_LoadTexture(ren, "coin.png");
    app.texMenuBg = IMG_LoadTexture(ren, "logo.png");
    app.texLogo = IMG_LoadTexture(ren, "menu_bg.png");
    app.texManual = IMG_LoadTexture(ren, "manual.png");

    SDL_Event e;
    int mx, my, m_click;
    while(app.running) {
        m_click = 0;
        while(SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) app.running = 0;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) m_click = 1;
        }
        SDL_GetMouseState(&mx, &my);
        draw_game_screen(ren, mx, my, m_click);
    }

    TTF_CloseFont(app.font);
    TTF_CloseFont(app.fontLarge);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
