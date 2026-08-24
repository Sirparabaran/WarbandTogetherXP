#pragma once
/* Level from XP, mirroring the engine's mbGame::addExperienceToTroop:
   advance while xp >= round(level_table[level+1] * boundary_mult).
   level_table is the engine's g_levelTable (read from the exe, not copied). */
int coop_level_from_xp(const int *level_table, int num_entries,
                       float boundary_mult, int xp);
