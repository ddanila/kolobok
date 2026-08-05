# Platform reachability

The campaign places platforms on tile rows 7, 6 and 5 above a ground surface on
row 9. Until now none of them could be landed on, because `KOLO_JUMP_SPEED` was
too weak. This note records the geometry, what was changed, and what is still
out of reach.

## Geometry

The player is 14 px tall and stands at `y = 130` on the ground row. Landing on a
platform on row `r` is not simply a matter of clearing `r * 16` with the feet.
`move_horizontal` (`src/game.c:85`) tests the body at `y + 2` and `y + 12`, so
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

`KOLO_JUMP_SPEED` moved from -850 to -1025, which raises the peak from y=105 to
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

## Still unreachable

Rows 6 and 5 remain unreachable from the ground, and no platform chain leads to
them — the Garden row-6 pair at x=34..39 and x=76..80 has no row-7 neighbour
within jumping distance, and the same is true in the other two levels. Reaching
row 6 needs about -1200 and row 5 about -1400, which is a much floatier game
than this one currently is.

This matters for one piece of content: the Deep Forest big pie sits at (63, 4),
on top of the row-5 ice platform at x=58..64. `tests/test_balance.py` calls it a
deliberate exploration reward, but it cannot currently be collected. Resolving
that needs a decision rather than a tweak — either a much larger jump with the
enemy rebalancing that implies, platforms rearranged into climbable chains, or
the pie moved somewhere reachable.
