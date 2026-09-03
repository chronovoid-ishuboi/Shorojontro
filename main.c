#include "game.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
    
    srand((unsigned)time(NULL));
    
    Game g;
    ui_set_player_names(&g);

    puts("\nWelcome to COUP (terminal edition)!");
    puts("Rules baked in:");
    puts("- Income (+1) never blocked.");
    puts("- Foreign Aid (+2) blockable by Duke (any opponent).");
    puts("- Tax: claim Duke (+3), challengeable.");
    puts("- Assassinate: claim Assassin, pay 3; challengeable; target can block with Contessa.");
    puts("- Steal: claim Captain, take up to 2; challengeable; target can block (Captain or Ambassador).");
    puts("- Exchange: claim Ambassador; draw 2, keep 2.");
    puts("- Coup: pay 7, cannot be blocked/challenged. Forced if you have 10+ coins.");
    puts("Bluff/counter/challenge flows with reveals and replacements are fully implemented.\n");

    ui_run_game_loop(&g);
    
    return 0;
}
