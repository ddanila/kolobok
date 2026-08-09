# Music provenance and licensing

Kolobok uses “Korobeiniki,” a Russian folk song dating to 1861. The melody is
public domain; a score and its public-domain declaration are
available from [Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Korobeiniki.jpg).

No modern recording, performance, engraving, or edition is distributed or
copied. The title/Garden, Small Forest, Deep Forest, and homecoming harmonies,
bass movement, rhythm, form, and two-operator OPL instruments were created for
this project and are released under the repository's MIT license.

The YM3812 driver detects hardware at ports 388h/389h and sequences at the
game's 30 Hz tick. Four independent arrangements (title/Garden, Small Forest,
Deep Forest, and homecoming) each use six two-operator voices, their own
instrument bank, tempo, register and chord voicings.

The score contains 54 note events: the opening strain followed by two passes of
the second strain. One form lasts 480 ticks for title/Garden, 576 for Small
Forest, 672 for Deep Forest and 384 for homecoming before looping.

Failed detection silently disables music without changing PC-speaker sound
effects. Host tests route writes through a mock register sink, drive each
arrangement for 704 ticks — at least one pass of the longest score — and verify
that the sequencer advanced on every tick, that more than 40 note events fired,
that all six voices sound, and that the four register streams are distinct. The
DOS-native self-test additionally checks OPL2 detection and 30 Hz event
advancement under DOSBox-X.
