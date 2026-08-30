#include "xp.h"

int coop_level_from_xp(const int *level_table, int num_entries,
                       float boundary_mult, int xp) {
    int level = 0;
    while (level < num_entries - 1) {
        double boundary = (double)level_table[level + 1] * (double)boundary_mult;
        /* Saturate before the int cast: a modded boundary_mult (> ~1.047 on
           the stock table's top entry) pushes the boundary past INT_MAX, and
           casting that to int is undefined. No int xp reaches a saturated
           boundary, so it behaves as "not met". */
        int rounded = (boundary + 0.5 >= 2147483647.0)
                          ? 2147483647
                          : (int)(boundary + 0.5);
        if (xp < rounded) break;
        level++;
    }
    return level;
}
