# General targets

.PHONY: all
all: format

# Formatting

.PHONY: format
format:
	find . -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' | xargs clang-format -i
