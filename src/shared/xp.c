#include "xp.h"

int coop_level_from_xp(const int *level_table, int num_entries,
                       float boundary_mult, int xp) {
    int level = 0;
    while (level < num_entries - 1) {
        double boundary = (double)level_table[level + 1] * (double)boundary_mult;
        int rounded = (int)(boundary + 0.5);
        if (xp < rounded) break;
        level++;
    }
    return level;
}
