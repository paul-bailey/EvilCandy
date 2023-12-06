.PHONY: all clean

prog := evilcandy-highlight
all: $(prog)

$(prog): evilcandy_highlight.c
	$(CC) -Wall -o $@ $^

cleanfiles := $(prog) $(wildcard *.o)
clean:
	$(if $(wildcard $(cleanfiles)),$(RM) $(wildcard $(cleanfiles)),@echo "nothing to clean")
