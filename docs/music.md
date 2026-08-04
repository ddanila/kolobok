# Music provenance and licensing

The melody contour used by Kolobok is transcribed from song 45, “Cranberries
and Raspberries,” in Tchaikovsky's 1869 *50 Russian Folk Songs*, TH 176. The
collection and underlying melody are public domain. A scan is available from
[IMSLP](https://imslp.org/wiki/50_Russian_Folk_Songs,_TH_176_%28Tchaikovsky,_Pyotr%29).

No modern recording, performance, engraving, or edition is distributed or
copied. The title/Garden, Small Forest, Deep Forest, and homecoming harmonies,
bass movement, rhythm, form, and two-operator OPL instruments were created for
this project and are released under the repository's MIT license.

The YM3812 driver detects hardware at ports 388h/389h, sequences at the game's
30 Hz tick, supports six two-operator voices, and writes compact delta-timed
events. Failed detection silently disables music without changing PC-speaker
sound effects. The portable mock register sink is used by host timing tests.
