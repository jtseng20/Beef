CC      = g++
CFLAGS  = -DNDEBUG -Wall -Wcast-qual -Wextra -Wshadow -pedantic -std=c++11 -m64 -msse3 -mpopcnt -flto -DUSE_POPCNT
NOPOPFLAGS  = -DNDEBUG -Wall -Wcast-qual -Wextra -Wshadow -pedantic -std=c++11 -m64
EXE  = Beef
OPT     = -O3
VERSION = 0.2.1
OBJECTS = board.o eval.o evaltune.o magic.o main.o movegen.o polyglot.o position.o psqt.o search.o syzygy.o thread.o time.o tt.o uci.o util.o weights.o pyrrhic/tbprobe.o

all: $(EXE)

$(EXE): $(OBJECTS)
	$(CC) $(CFLAGS) $(OPT) $^ -o $@ -pthread

%.o: %.cpp
	$(CC) $(CFLAGS) $(OPT) -c $^ -o $@ -pthread

nopopcnt:
	$(CC) $(NOPOPFLAGS) $(OPT) *.cpp pyrrhic/tbprobe.cpp -o $(EXE)_$(VERSION)_nopopcnt -lpthread

release: $(OBJECTS)
	$(CC) $(CFLAGS) $(OPT) $^ -o $(EXE)_$(VERSION)_popcnt -lpthread
	
releasewindows:
	$(CC) $(CFLAGS) $(OPT) *.cpp pyrrhic/tbprobe.cpp -o $(EXE)_$(VERSION)_popcnt.exe -lpthread
	
nopopcntwindows:
	$(CC) $(NOPOPFLAGS) -static $(OPT) *.cpp pyrrhic/tbprobe.cpp -o $(EXE)_$(VERSION)_nopopcnt -lpthread
