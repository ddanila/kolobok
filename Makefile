.PHONY: all art-import assets levels toolchain dos dos-debug trace-playtest host-test perf-test playtest test run screenshot dist clean

PYTHON ?= python3

# g++ accepts all of C++11; Open Watcom's wpp is C++98 plus static_assert,
# decltype and the >> template close, so host-test cannot vouch for a change on
# its own. Classes, destructors, references, overloading and templates all work.
# Its deduction does not: no array extents, and const is dropped when deducing
# from an array inside a const struct.
HOST_CXX ?= g++
HOST_CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror

all: dos

art-import:
	$(PYTHON) tools/assets.py --normalize-sources

assets: build/KOLOBOK.DAT build/generated/grandparents.inc levels

build/KOLOBOK.DAT: tools/assets.py assets/art/manifest.json assets/art/tiles.png assets/art/sprites.png
	$(PYTHON) tools/assets.py --out $@

build/generated/grandparents.inc: tools/characters.py tools/assets.py assets/art/manifest.json assets/art/grandparents.png
	$(PYTHON) tools/characters.py --out $@

levels: build/GARDEN.KLV build/SFOREST.KLV build/DFOREST.KLV

build/GARDEN.KLV: tools/levels.py assets/levels/garden.json
	$(PYTHON) tools/levels.py import assets/levels/garden.json $@

build/SFOREST.KLV: tools/levels.py assets/levels/sforest.json
	$(PYTHON) tools/levels.py import assets/levels/sforest.json $@

build/DFOREST.KLV: tools/levels.py assets/levels/dforest.json
	$(PYTHON) tools/levels.py import assets/levels/dforest.json $@

toolchain:
	bash tools/bootstrap-watcom.sh

dos: toolchain assets
	bash tools/build.sh

# Same binaries with the ring-buffer trace compiled in. Overwrites
# build/KOLOBOK.EXE, so run `make dos` again before measuring or shipping.
dos-debug: toolchain assets
	KOLO_TRACE=1 bash tools/build.sh

build/test_game: tests/test_game.cpp src/game.cpp src/game_state.cpp src/game.h src/assets.cpp src/assets.h src/trace.cpp src/trace.h build/KOLOBOK.DAT levels
	$(HOST_CXX) $(HOST_CXXFLAGS) -Isrc tests/test_game.cpp src/game.cpp src/game_state.cpp src/assets.cpp src/trace.cpp -o $@

build/test_music: tests/test_music.cpp src/music.cpp src/music.h src/assets.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -Isrc tests/test_music.cpp src/music.cpp -o $@

build/test_editor: tests/test_editor.cpp src/editcore.cpp src/editcore.h src/assets.cpp src/assets.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -Isrc tests/test_editor.cpp src/editcore.cpp src/assets.cpp -o $@

# tests/test_levels.py runs this over every KLV the Python compiler emits, so the
# compiler can never accept a level the DOS runtime would reject.
build/klvcheck: tests/klvcheck.cpp src/assets.cpp src/assets.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -Isrc tests/klvcheck.cpp src/assets.cpp -o $@

host-test: build/test_game build/test_music build/test_editor build/klvcheck build/generated/grandparents.inc
	./build/test_game
	./build/test_music
	./build/test_editor
	$(PYTHON) tools/assets.py --check
	$(PYTHON) tools/characters.py --check
	$(PYTHON) tests/test_levels.py
	$(PYTHON) tests/test_balance.py

# Traced playthrough against the debug machine profile, so each record also
# streams to the emulator log (build/PLAY-EMU.LOG) as it happens.
trace-playtest: dos-debug
	KOLO_DOSBOX_CONF=$(CURDIR)/dosbox-x-debug.conf bash tools/dosbox-playtest.sh

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
