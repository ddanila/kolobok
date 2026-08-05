# Music provenance and licensing

The melody contour used by Kolobok is transcribed from song 45, “Cranberries
and Raspberries,” in Tchaikovsky's 1869 *50 Russian Folk Songs*, TH 176. The
collection and underlying melody are public domain. A scan is available from
[IMSLP](https://imslp.org/wiki/50_Russian_Folk_Songs,_TH_176_%28Tchaikovsky,_Pyotr%29).

No modern recording, performance, engraving, or edition is distributed or
copied. The title/Garden, Small Forest, Deep Forest, and homecoming harmonies,
bass movement, rhythm, form, and two-operator OPL instruments were created for
this project and are released under the repository's MIT license.

The YM3812 driver detects hardware at ports 388h/389h and sequences at the
game's 30 Hz tick. Four independent arrangements (title/Garden, Small Forest,
Deep Forest, and homecoming) each use six two-operator voices, their own
instrument bank, and compact delta-timed note events. The Deep Forest version
reharmonizes the melody rather than merely changing tempo or timbre.

Each arrangement is a sixteen-step table whose steps carry their own tick
duration, so one pass lasts 88 ticks for the title/Garden score, 90 for Small
Forest, 118 for Deep Forest and 69 for the homecoming, and then loops.

Failed detection silently disables music without changing PC-speaker sound
effects. Host tests route writes through a mock register sink, drive each
arrangement for 192 ticks — several passes of the longest score — and verify
that the sequencer advanced on every tick, that more than 40 note events fired,
that all six voices sound, and that the four register streams are distinct. The
DOS-native self-test additionally checks OPL2 detection and 30 Hz event
advancement under DOSBox-X.
