/*
  Beef is a UCI-compliant chess engine.
  Copyright (C) 2020 Jonathan Tseng.

  Beef is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Beef is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Beef.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/mman.h>
#endif

extern timeInfo globalLimits;

void* aligned_ttmem_alloc(size_t allocSize, void*& mem) {
    #if defined(__linux__) && !defined(__ANDROID__)
    cout << "Using large pages" << endl;
    constexpr size_t alignment = 2 * 1024 * 1024;
    size_t size = ((allocSize + alignment - 1) / alignment) * alignment;
    if (posix_memalign(&mem, alignment, size))
        mem = nullptr;
    madvise(mem, allocSize, MADV_HUGEPAGE);
    return mem;
    #else
    constexpr size_t alignment = 64;
    size_t size = allocSize + alignment - 1;
    mem = malloc(size);
    void* ret = (void*)((uintptr_t(mem) + alignment - 1) & ~uintptr_t(alignment - 1));
    return ret;
    #endif
}


void printBits(U64 x)
{
    string S = bitset<64>(x).to_string();
    for ( int i = 0; i < 64; i+= 8)
    {
        string temp = S.substr(i, 8);
        reverse(temp.begin(), temp.end());
        cout << temp << endl;
    }
    cout << endl;
}

vector<string> SplitString(const char *c)
{
	string ss(c);
	vector<string> out;
	istringstream iss(ss);
	for (string s; iss >> s;)
		out.push_back(s);
	return out;
}

unsigned char AlgebraicToIndex(string s)
{
    char file = (char)(s[0] - 'a');
    char rank = (char)(s[1] - '1');

    return (unsigned char)(rank << 3 | file);
}

PieceType GetPieceType(char c)
{
    switch (c)
    {
    case 'n':
    case 'N':
        return KNIGHT;
    case 'b':
    case 'B':
        return BISHOP;
    case 'r':
    case 'R':
        return ROOK;
    case 'q':
    case 'Q':
        return QUEEN;
    case 'k':
    case 'K':
        return KING;
    default:
        break;
    }
    return BLANKTYPE;
}

char PieceChar(PieceCode c, bool lower)
{
    PieceType p = (PieceType)(c >> 1);
    int color = (c & 1);
    char o;
    switch (p)
    {
    case PAWN:
        o = 'p';
        break;
    case KNIGHT:
        o = 'n';
        break;
    case BISHOP:
        o = 'b';
        break;
    case ROOK:
        o = 'r';
        break;
    case QUEEN:
        o = 'q';
        break;
    case KING:
        o = 'k';
        break;
    default:
        o = ' ';
        break;
    }
    if (!lower && !color)
        o = (char)(o + ('A' - 'a'));
    return o;
}

const string move_to_str(Move code)
{
    char s[100];

    if (code == 65 || code == 0)
        return "none";

    int from, to;
    from = from_sq(code);
    to = to_sq(code);
    char promChar = 0;
    if(type_of(code) == PROMOTION)
        promChar = PieceChar(PieceCode(promotion_type(code)*2), true);
    if(type_of(code) == CASTLING)
    {
        int side = (from > 56) ? 1 : 0;
        int castleIndex = 2*side + ((to > from) ? 1 : 0);
        to = castleKingTo[castleIndex];
    }

    sprintf(s, "%c%d%c%d%c", (from & 0x7) + 'a', ((from >> 3) & 0x7) + 1, (to & 0x7) + 'a', ((to >> 3) & 0x7) + 1, promChar);
    return s;
}

void bench()
{
    uint64_t nodes = 0;
    int benchStart = getRealTime();
    is_timeout = false;
    globalLimits.movesToGo = 0;
    globalLimits.totalTimeLeft = 0;
    globalLimits.increment = 0;
    globalLimits.movetime = 0;
    globalLimits.depthlimit = 13;
    globalLimits.timelimited = false;
    globalLimits.depthlimited = true;
    globalLimits.infinite = false;

    for (int i = 0; i < 36; i++){
        cout << "\nPosition [" << (i + 1) << "|36]\n" << endl;
        Position *p = import_fen(benchmarks[i].c_str(), 0);
        think(p);
        nodes += main_thread.nodes;

        clear_tt();
    }

    int time_taken = getRealTime() - benchStart;

    cout << "\n------------------------\n";
    cout << "Time  : " << time_taken << endl;
    cout << "Nodes : " << nodes << endl;
    cout << "NPS   : " << nodes * 1000 / (time_taken + 1) << endl;
}
