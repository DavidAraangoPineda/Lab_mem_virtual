CC       := gcc
CFLAGS   := -Wall -Werror -std=c99 -O2
SRC      := $(wildcard src/*.c)
HDR      := $(wildcard src/*.h)
OBJ      := $(SRC:.c=.o)
TARGET   := vmsim
GEN      := gen_trace

# En Windows gcc anade .exe, y sin esto make reenlazaria en cada invocacion.
ifeq ($(OS),Windows_NT)
  TARGET := $(TARGET).exe
  GEN    := $(GEN).exe
endif
TRACES   := tests/t1_basico.trace tests/t2_thrash.trace tests/t3_locality.trace \
            tests/t4_belady.trace
POLICIES := fifo lru clock

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ): $(HDR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Ejemplo del enunciado.
run: $(TARGET)
	./$(TARGET) tests/t1_basico.trace

# Tabla comparativa de las tres politicas con 4 marcos (16 KB).
compare: $(TARGET)
	@echo "politica,traza,accesos,fallos,hit_rate,reemplazos,writebacks"
	@for t in $(TRACES); do \
	  for p in $(POLICIES); do \
	    ./$(TARGET) $$t --policy=$$p --phys=16384 --csv; \
	  done; \
	done

# Stress test: traza generada con localidad, 20k accesos sobre 64 paginas.
stress: $(TARGET) $(GEN)
	./$(GEN) locality 64 20000 1 > tests/stress.trace
	@for p in $(POLICIES); do \
	  ./$(TARGET) tests/stress.trace --policy=$$p --phys=65536 --csv; \
	done

$(GEN): tools/gen_trace.c
	$(CC) $(CFLAGS) -o $@ $<

# Solo Linux/WSL: en Windows no hay valgrind.
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 \
	  ./$(TARGET) tests/t2_thrash.trace --policy=lru --phys=16384

clean:
	rm -f $(OBJ) $(TARGET) $(TARGET).exe $(GEN) $(GEN).exe tests/stress.trace

.PHONY: all run compare stress valgrind clean
