CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I./src
SRC     = src/main.c src/table.c src/loader.c src/query.c
OUT     = dsuc

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

debug:
	$(CC) -Wall -Wextra -O0 -g -fsanitize=address -I./src -o $(OUT)_debug $(SRC)

clean:
	rm -f $(OUT) $(OUT)_debug *.csv

.PHONY: all debug clean
