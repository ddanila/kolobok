# Platform reachability

The campaign places platforms on tile rows 7, 6 and 5 above a ground surface on
row 9. This note records the geometry a jump has to satisfy, which rows are
reachable, and the constraints that bind new platform placement.

## Geometry

The player is 14 px tall and stands at `y = 130` on the ground row. Landing on a
platform on row `r` is not simply a matter of clearing `r * 16` with the feet.
`move_horizontal` (`src/game.cpp:87`) tests the body at `y + 2` and `y + 12`, so
while the lower body is still inside the platform row the player is refused
entry to the platform's columns and slides along its side. The jump therefore
has to lift the *top* of the player to `y <= r * 16 - 13`.

| Platform row | Required peak | Rise from the ground |
| --- | --- | --- |
| 7 | y ≤ 99 | 31 px |
| 6 | y ≤ 83 | 47 px |
| 5 | y ≤ 67 | 63 px |

Ascending collision only samples the head and descending collision only samples
the feet, so a jump that peaks with the feet inside a platform row snaps the
player up onto it. That is a glitch rather than a route: it teleports the player
several pixels upward, and it cannot be relied on because the horizontal test
above blocks the approach in the first place.

## Which rows are reachable

`JUMP_SPEED` is -1025, which peaks at roughly y=92 and makes row 7 reachable
from the ground. No single jump from the ground reaches rows 6 or 5: row 6 needs
about -1200 and row 5 about -1400, which would be a far floatier game and would
drag the enemy placement into a rebalance.

Climbing needs nothing new. From a row-7 ledge the current jump peaks at y=60,
which clears both thresholds, so the upper tiers are a layout problem rather
than a physics one — a lower ledge simply has to sit beside a higher one.

Deep Forest has a staircase up to the big pie at (63, 4): a grass ledge on row 7
at x=51..54, a sand ledge on row 6 at x=55..57, and the row-5 ice platform at
x=58..64. Measured landing rates along that route are 46%, 82% and 99%.
Standing on the ice platform overlaps the pie's pickup box, so walking the ledge
collects it.

The decorative row-6 platforms in Garden and Small Forest are deliberately left
unreachable. Nothing is stranded on them.

## Constraints on new placement

Two rules bind any new ledge, and both are easy to trip over:

- `tests/test_balance.py` forbids a platform above a required red berry. Deep
  Forest has berries at x=50 and x=65, which is what leaves x=51..57 as the only
  window beside its row-5 platform.
- A ledge must not sit above the launch columns of the ledge below it, or its own
  underside blocks the head during the ascent. A row-6 step at x=66..69, directly
  over the run-up to a row-7 platform at x=70..75, drops that step's landing rate
  to zero.

The campaign playtest's verdict is not monotonic in `JUMP_SPEED`: -1000 and -1050
fail while -1025 and -1100 pass. The bot is a deterministic heuristic, not a
balance model, so a pass at one value is partly luck. Treat it as a
completability smoke test, not as evidence that a given jump height is
well-tuned.
