#ifndef KOLOBOK_EDITCORE_H
#define KOLOBOK_EDITCORE_H

#include "assets.h"

/* The editor's portable half: the level document, the operations that mutate it,
 * and the file names it may live under. None of it touches Mode X or the
 * keyboard, so build/test_editor covers it on the host and src/editor.cpp is
 * left holding only the DOS shell around it. Anything that reads Editor UI state
 * belongs there, not here -- these functions see a LevelData and nothing else. */

/* The blank level a new file starts from: flat ground with one of each object,
 * already valid, so the editor always has something it can save and load. */
#define BLANK_WIDTH 80
#define BLANK_GROUND_ROW 9

#define DEFAULT_RETRY_FRAMES 150
#define DEFAULT_TREE_HEIGHT 3
#define MAX_TREE_HEIGHT 8
#define MAX_CLOUD_SEED 65535L
#define PATROL_HALF_WIDTH 3

/* One tool per paintable thing. On the tile layer the tool *is* the tile index;
 * on the object layer it selects a pickup, animal or tree subtype in that order. */
#define TOOL_COUNT 11
#define TOOL_ANIMAL_FIRST 4
#define TOOL_TREE_FIRST 8

bool make_blank(LevelData *level);
bool valid_83(const char *name);
bool find_object(const LevelData *level, unsigned x, unsigned y,
                 unsigned *kind, unsigned *index);
Encounter *encounter_for(LevelData *level, u16 animal_id, bool create);
bool adjust_property(LevelData *level, unsigned kind, unsigned index,
                     unsigned field, int delta);
bool place_object(LevelData *level, unsigned x, unsigned y, unsigned tool);
bool erase_object(LevelData *level, unsigned x, unsigned y);

#endif
