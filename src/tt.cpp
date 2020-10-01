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

transpositionTable TT;

void init_tt()
{
    size_t MB = TRANSPOSITION_MB;

    TT.table_size = MB *1024 * 1024;
    TT.size_mask = uint64_t(TT.table_size / sizeof(TTBucket) - 1);
    TT.table = (TTBucket *)aligned_ttmem_alloc(TT.table_size, TT.mem);
    clear_tt();
}

void reset_tt(int mbSize)
{
    free(TT.mem);

    TT.table_size = (uint64_t)mbSize * 1024 * 1024;
    TT.size_mask = uint64_t(TT.table_size / sizeof(TTBucket) - 1);
    TT.table = (TTBucket *)(aligned_ttmem_alloc(TT.table_size, TT.mem));
    if (!TT.mem)
    {
        std::cerr << "Failed to allocate " << mbSize
                << "MB for transposition table." << std::endl;
        exit(EXIT_FAILURE);
    }

    clear_tt();
}

void clear_tt()
{
    /*size_t totalsize = TT.table_size;
    size_t sizePerThread = totalsize / num_threads;
    thread tthread[MAX_THREADS];
    for (int i = 0; i < num_threads; i++)
    {
        void *start = (char*)TT.table + i * sizePerThread;
        tthread[i] = thread(memset, start, 0, sizePerThread);
    }
    memset((char*)TT.table + num_threads * sizePerThread, 0, totalsize - num_threads * sizePerThread);
    for (int i = 0; i < num_threads; i++)
    {
        if (tthread[i].joinable())
            tthread[i].join();
    }*/
    memset(TT.table, 0, TT.table_size);

    TT.generation = 0;
}

int hashfull()
{
    int cnt = 0;
    for (int i = 0; i < 1000 / 3; i++)
        for (int j = 0; j < 3; j++)
            cnt += (tte_age(&TT.table[i].entries[j]) == TT.generation);
    return cnt * 1000 / (3 * (1000 / 3));
}

int score_to_tt(int score, uint16_t ply) {
    if (score >= MATE_IN_MAX_PLY) {
        return score + ply;
    } else if (score <= MATED_IN_MAX_PLY) {
        return score - ply;
    } else {
        return score;
    }
}

int tt_to_score(int score, uint16_t ply) {
    if (score >= MATE_IN_MAX_PLY) {
        return score - ply;
    } else if (score <= MATED_IN_MAX_PLY) {
        return score + ply;
    } else {
        return score;
    }
}

void storeEntry(TTEntry *entry, U64 key, Move m, int depth, int score, int staticEval, uint8_t flag)
{
    #ifndef __TUNE__
    uint16_t upper = (uint16_t)(key >> 48);
    if (m || upper != entry->hashupper)
        entry->movecode = m;
    if (upper != entry->hashupper || depth > entry->depth - 4)
    {
        entry->hashupper = upper;
        entry->depth = (int8_t)depth;
        entry->flags = (TT.generation << 2) | flag;
        entry->static_eval = (int16_t)staticEval;
        entry->value = (int16_t)score;
    }
    #endif
}

int age_diff(TTEntry *tte) {
    return (TT.generation - tte_age(tte)) & 0x3F;
}

TTEntry *probeTT(U64 key, bool &ttHit)
{
    #ifndef __TUNE__
    U64 index = key & TT.size_mask;
    TTBucket *bucket = &TT.table[index];
    uint16_t upper = (uint16_t)(key >> 48);
    for (int i = 0; i < 3; i++)
    {
        ///entry found
        if (bucket->entries[i].hashupper == upper)
        {
            ttHit = true;
            return &bucket->entries[i];
        }

        ///blank entry found, this key has never been used
        if (!bucket->entries[i].hashupper)
        {
            ttHit = false;
            return &bucket->entries[i];
        }
    }

    ///no matching entry found + no empty entries found, return the least valuable entry
    TTEntry *cheapest = &bucket->entries[0];
    for (int i = 1; i< 3; i++)
    {
        if ((bucket->entries[i].depth - age_diff(&bucket->entries[i]) * 16) < (cheapest->depth - age_diff(cheapest) * 16))
            cheapest = &bucket->entries[i];
    }
    ttHit = false;
    return cheapest;
    #else
    TTBucket *bucket = &TT.table[0];
    ttHit = false;
    return &bucket->entries[0];
    #endif
}

void start_search() {
    TT.generation = (TT.generation + 1) % 64;
}

zobrist::zobrist()
{
    PRNG rng(1070372);
    int i;
    int j = 0;

    U64 c[4];
    U64 ep[8];
    for (i = 0; i < 4; i++)
        c[i] = rng.rand();
    for (i = 0; i < 8; i++)
        ep[i] = rng.rand();
    for (i = 0; i < 16; i++)
    {
        castle[i] = 0ULL;
        for (j = 0; j < 4; j++)
        {
            if (i & (1 << (j)))
                castle[i] ^= c[j];
        }
    }
    for (i = 0; i < 64; i++)
    {
        epSquares[i] = 0ULL;
        if (RANK(i) == 2 || RANK(i) == 5)
            epSquares[i] = ep[FILE(i)];
    }

    for (i = 0; i < 64*16; i++)
        pieceKeys[i] = rng.rand();

    activeSide = rng.rand();
}

U64 zobrist::getHash(Position *pos)
{
	U64 out = 0ULL;
	for (int i = WPAWN; i<=BKING;i++)
	{
		U64 pieces = pos->pieceBB[i];
		unsigned int index;
		while (pieces)
		{
			index = popLsb(&pieces);
			out ^= pieceKeys[(index << 4) | i];
		}
	}

	if (pos->activeSide)
		out ^= activeSide;
	out ^= castle[pos->castleRights];
	out ^= epSquares[pos->epSquare];
	return out;
}

U64 zobrist::getPawnHash(Position *pos)
{
	U64 out = 0ULL;
	for (int i = WPAWN; i<=BPAWN; i++)
	{
		U64 pawns = pos->pieceBB[i];
		unsigned int index;
		while (pawns)
		{
			index = popLsb(&pawns);
			out ^= pieceKeys[(index << 4) | i];
		}
	}

	out ^= pieceKeys[(pos->kingpos[0] << 4) | WKING] ^ pieceKeys[(pos->kingpos[1] << 4) | BKING];
	return out;
}
