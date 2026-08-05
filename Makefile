.PHONY: all assets levels toolchain dos dos-debug host-test perf-test playtest test run screenshot dist clean

PYTHON ?= python3
HOST_CC ?= gcc

all: dos

assets: build/KOLOBOK.DAT levels

build/KOLOBOK.DAT: tools/assets.py
	$(PYTHON) tools/assets.py --out $@

levels: build/GARDEN.KLV build/SFOREST.KLV build/DFOREST.KLV

build/GARDEN.KLV: tools/levels.py assets/garden.json
	$(PYTHON) tools/levels.py import assets/garden.json $@

build/SFOREST.KLV: tools/levels.py assets/sforest.json
	$(PYTHON) tools/levels.py import assets/sforest.json $@

build/DFOREST.KLV: tools/levels.py assets/dforest.json
	$(PYTHON) tools/levels.py import assets/dforest.json $@

toolchain:
	bash tools/bootstrap-watcom.sh

dos: toolchain assets
	bash tools/build.sh

# Same binaries with the ring-buffer trace compiled in. Overwrites
# build/KOLOBOK.EXE, so run `make dos` again before measuring or shipping.
dos-debug: toolchain assets
	KOLO_TRACE=1 bash tools/build.sh

build/test_game: tests/test_game.c src/game.c src/game.h src/assets.c src/assets.h src/trace.c src/trace.h build/KOLOBOK.DAT build/GARDEN.KLV
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isrc tests/test_game.c src/game.c src/assets.c src/trace.c -o $@

build/test_music: tests/test_music.c src/music.c src/music.h src/assets.h
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isrc tests/test_music.c src/music.c -o $@

host-test: build/test_game build/test_music
	./build/test_game
	./build/test_music
	$(PYTHON) tools/assets.py --check
	$(PYTHON) tests/test_levels.py
	$(PYTHON) tests/test_balance.py

perf-test: dos
	bash tools/dosbox-perf-test.sh

playtest: dos
	bash tools/dosbox-playtest.sh

test: dos host-test perf-test playtest
	bash tools/dosbox-test.sh

run: dos
	bash tools/run.sh

screenshot: dos
	bash tools/dosbox-capture.sh

dist: test
	mkdir -p dist
	cp build/KOLOBOK.EXE build/KOLOEDIT.EXE build/KOLOBOK.DAT \
		build/GARDEN.KLV build/SFOREST.KLV build/DFOREST.KLV \
		README.TXT LICENSE dist/

clean:
	rm -rf build dist
