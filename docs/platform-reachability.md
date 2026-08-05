# Platform reachability

The campaign places platforms on tile rows 7, 6 and 5 above a ground surface on
row 9. Until now none of them could be landed on, because `JUMP_SPEED` was
too weak. This note records the geometry, what was changed, and what is still
out of reach.

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

## What changed

`JUMP_SPEED` moved from -850 to -1025, which raises the peak from y=105 to
roughly y=92 and makes row 7 reachable.

The value was chosen by simulating the real movement code over every run-up
position and launch frame in front of the first Garden platform and counting how
many of them land:

| Jump speed | Peak | Row 7 landings |
| --- | ---: | ---: |
| -850 (old) | y=105 | 0% |
| -950 | y=99 | 2.5% |
| -1000 | y=96 | 24% |
| -1025 | y=92 | 26% |
| -1100 | y=89 | 31% |

-950 is the arithmetic minimum but only lands from a 2.5% sliver of timings,
which is not a fix a player would notice. -1025 is the smallest value that both
leaves a comfortable window and keeps the campaign playtest passing.

Beware that the playtest's verdict is not monotonic in this constant: -1000 and
-1050 fail while -1025 and -1100 pass. The bot is a deterministic heuristic, not
a balance model, so a pass at one value is partly luck. Treat it as a
completability smoke test, not as evidence that a given jump height is
well-tuned.

The bot also needed one fix to survive the change. Its nearest-pickup steering
could loop forever when an optional pickup sat inside an enemy patrol: knockback
pushed it away, it immediately re-targeted the same pickup and repeated. It now
gives up on a pickup it has chased for 400 frames without collecting. Giving up
on a *required* berry still fails the run, so the check stays meaningful.

## Reaching rows 6 and 5

No single jump from the ground reaches them: row 6 needs about -1200 and row 5
about -1400, which would be a far floatier game and would drag the enemy
placement into a rebalance. Climbing, however, needs nothing new. From a row-7
ledge the current -1025 jump peaks at y=60, which clears both thresholds, so the
tiers were only ever a layout problem — no lower ledge sat beside a higher one.

Deep Forest now has a staircase up to the big pie at (63, 4): a grass ledge on
row 7 at x=51..54, a sand ledge on row 6 at x=55..57, and the pre-existing row-5
ice platform at x=58..64. Measured landing rates along that route are 46%, 82%
and 99%. Standing on the ice platform overlaps the pie's pickup box, so walking
the ledge collects it.

Two constraints shaped the placement, and both are easy to trip over again:

- `tests/test_balance.py` forbids a platform above a required red berry, and
  Deep Forest has berries at x=50 and x=65. That leaves x=51..57 as the only
  window beside the row-5 platform.
- A ledge must not sit above the launch columns of the ledge below it. The first
  attempt put the row-6 step at x=66..69, directly over the run-up to the row-7
  platform at x=70..75, and its own underside then blocked the head during the
  ascent: landing rate on that first step fell from 19% to zero.

The decorative row-6 platforms in Garden and Small Forest are deliberately left
unreachable. Nothing is stranded on them.

## Effects on the playtest bot

Adding a climbable route exposed four separate deadlocks in `campaign_playtest`,
all of which predate this change and were only hidden by platforms being
unreachable:

- It abandoned pickups it could not reach. It now gives up after 400 frames, but
  never on a red berry, since abandoning a required item guarantees the failure
  the rule was meant to avoid.
- It jumped while standing on a platform, walking itself up the new staircase
  with no way down. It now only jumps from the ground row, which is where every
  required pickup sits.
- It treated guardians as threats and evaded them. A full jump lifts it 38px
  while `game_try_talk` only fires within 24px vertically, so it sailed over the
  animal it needed to talk to. Guardians are now destinations, and being near one
  cancels evasion entirely.
- With every berry collected it walked to the exit and idled there, because
  `game_exit_ready` refuses to finish while a required encounter is unsolved. It
  now steers to the guardian once the berries are gone.
