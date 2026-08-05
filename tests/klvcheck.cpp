/* Runs the shipping level_validate over a KLV from the host.
 *
 * tools/levels.py performs its own checks while compiling JSON, but those are a
 * second, independently written copy of the same rules and nothing forced the
 * two to agree -- a gap that let unvalidated bear climb rows through. This tool
 * lets tests/test_levels.py hold every KLV the compiler emits against the
 * validator the DOS runtime actually trusts, so C++ stays the single authority
 * and the Python checks are only early diagnostics. */

#include "assets.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    int i;
    if (argc < 2) {
        fprintf(stderr, "usage: klvcheck <level.klv>...\n");
        return 2;
    }
    for (i = 1; i < argc; ++i) {
        LevelData level;
        Error error;
        if (!level_load(&level, argv[i], error)) {
            printf("%s: %s\n", argv[i], error.message());
            return 1;
        }
        level_free(&level);
    }
    return 0;
}
