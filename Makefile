.PHONY: all assets toolchain dos host-test test run dist clean

PYTHON ?= python3
HOST_CC ?= gcc

all: dos

assets: build/KOLOBOK.DAT

build/KOLOBOK.DAT: tools/assets.py assets/level.json
	$(PYTHON) tools/assets.py --out $@

toolchain:
	bash tools/bootstrap-watcom.sh

dos: toolchain assets
	bash tools/build.sh

build/test_game: tests/test_game.c src/game.c src/game.h src/assets.c src/assets.h build/KOLOBOK.DAT
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isrc tests/test_game.c src/game.c src/assets.c -o $@

host-test: build/test_game
	./build/test_game
	$(PYTHON) tools/assets.py --check

test: dos host-test
	bash tools/dosbox-test.sh

run: dos
	bash tools/run.sh

dist: test
	mkdir -p dist
	cp build/KOLOBOK.EXE build/KOLOBOK.DAT README.TXT LICENSE dist/

clean:
	rm -rf build dist
