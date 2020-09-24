# Beef is a UCI-compliant chess engine.
# Copyright (C) 2020 Jonathan Tseng.
# 
# Beef is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# Beef is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

CC      = g++
CFLAGS  = -DNDEBUG -Wall -Wcast-qual -Wextra -Wshadow -pedantic -std=c++11 -m64 -msse3 -mpopcnt -flto -DUSE_POPCNT
NOPOPFLAGS  = -DNDEBUG -Wall -Wcast-qual -Wextra -Wshadow -pedantic -std=c++11 -m64
EXE  = Beef
OPT     = -O3
VERSION = 0.3.0
OBJECTS = board.o eval.o evaltune.o magic.o main.o movegen.o polyglot.o position.o psqt.o search.o syzygy.o thread.o time.o tt.o uci.o util.o weights.o pyrrhic/tbprobe.o

all: $(EXE)

$(EXE): $(OBJECTS)
	$(CC) $(CFLAGS) $(OPT) $^ -o $@ -lpthread

%.o: %.cpp
	$(CC) $(CFLAGS) $(OPT) -c $^ -o $@ -lpthread

nopopcnt:
	$(CC) $(NOPOPFLAGS) $(OPT) *.cpp pyrrhic/tbprobe.cpp -o $(EXE)_$(VERSION)_nopopcnt -lpthread

release: $(OBJECTS)
	$(CC) $(CFLAGS) $(OPT) $^ -o $(EXE)_$(VERSION)_popcnt -lpthread
	
releasewindows:
	$(CC) $(CFLAGS) -static $(OPT) *.cpp pyrrhic/tbprobe.cpp -o $(EXE)_$(VERSION)_popcnt.exe -lpthread
	
nopopcntwindows:
	$(CC) $(NOPOPFLAGS) -static $(OPT) *.cpp pyrrhic/tbprobe.cpp -o $(EXE)_$(VERSION)_nopopcnt -lpthread
