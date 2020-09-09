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

#ifndef BEEF_H_INCLUDED
#define BEEF_H_INCLUDED
#endif // BEEF_H_INCLUDED

#pragma once
#include <iostream>
#include <iomanip>
#include <stdarg.h>
#include <cstring>
#include <string>
#include <iostream>
#include <fstream>
#include <list>
#include <string>
#include <string.h>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iterator>
#include <thread>
//#include <pthread.h>
#include <map>
#include <array>
#include <bitset>
#include <limits.h>
#include <math.h>
#include <regex>
#include <stdlib.h>
#include <assert.h>


#define NAME "Beef"
#define VERSION "0.2.2"
#define AUTHOR "Jonathan Tseng"

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <time.h>
#else
#  include <sys/time.h>
#endif


#ifdef _MSC_VER
#define USE_POPCNT // For MSVC, automatically use popcount
#define PREFETCH(x) _mm_prefetch((char *)(x), _MM_HINT_T0)
inline int LSB(uint64_t x)
{
    DWORD i;
    _BitScanForward64(&i, x);
    return i;
}

inline int MSB(uint64_t x)
{
    DWORD i;
    _BitScanReverse64(&i, x);
    return i;
}

inline int popLsb(uint64_t* x)
{
    DWORD i;
    _BitScanForward64(&i, *x);
    *x &= *x - 1;
    return i;
}
#else
#define PREFETCH(a) __builtin_prefetch(a)
inline int LSB(uint64_t x)
{
    return __builtin_ctzll(x);
}
inline int MSB(uint64_t x)
{
    return 63 - __builtin_clzll(x);
}

inline int popLsb(uint64_t* x) {
    int i = __builtin_ctzll(*x);
    *x &= *x - 1;
    return i;
}
#endif

void* aligned_ttmem_alloc(size_t allocSize, void*& mem);

enum Color { WHITE, BLACK };

constexpr Color operator~(Color c) {
    return Color(c ^ BLACK); // Toggle color
}

#define RANK(x) ((x) >> 3)
#define RRANK(x,s) ((s) ? ((x) >> 3) ^ 7 : ((x) >> 3))
#define FILE(x) ((x) & 0x7)
#define INDEX(r,f) ((r << 3) | f)
#define PROMOTERANK(x) (RANK(x) == 0 || RANK(x) == 7)
#define PROMOTERANKBB 0xff000000000000ff

typedef uint64_t U64;

enum Move : uint16_t {
    MOVE_NONE,
    MOVE_NULL = 65
};

enum SpecialType {
    NORMAL,
    PROMOTION = 1,
    ENPASSANT = 2,
    CASTLING = 3
};

enum gamePhase {
    MG, EG
};

enum {
    A1 = 0,
    B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8
};

constexpr U64 DarkSquares = 0xAA55AA55AA55AA55ULL;

constexpr U64 FileABB = 0x0101010101010101ULL;
constexpr U64 FileBBB = FileABB << 1;
constexpr U64 FileCBB = FileABB << 2;
constexpr U64 FileDBB = FileABB << 3;
constexpr U64 FileEBB = FileABB << 4;
constexpr U64 FileFBB = FileABB << 5;
constexpr U64 FileGBB = FileABB << 6;
constexpr U64 FileHBB = FileABB << 7;
constexpr U64 FileBB[8] = { FileABB, FileBBB, FileCBB, FileDBB, FileEBB, FileFBB, FileGBB, FileHBB };

constexpr U64 FileAH = FileABB | FileHBB;

constexpr U64 Rank1BB = 0xFF;
constexpr U64 Rank2BB = Rank1BB << (8 * 1);
constexpr U64 Rank3BB = Rank1BB << (8 * 2);
constexpr U64 Rank4BB = Rank1BB << (8 * 3);
constexpr U64 Rank5BB = Rank1BB << (8 * 4);
constexpr U64 Rank6BB = Rank1BB << (8 * 5);
constexpr U64 Rank7BB = Rank1BB << (8 * 6);
constexpr U64 Rank8BB = Rank1BB << (8 * 7);
constexpr U64 RankBB[8] = { Rank1BB, Rank2BB, Rank3BB, Rank4BB, Rank5BB, Rank6BB, Rank7BB, Rank8BB };

constexpr U64 Edges = FileAH | Rank1BB | Rank2BB | Rank7BB | Rank8BB;
constexpr U64 Center = ~Edges;

constexpr U64 flank_ranks[2] = { Rank1BB | Rank2BB | Rank3BB | Rank4BB,
                                Rank5BB | Rank6BB | Rank7BB | Rank8BB };

constexpr U64 outpost_ranks[2] = { Rank4BB | Rank5BB | Rank6BB,
                                  Rank3BB | Rank4BB | Rank5BB };

constexpr U64 queenSide = FileABB | FileBBB | FileCBB | FileDBB;
constexpr U64 centerFiles = FileCBB | FileDBB | FileEBB | FileFBB;
constexpr U64 kingSide = FileEBB | FileFBB | FileGBB | FileHBB;

constexpr U64 flank_files[8] = { queenSide,
                                queenSide,
                                queenSide,
                                centerFiles,
                                centerFiles,
                                kingSide,
                                kingSide,
                                kingSide };

enum Direction : int {
    NORTH = 8,
    EAST = 1,
    SOUTH = -8,
    WEST = -1,

    NORTH_EAST = NORTH + EAST,
    SOUTH_EAST = SOUTH + EAST,
    SOUTH_WEST = SOUTH + WEST,
    NORTH_WEST = NORTH + WEST
};

template<Direction D>
constexpr U64 shift(U64 b) {
    return  D == NORTH ? b << 8 : D == SOUTH ? b >> 8
        : D == NORTH + NORTH ? b << 16 : D == SOUTH + SOUTH ? b >> 16
        : D == EAST ? (b & ~FileHBB) << 1 : D == WEST ? (b & ~FileABB) >> 1
        : D == NORTH_EAST ? (b & ~FileHBB) << 9 : D == NORTH_WEST ? (b & ~FileABB) << 7
        : D == SOUTH_EAST ? (b & ~FileHBB) >> 7 : D == SOUTH_WEST ? (b & ~FileABB) >> 9
        : 0;
}

template<Color C>
constexpr U64 pawn_attacks_bb(U64 b) {
    return C == WHITE ? shift<NORTH_WEST>(b) | shift<NORTH_EAST>(b)
        : shift<SOUTH_WEST>(b) | shift<SOUTH_EAST>(b);
}

constexpr Direction pawn_push(Color c) {
    return c == WHITE ? NORTH : SOUTH;
}

enum PieceType { BLANKTYPE, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum PieceCode { BLANK, WPAWN = 2, BPAWN, WKNIGHT, BKNIGHT, WBISHOP, BBISHOP, WROOK, BROOK, WQUEEN, BQUEEN, WKING, BKING };

PieceType GetPieceType(char c);

constexpr SpecialType type_of(uint16_t m)
{
    return SpecialType((m >> 14) & 3);
}

constexpr int from_sq(uint16_t m) {
    return ((m >> 6) & 0x3F);
}

constexpr int to_sq(uint16_t m) {
    return (m & 0x3F);
}

constexpr int from_to(uint16_t m) {
    return m & 0xFFF;
}

constexpr PieceType promotion_type(uint16_t m) {
    return (PieceType)(((m >> 12) & 3) + KNIGHT);
}

constexpr Move makeMove(int from, int to, SpecialType type)
{
    return Move((type << 14) | (from << 6) | to);
}

constexpr Move makePromotionMove(int from, int to, PieceType promote, SpecialType type)
{
    return  Move((type << 14) | ((promote - KNIGHT) << 12) | (from << 6) | to);
}

constexpr PieceCode make_piece(Color c, PieceType pt) {
    return PieceCode((pt << 1) + c);
}


#define PAWNPUSH(s, x) ((s) ? (x >> 8) : (x << 8))
#define PAWNPUSHINDEX(s, x) ((s) ? (x - 8) : (x + 8))
#define PAWN2PUSHINDEX(s, x) ((s) ? (x - 16) : (x + 16))

#define BITSET(x) (1ULL << (x))
#define MORETHANONE(x) ((x) & ((x) - 1))
#define ONEORZERO(x) (!MORETHANONE(x))
#define ONLYONE(x) (!MORETHANONE(x) && x)

#define PAWNATTACKS(s, x) ((s) ? ((shift<SOUTH_EAST>(x)) | (shift < SOUTH_WEST> (x))) : ((shift <NORTH_EAST> (x)) | (shift <NORTH_WEST> (x))))
#define STARTFEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

#define SIDESWITCH 0x01
#define S2MSIGN(s) (s ? -1 : 1)
#define WQCMASK 0x01
#define WKCMASK 0x02
#define BQCMASK 0x04
#define BKCMASK 0x08
#define WHITECASTLE 0x03
#define BLACKCASTLE 0x0c

enum castleSides { QUEENSIDE, KINGSIDE };
constexpr int KINGSIDE_CASTLE_MASKS[2] = { 0x02, 0x08 };
constexpr int QUEENSIDE_CASTLE_MASKS[2] = { 0x01, 0x04 };

#define S(m, e) make_score(m, e)
#define FLIP_SQUARE(s, x) ((s) ? (x) ^ 56 : (x))

const int castleRookFrom[4] = { A1, H1, A8, H8 };
const int castleRookTo[4] = { D1, F1, D8, F8 };
const int castleKingTo[4] = { C1, G1, C8, G8 };

const int knightoffset[] = { SOUTH + EAST + EAST,
                             SOUTH + WEST + WEST,
                             SOUTH + SOUTH + EAST,
                             SOUTH + SOUTH + WEST,
                             NORTH + WEST + WEST,
                             NORTH + EAST + EAST,
                             NORTH + NORTH + WEST,
                             NORTH + NORTH + EAST };

const int diagonaloffset[] = { SOUTH_EAST, SOUTH_WEST, NORTH_WEST, NORTH_EAST };
const int orthogonaloffset[] = { SOUTH, WEST, EAST, NORTH };
const int orthogonalanddiagonaloffset[] = { SOUTH, WEST, EAST, NORTH, SOUTH_EAST, SOUTH_WEST, NORTH_WEST, NORTH_EAST };

using namespace std;

const string move_to_str(Move code);

enum MoveType { QUIET = 1, CAPTURE = 2, PROMOTE = 4, TACTICAL = 6, ALL = 7, EVASION = 8, QUIET_CHECK = 16 };

const int MAX_PLY = 128;
const int MAX_THREADS = 32;

enum {
    VALUE_DRAW = 0,
    VALUE_MATE = 32000,
    VALUE_INF = 32001,
    UNDEFINED = 32002,
    TIMEOUT = 32003,
    WON_ENDGAME = 10000,
    MATE_IN_MAX_PLY = VALUE_MATE - MAX_PLY,
    VALUE_MATED = -32000,
    MATED_IN_MAX_PLY = VALUE_MATED + MAX_PLY,
    TB_MATE = MATE_IN_MAX_PLY - MAX_PLY - 1
};

enum Score : int { SCORE_DRAW };

enum ScoreType {
    SCORE_QUIET,
    SCORE_CAPTURE,
    SCORE_EVASION
};


constexpr Score operator+(Score d1, Score d2) { return Score(int(d1) + int(d2)); }
constexpr Score operator-(Score d1, Score d2) { return Score(int(d1) - int(d2)); }
constexpr Score operator-(Score d) { return Score(-int(d)); }
inline Score& operator+=(Score& d1, Score d2) { return d1 = d1 + d2; }
inline Score& operator-=(Score& d1, Score d2) { return d1 = d1 - d2; }


constexpr Score make_score(int mg, int eg) {
    return Score((int)((unsigned int)eg << 16) + mg);
}

inline int eg_value(Score s) {
    return (int16_t(uint16_t(unsigned(s + 0x8000) >> 16)));
}

inline int mg_value(Score s) {
    return (int16_t(uint16_t(unsigned(s))));
}

inline Score operator/(Score s, int i) {
    return make_score(mg_value(s) / i, eg_value(s) / i);
}

inline Score operator*(Score s, int i) {

    Score result = Score(int(s) * i);
    return result;
}

struct SMagic {
    U64 mask;
    U64 magic;
    unsigned shift;
    U64* aptr;

    inline unsigned index(U64 occ) const
    {
        return unsigned((occ & mask) * magic >> shift);
    }
};

/// http://vigna.di.unimi.it/ftp/papers/xorshift.pdf
class PRNG {

    U64 s;

    U64 rand64() {

        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return s * 2685821657736338717LL;
    }

public:
    PRNG(U64 seed) : s(seed) { ; }

    U64 rand() { return (rand64()); }

    U64 sparse_rand()
    {
        return (rand64() & rand64() & rand64());
    }
};

extern U64 PAWN_ATTACKS[2][64];
extern U64 RANK_MASKS[64];
extern U64 FILE_MASKS[64];
extern U64 DIAG_MASKS[64];
extern U64 ANTIDIAG_MASKS[64];
extern U64 BETWEEN_MASKS[64][64];
extern U64 PAWN_2PUSHES[2][64];
extern U64 PAWN_PUSHES[2][64];
extern U64 PAWN_2PUSHESFROM[2][64]; //pawn pushes that go TO index
extern U64 PAWN_PUSHESFROM[2][64];
extern U64 PAWN_ATTACKSFROM[2][64];
extern U64 SQUARE_MASKS[64];
extern U64 BISHOP_MASKS[64];
extern U64 ROOK_MASKS[64];
extern U64 RAY_MASKS[64][64];
extern U64 PseudoAttacks[7][64];
extern U64 castlekingwalk[4];
extern U64 epthelper[64];
extern U64 castlerights[64];
extern U64 neighborMasks[64];
extern U64 passedPawnMasks[2][64];
extern U64 pawnBlockerMasks[2][64];
extern U64 phalanxMasks[64];
extern U64 kingRing[64];
extern int mvvlva[14][14];
extern U64 distanceRings[64][8];
extern U64 colorMasks[64];
extern int squareDistance[64][64];
extern uint8_t PopCnt16[1 << 16];

extern SMagic mBishopTbl[64];
extern SMagic mRookTbl[64];

inline int POPCOUNT(uint64_t x)
{
#ifndef USE_POPCNT

    union { uint64_t bb; uint16_t u[4]; } v = { x };
    return PopCnt16[v.u[0]] + PopCnt16[v.u[1]] + PopCnt16[v.u[2]] + PopCnt16[v.u[3]];

#elif defined(_MSC_VER)

    return (int)(__popcnt64(x));

#else // GCC

    return __builtin_popcountll(x);

#endif
}

void init_boards();
void init_magics(U64 attackTable[], SMagic magics[], U64* masks, U64(*func)(U64, int), const U64* magicNumbers);
void init_values();
void init_threads();
void reset_threads(int thread_num);
void get_ready();

U64 rookAttacks_slow(U64 occ, int sq);
U64 bishopAttacks_slow(U64 occ, int sq);

inline U64 bishopAttacks(U64 occ, int sq)
{
    const SMagic m = mBishopTbl[sq];
    return m.aptr[m.index(occ)];
}

inline U64 rookAttacks(U64 occ, int sq)
{
    const SMagic m = mRookTbl[sq];
    return m.aptr[m.index(occ)];
}

char PieceChar(PieceCode c, bool lower = false);

struct SMove
{
public:
    Move code;
    int value;
    operator Move() const { return code; }
    void operator=(Move m) { code = m; }

    bool operator<(const SMove m) const { return (value < m.value); }
    bool operator>(const SMove m) const { return (value > m.value); }

    string toString() const
    {
        return move_to_str(code);
    }
};

typedef struct Position Position;
typedef struct SearchThread SearchThread;
typedef struct pawnhashTable pawnhashTable;
typedef struct searchInfo searchInfo;
typedef struct materialhashEntry materialhashEntry;

struct stateHistory
{
    U64 key;
    U64 pawnhash;
    U64 materialhash;
    U64 blockersForKing[2][2]; //color x (diag/straight)
    U64 checkBB;
    uint8_t castleRights;
    int epSquare;
    int kingpos[2];
    int halfmoveClock;
    int fullmoveClock;
    PieceCode capturedPiece;
};

// TODO (drstrange767#1#): Add checkSquares in Position to upgrade gives check method

class Position
{
public:
    //copy these to the historyStack
    U64 key;
    U64 pawnhash;
    U64 materialhash;
    U64 blockersForKing[2][2]; //color x (diag/straight)
    U64 checkBB;
    uint8_t castleRights; // BK BQ WK WQ
    int epSquare;
    int kingpos[2];
    int halfmoveClock;
    int fullmoveClock;
    PieceCode capturedPiece;

    U64 pieceBB[14];
    U64 occupiedBB[2];
    PieceCode mailbox[64];
    int pieceCount[14];
    Color activeSide;

    stateHistory historyStack[512];
    int historyIndex;
    Move moveStack[512];

    Score psqt_score;
    SearchThread* my_thread;
    int nonPawn[2];
    bool gameCycle;

    static void init();

    bool isAttacked(int square, int side) const;
    bool testRepetition() const;
    bool isCapture(Move m) const;
    bool isTactical(Move m) const;
    bool isPseudoLegal(const Move m) const;
    bool isLegal(const Move m) const;
    bool pawnOn7th() const;
    int smallestAttacker(U64 attackers, Color color) const;
    bool see(Move m, int threshold) const;
    U64 attackersTo(int square, int side, bool free = false) const;
    U64 attackers_to(int sq, int side, U64 occ) const;
    U64 all_attackers_to(int sq, U64 occ) const;
    U64 getAttackSet(int sq, U64 occ) const;
    bool givesCheck(Move m);
    bool givesDiscoveredCheck(Move m) const;
    bool advanced_pawn_push(Move m) const;
    void do_move(Move m);
    void undo_move(Move m);
    void do_null_move();
    void undo_null_move();
    void set_piece_at(int sq, PieceCode pc);
    void remove_piece_at(int sq, PieceCode pc);
    void move_piece(int from, int to, PieceCode pc);

    U64 tabooSquares() const;
    bool horizontalCheck(U64 occ, int sq) const;

    void updateBlockers();
    void readFEN(const char* fen);

    int scaleFactor() const;

    friend ostream& operator<<(ostream& os, const Position& p);
};

// assuming that Move m gives check, if it was a blocker then it's a discovered check
inline bool Position::givesDiscoveredCheck(Move m) const
{
    return (blockersForKing[~activeSide][0] | blockersForKing[~activeSide][1]) & BITSET(from_sq(m));
}

Position* start_position();
Position* import_fen(const char* fen, int thread_id);

class zobrist
{
public:
    U64 pieceKeys[64 * 16];
    U64 castle[16];
    U64 epSquares[64];
    U64 activeSide;
    zobrist();
    U64 getHash(Position* pos);
    U64 getPawnHash(Position* pos);
};

extern zobrist zb;

template <PieceType Pt> SMove* generatePieceMoves(const Position& pos, SMove* movelist, U64 occ, U64 to_squares, U64 from_mask = ~0);

template <Color side, MoveType Mt> inline SMove* generatePawnMoves(const Position& pos, SMove* movelist, U64 from_mask = ~0, U64 to_mask = ~0);

template <Color side> inline SMove* generateCastles(const Position& pos, SMove* movelist);

template <Color side, MoveType Mt> SMove* generate_pseudo_legal_moves(const Position& pos, SMove* movelist, U64 from_mask);

template <Color side> SMove* generate_evasions(const Position& pos, SMove* movelist);

template <Color side, MoveType Mt> SMove* generatePinnedMoves(const Position& pos, SMove* movelist);

template <Color side, MoveType Mt> SMove* generate_legal_moves(const Position& pos, SMove* movelist);

template <MoveType Mt> SMove* generate_all(const Position& pos, SMove* movelist);

template <Color side> SMove* generate_quiet_checks(const Position& pos, SMove* movelist);

template<MoveType T>
struct MoveList {

    explicit MoveList(const Position& pos) : last(generate_all<T>(pos, moveList)) {}
    const SMove* begin() const { return moveList; }
    const SMove* end() const { return last; }
    size_t size() const { return last - moveList; }
    bool contains(Move move) const {
        return std::find(begin(), end(), move) != end();
    }

private:
    SMove moveList[256], * last;
};

enum Stages {
    // Regular stages
    HASHMOVE_STATE = 0,

    TACTICAL_INIT,
    TACTICAL_STATE,

    KILLER_MOVE_2,
    COUNTER_MOVE,

    QUIETS_INIT,
    QUIET_STATE,

    BAD_TACTICAL_STATE,

    // Evasion stages
    EVASIONS_INIT,
    EVASIONS_STATE,

    // Quiescence stages
    QUIESCENCE_HASHMOVE,
    QUIESCENCE_CAPTURES_INIT,
    QUIESCENCE_CAPTURES,
    QUIESCENCE_CHECKS_INIT,
    QUIESCENCE_CHECKS,

    //Probcut stages
    PROBCUT_HASHMOVE,
    PROBCUT_CAPTURES_INIT,
    PROBCUT_CAPTURES
};

enum SearchType {
    NORMAL_SEARCH,
    QUIESCENCE_SEARCH,
    PROBCUT_SEARCH,
    DEBUG_SEARCH
};

enum PickType {
    NEXT,
    BEST
};

class MoveGen
{
public:
    MoveGen(Position* p, SearchType type, Move hshm, int t, int d);
    int state;
    SMove moveList[256];
    SMove* curr, * endMoves, * endBadCaptures;
    Move hashmove;
    Move counterMove;
    int threshold;
    int depth;

    Move next_move(searchInfo* info, bool skipQuiets = false);
    void scoreMoves(searchInfo* info, ScoreType type);
    template <PickType type> Move select_move();
private:
    Position* pos;
    SMove* begin() { return curr; };
    SMove* end() { return endMoves; };
};

unsigned char AlgebraicToIndex(string s);
vector<string> SplitString(const char* c);
void printBits(U64 x);

#define TRANSPOSITION_MB 128 // default size
constexpr int PAWN_ENTRIES = 16384; // default size
constexpr int PAWN_HASH_SIZE_MASK = PAWN_ENTRIES - 1;

constexpr int MATERIAL_ENTRIES = 65536; // default size
constexpr int MATERIAL_HASH_SIZE_MASK = MATERIAL_ENTRIES - 1;

const uint8_t FLAG_ALPHA = 1;
const uint8_t FLAG_BETA = 2;
const uint8_t FLAG_EXACT = 3; // FLAG_ALPHA | FLAG_BETA

struct TTEntry
{
    uint16_t hashupper;
    Move movecode;
    int16_t value;
    int16_t static_eval;
    uint8_t depth;
    uint8_t flags;
};

inline uint8_t tte_flag(TTEntry* tte) {
    return (uint8_t)(tte->flags & 0x3);
}

inline uint8_t tte_age(TTEntry* tte) {
    return (uint8_t)(tte->flags >> 2);
}

struct TTBucket
{
    TTEntry entries[3];
    char padding[2];
};

struct transpositionTable
{
    TTBucket* table;
    void* mem;
    uint8_t generation;
    U64 table_size;
    U64 size_mask;
};

extern transpositionTable TT;

void start_search();
void init_tt();
void clear_tt();
void reset_tt(int MB);
int hashfull();
void storeEntry(TTEntry* entry, U64 key, Move m, int depth, int score, int staticEval, uint8_t flag);
TTEntry* probeTT(U64 key, bool& ttHit);
int score_to_tt(int score, uint16_t ply);
int tt_to_score(int score, uint16_t ply);

struct pawnhashEntry
{
    U64 pawn_hash;
    Score scores[2];
    U64 passedPawns[2];
    int pawnShelter[2];
    int kingpos[2];
    uint8_t semiopenFiles[2];
    U64 attackSpans[2];
    uint8_t castling;
};

struct materialhashEntry
{
    U64 key;
    Score score;
    int phase;
    int endgame_type;
    bool hasSpecialEndgame;
    int (*evaluation)(const Position&);
};

enum Tracing { NO_TRACE, DO_TRACE };

template <Tracing T>
class Eval {
public:
    explicit Eval(const Position& p) : pos(p) {memset(&attackedSquares, 0, sizeof(attackedSquares));}
    int value();
private:
    const Position& pos;
    pawnhashEntry* pawntte;
    void pre_eval();
    void evaluate_pawns();
    template <Color side> void evaluate_pawn_structure();
    template <Color side> Score evaluate_passers();
    template <Color side> Score king_safety() const;
    template <Color side> int pawn_shelter_score(int sq);
    template <Color side> void pawn_shelter_castling();
    template <Color side, PieceType type> Score evaluate_piece();
    template <Color side> Score evaluate_threats() const;
    U64 mobility_area[2];
    Score mobility[2] = {S(0,0), S(0,0)};
    U64 double_targets[2];
    U64 kingRings[2];
    U64 attackedSquares[14];
    int king_attackers_count[2];
    int king_attacks_count[2];
    int king_attackers_weight[2];
};

enum evalTerms { MATERIAL, MOBILITY, KNIGHTS, BISHOPS, ROOKS, QUEENS, IMBALANCE, PAWNS, PASSERS, KING_SAFETY, THREAT, TOTAL, PHASE, SCALE, TEMPO, TERM_NB };

extern Score trace_scores[TERM_NB][2];

int evaluate(const Position& p);
string trace(const Position& p);
int KBNvK(const Position& pos);

struct PSQT {
    Score psqt[14][64];
    void print();
    Score verify_score(Position* p);
};

extern PSQT psq;

typedef array<array<int16_t, 64>, 14> pieceToHistory; // std::arrays don't need annoying "new" declarations to pass pointers, unlike using int16_t**

struct searchInfo
{
    Move pv[MAX_PLY + 1];
    int ply; ///This counts UP as the search progresses
    Move chosenMove;
    Move excludedMove;
    int staticEval;
    Move killers[2];
    pieceToHistory *counterMove_history;
};

struct SearchThread
{
    Position position;
    searchInfo ss[MAX_PLY + 2];
    int16_t historyTable[2][64][64];
    Move counterMoveTable[14][64];
    pieceToHistory counterMove_history[14][64];
    int thread_id;
    int seldepth;
    U64 nodes;
    U64 tb_hits;
    int rootheight; ///this is how many ply from 0 the current root is
    pawnhashEntry pawntable[PAWN_ENTRIES];
    materialhashEntry materialTable[MATERIAL_ENTRIES];
    bool doNMP;
};

inline pawnhashEntry* get_pawntte(const Position& pos)
{
    return &pos.my_thread->pawntable[pos.pawnhash & (PAWN_HASH_SIZE_MASK)];
}

inline bool is_main_thread(Position* p) { return p->my_thread->thread_id == 0; }

extern int num_threads;
extern SearchThread main_thread;
extern SearchThread* search_threads;

inline void* get_thread(int thread_id) { return thread_id == 0 ? &main_thread : &search_threads[thread_id - 1]; }
void clear_threads();

extern int PAWN_MG;
extern int PAWN_EG;
extern int KNIGHT_MG;
extern int KNIGHT_EG;
extern int BISHOP_MG;
extern int BISHOP_EG;
extern int ROOK_MG;
extern int ROOK_EG;
extern int QUEEN_MG;
extern int QUEEN_EG;
extern int KING_MG;
extern int KING_EG;

extern Score piece_bonus[7][64];

extern int pieceValues[2][14];

extern int nonPawnValue[14];

constexpr int SCALE_OCB = 16;
constexpr int SCALE_OCB_PIECES = 27;
constexpr int SCALE_NOPAWNS = 0;
constexpr int SCALE_HARDTOWIN = 3;
constexpr int SCALE_ONEPAWN = 10;
constexpr int SCALE_NORMAL = 32;

extern Score isolated_penalty[2];
extern Score isolated_penaltyAH[2];

extern Score doubled_penalty[2];
extern Score doubled_penalty_undefended[2];

extern Score isolated_doubled_penalty[2];
extern Score isolated_doubled_penaltyAH[2];

extern Score backward_penalty[2];

//connected bonus [opposed][phalanx][rank]
extern Score connected_bonus[2][2][8];

extern Score passedRankBonus[8];

extern Score passedUnsafeBonus[2][8];

extern Score passedBlockedBonus[2][8];


extern Score knightMobilityBonus[9];

extern Score bishopMobilityBonus[14];

extern Score rookMobilityBonus[15];

extern Score queenMobilityBonus[28];


extern int attackerWeights[7];
extern int checkPenalty[7];
extern int unsafeCheckPenalty[7];
extern int queenContactCheck;
extern int kingDangerBase;
extern int kingflankAttack;
extern int kingringAttack;
extern int kingpinnedPenalty;
extern int kingweakPenalty;
extern int pawnDistancePenalty;
extern int kingShieldBonus;
extern int noQueen;


extern int kingShield[4][8];

extern int pawnStormBlocked[4][8];

extern int pawnStormFree[4][8];

extern int bishop_pair;

extern Score bishopPawns;
extern Score rookFile[2];
extern Score battery;
extern Score kingProtector;
extern Score outpostBonus;
extern Score reachableOutpost;
extern Score minorThreat[7];
extern Score rookThreat[7];
extern Score kingThreat;
extern Score kingMultipleThreat;
extern Score pawnPushThreat;
extern Score safePawnThreat;
extern Score hangingPiece;
extern Score defendedRookFile;
extern Score rank7Rook;

extern Score passedFriendlyDistance[8];
extern Score passedEnemyDistance[8];

extern Score tarraschRule_friendly[8];
extern Score tarraschRule_enemy;

constexpr int tempo = 30;

constexpr U64 trappedBishop[2][64] = // Color x Square
{
    {
        BITSET(B2), BITSET(C2), 0, 0, 0, 0, BITSET(F2), BITSET(G2),
        BITSET(B3), 0         , 0, 0, 0, 0, 0         , BITSET(G3),
        0         , 0         , 0, 0, 0, 0, 0         , 0         ,
        0         , 0         , 0, 0, 0, 0, 0         , 0         ,
        0         , 0         , 0, 0, 0, 0, 0         , 0         ,
        BITSET(B5), 0         , 0, 0, 0, 0, 0         , BITSET(G5),
        BITSET(B6), 0         , 0, 0, 0, 0, 0         , BITSET(G6),
        BITSET(B7), BITSET(C7), 0, 0, 0, 0, BITSET(F7), BITSET(G7)
    },
    {
        BITSET(B2), BITSET(C2), 0, 0, 0, 0, BITSET(F2), BITSET(G2),
        BITSET(B3), 0         , 0, 0, 0, 0, 0         , BITSET(G3),
        BITSET(B4), 0         , 0, 0, 0, 0, 0         , BITSET(G4),
        0         , 0         , 0, 0, 0, 0, 0         , 0         ,
        0         , 0         , 0, 0, 0, 0, 0         , 0         ,
        0         , 0         , 0, 0, 0, 0, 0         , 0         ,
        BITSET(B6), 0         , 0, 0, 0, 0, 0         , BITSET(G6),
        BITSET(B7), BITSET(C7), 0, 0, 0, 0, BITSET(F7), BITSET(G7)
    }
};

constexpr U64 veryTrappedBishop[64] =
{
    BITSET(B3), BITSET(B3), 0, 0, 0, 0, BITSET(G3), BITSET(G3),
    BITSET(B3), 0         , 0, 0, 0, 0, 0         , BITSET(F2),
    BITSET(B4), 0         , 0, 0, 0, 0, 0         , BITSET(F3),
    0         , 0         , 0, 0, 0, 0, 0         , 0         ,
    0         , 0         , 0, 0, 0, 0, 0         , 0         ,
    BITSET(C6), 0         , 0, 0, 0, 0, 0         , BITSET(F6),
    BITSET(C7), 0         , 0, 0, 0, 0, 0         , BITSET(F7),
    BITSET(B6), BITSET(B6), 0, 0, 0, 0, BITSET(G6), BITSET(G6)
};

constexpr U64 knightOpposingBishop[2][64] = //Color x Square
{
    {
        0         , 0         , 0         , 0         , 0         , 0         , 0         , 0         ,
        BITSET(A5), BITSET(B5), BITSET(C5), BITSET(D5), BITSET(E5), BITSET(F5), BITSET(G5), BITSET(H5),
        BITSET(A6), BITSET(B6), BITSET(C6), BITSET(D6), BITSET(E6), BITSET(F6), BITSET(G6), BITSET(H6),
        BITSET(A7), BITSET(B7), BITSET(C7), BITSET(D7), BITSET(E7), BITSET(F7), BITSET(G7), BITSET(H7),
        BITSET(A8), BITSET(B8), BITSET(C8), BITSET(D8), BITSET(E8), BITSET(F8), BITSET(G8), BITSET(H8),
        0         , 0         , 0         , 0         , 0         , 0         , 0         , 0         ,
        0         , 0         , 0         , 0         , 0         , 0         , 0         , 0         ,
        0         , 0         , 0         , 0         , 0         , 0         , 0         , 0
    },
    {
        0         , 0         , 0         , 0         , 0         , 0         , 0         , 0         ,
        0         , 0         , 0         , 0         , 0         , 0         , 0         , 0         ,
        0         , 0         , 0         , 0         , 0         , 0         , 0         , 0         ,
        BITSET(A1), BITSET(B1), BITSET(C1), BITSET(D1), BITSET(E1), BITSET(F1), BITSET(G1), BITSET(H1),
        BITSET(A2), BITSET(B2), BITSET(C2), BITSET(D2), BITSET(E2), BITSET(F2), BITSET(G2), BITSET(H2),
        BITSET(A3), BITSET(B3), BITSET(C3), BITSET(D3), BITSET(E3), BITSET(F3), BITSET(G3), BITSET(H3),
        BITSET(A4), BITSET(B4), BITSET(C4), BITSET(D4), BITSET(E4), BITSET(F4), BITSET(G4), BITSET(H4),
        0         , 0         , 0         , 0         , 0         , 0         , 0         , 0
    }
};

constexpr U64 knightOpposingBishop_leftRight[64] =
{
    0, 0, 0, BITSET(A1), BITSET(H1), 0, 0, 0,
    0, 0, 0, BITSET(A2), BITSET(H2), 0, 0, 0,
    0, 0, 0, BITSET(A3), BITSET(H3), 0, 0, 0,
    0, 0, 0, BITSET(A4), BITSET(H4), 0, 0, 0,
    0, 0, 0, BITSET(A5), BITSET(H5), 0, 0, 0,
    0, 0, 0, BITSET(A6), BITSET(H6), 0, 0, 0,
    0, 0, 0, BITSET(A7), BITSET(H7), 0, 0, 0,
    0, 0, 0, BITSET(A8), BITSET(H8), 0, 0, 0
};

extern Score bishopOpposerBonus;
extern Score trappedBishopPenalty;
extern Score veryTrappedBishopPenalty;


struct Parameter
{
    Score* address;
    int* constaddress;
    int value;
    int flag; // 0:mg 1:eg 2:const
    int stability;
    int valuedelta;
    double errordelta;

    bool operator<(const Parameter p) const { return (errordelta < p.errordelta); }
    bool operator>(const Parameter p) const { return (errordelta > p.errordelta); }
};

//imbalance weights
extern int my_pieces[5][5];
extern int opponent_pieces[5][5];

enum endgameType { NORMAL_ENDGAME, DRAW_ENDGAME };

inline materialhashEntry* get_materialEntry(const Position& p) { return &p.my_thread->materialTable[p.materialhash & MATERIAL_HASH_SIZE_MASK]; }

void init_imbalance();
int qSearch(SearchThread* thread, searchInfo* info, int depth, int alpha, const int beta);
void find_optimal_k();
void testThings();
void tune();

const int futility_move_counts[2][9] = {
    {0, 3, 4, 5,  8, 13, 17, 23, 29}, // not improving
    {0, 5, 7, 10, 16, 24, 33, 44, 58}, // improving
};

constexpr int ASPIRATION_INIT = 10;
constexpr int futility_history_limit[2] = { 16000, 9000 };
constexpr int RazoringMarginByDepth[3] = {0, 180, 350};
constexpr int ProbCutMargin = 90;

constexpr int strongHistory = 13000;
constexpr int singularDepth = 8;

extern int reductions[2][64][64];

inline int lmr(bool improving, int depth, int num_moves) {
    return reductions[improving][std::min(depth, 63)][std::min(num_moves, 63)];
}

extern struct timeval curr_time, start_ts;
extern int startTime;

extern int timer_count,
ideal_usage,
max_usage,
think_depth_limit;

extern bool is_movetime;

extern volatile bool is_timeout,
quit_application,
is_searching,
is_pondering;

int getRealTime();

inline int time_passed() {
    return getRealTime() - startTime;
}

inline U64 sum_nodes() {
    U64 s = 0;
    for (int i = 0; i < num_threads; ++i) {
        SearchThread* t = (SearchThread*)get_thread(i);
        s += t->nodes;
    }
    return s;
}

inline U64 sum_tb_hits() {
    U64 s = 0;
    for (int i = 0; i < num_threads; ++i) {
        SearchThread *t = (SearchThread*)get_thread(i);
        s += t->tb_hits;
    }
    return s;
}

inline void initialize_nodes() {
    for (int i = 0; i < num_threads; ++i) {
        SearchThread* t = (SearchThread*)get_thread(i);
        t->nodes = 0;
        t->tb_hits = 0;
    }
}

typedef struct timeTuple { // A kind of makeshift tuple
    int optimum_time;
    int maximum_time;
} timeTuple;

timeTuple get_myremain(int increment, int remaining, int movestogo, int root_ply);

struct timeInfo {
    int movesToGo;
    int totalTimeLeft;
    int increment;
    int movetime;
    int depthlimit;
    bool timelimited;
    bool depthlimited;
    bool infinite;
};

timeTuple calculate_time();

extern int move_overhead;

template <bool Root> U64 Perft(Position& pos, int depth);


void *think(void* pos);
void loop();


constexpr U64 rookMagics[] = { 0x0a80004000801220, 0x8040004010002008, 0x2080200010008008, 0x1100100008210004, 0xc200209084020008, 0x2100010004000208, 0x0400081000822421, 0x0200010422048844,
                                0x0800800080400024, 0x0001402000401000, 0x3000801000802001, 0x4400800800100083, 0x0904802402480080, 0x4040800400020080, 0x0018808042000100, 0x4040800080004100,
                                0x0040048001458024, 0x00a0004000205000, 0x3100808010002000, 0x4825010010000820, 0x5004808008000401, 0x2024818004000a00, 0x0005808002000100, 0x2100060004806104,
                                0x0080400880008421, 0x4062220600410280, 0x010a004a00108022, 0x0000100080080080, 0x0021000500080010, 0x0044000202001008, 0x0000100400080102, 0xc020128200040545,
                                0x0080002000400040, 0x0000804000802004, 0x0000120022004080, 0x010a386103001001, 0x9010080080800400, 0x8440020080800400, 0x0004228824001001, 0x000000490a000084,
                                0x0080002000504000, 0x200020005000c000, 0x0012088020420010, 0x0010010080080800, 0x0085001008010004, 0x0002000204008080, 0x0040413002040008, 0x0000304081020004,
                                0x0080204000800080, 0x3008804000290100, 0x1010100080200080, 0x2008100208028080, 0x5000850800910100, 0x8402019004680200, 0x0120911028020400, 0x0000008044010200,
                                0x0020850200244012, 0x0020850200244012, 0x0000102001040841, 0x140900040a100021, 0x000200282410a102, 0x000200282410a102, 0x000200282410a102, 0x4048240043802106 };

constexpr U64 bishopMagics[] = { 0x40106000a1160020, 0x0020010250810120, 0x2010010220280081, 0x002806004050c040, 0x0002021018000000, 0x2001112010000400, 0x0881010120218080, 0x1030820110010500,
                                0x0000120222042400, 0x2000020404040044, 0x8000480094208000, 0x0003422a02000001, 0x000a220210100040, 0x8004820202226000, 0x0018234854100800, 0x0100004042101040,
                                0x0004001004082820, 0x0010000810010048, 0x1014004208081300, 0x2080818802044202, 0x0040880c00a00100, 0x0080400200522010, 0x0001000188180b04, 0x0080249202020204,
                                0x1004400004100410, 0x00013100a0022206, 0x2148500001040080, 0x4241080011004300, 0x4020848004002000, 0x10101380d1004100, 0x0008004422020284, 0x01010a1041008080,
                                0x0808080400082121, 0x0808080400082121, 0x0091128200100c00, 0x0202200802010104, 0x8c0a020200440085, 0x01a0008080b10040, 0x0889520080122800, 0x100902022202010a,
                                0x04081a0816002000, 0x0000681208005000, 0x8170840041008802, 0x0a00004200810805, 0x0830404408210100, 0x2602208106006102, 0x1048300680802628, 0x2602208106006102,
                                0x0602010120110040, 0x0941010801043000, 0x000040440a210428, 0x0008240020880021, 0x0400002012048200, 0x00ac102001210220, 0x0220021002009900, 0x84440c080a013080,
                                0x0001008044200440, 0x0004c04410841000, 0x2000500104011130, 0x1a0c010011c20229, 0x0044800112202200, 0x0434804908100424, 0x0300404822c08200, 0x48081010008a2a80 };



struct PolyglotEntry
{
    U64 key;
    uint16_t bestMove;
    uint16_t weight;
    uint32_t learn;
};

class OpeningBook
{
public:
    Move probe(Position& pos);
    void set_max_depth(int depth);
    void set_use_best(bool useBest);
    OpeningBook();
    ~OpeningBook();
    void init(string& path);
private:
    U64 polyglotKey(const Position& pos);
    Move polyglot_to_move(const Position& pos, unsigned short pgMove);

    PRNG* randomGen;

    int firstEntry;
    int bestEntry;
    int randomEntry;

    int numEntries;

    void correctEndian(PolyglotEntry* entry);

    bool is_little_endian();
    uint64_t swap_uint64(uint64_t d);
    uint32_t swap_uint32(uint32_t d);
    uint16_t swap_uint16(uint16_t d);

    bool check_do_search(const Position& pos);

    int find_first_key(U64 key);
    int get_key_data();

    U64 lastPosition;
    U64 currentPosition;
    int currentCount;
    int lastCount;

    int plyUsed; // how far into the book are we?
    int failCount; // how many failures have we had?

    PolyglotEntry* bookArray;

    bool use_best_move;
    int max_depth;

    bool isLittleEndian;
    bool bookEnabled; // uci setting
    bool searchBook; // stop searching after a set number of book failures
};

extern OpeningBook book;
extern string openingBookPath;

void bench();

//Ethereal bench positions
const std::string benchmarks[36] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
    "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
    "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
    "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
    "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
    "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
    "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
    "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
    "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22",
    "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - - 0 1",
    "3b4/5kp1/1p1p1p1p/pP1PpP1P/P1P1P3/3KN3/8/8 w - - 0 1",
    "8/6pk/1p6/8/PP3p1p/5P2/4KP1q/3Q4 w - - 0 1",
    "7k/3p2pp/4q3/8/4Q3/5Kp1/P6b/8 w - - 0 1",
    "8/2p5/8/2kPKp1p/2p4P/2P5/3P4/8 w - - 0 1",
    "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1",
    "8/pp2r1k1/2p1p3/3pP2p/1P1P1P1P/P5KR/8/8 w - - 0 1",
    "8/3p4/p1bk3p/Pp6/1Kp1PpPp/2P2P1P/2P5/5B2 b - - 0 1",
    "5k2/7R/4P2p/5K2/p1r2P1p/8/8/8 b - - 0 1",
    "6k1/6p1/P6p/r1N5/5p2/7P/1b3PP1/4R1K1 w - - 0 1",
    "1r3k2/4q3/2Pp3b/3Bp3/2Q2p2/1p1P2P1/1P2KP2/3N4 w - - 0 1",
    "6k1/4pp1p/3p2p1/P1pPb3/R7/1r2P1PP/3B1P2/6K1 w - - 0 1",
    "8/3p3B/5p2/5P2/p7/PP5b/k7/6K1 w - - 0 1",
    "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",
    "8/8/8/5N2/8/p7/8/2NK3k w - - 0 1",
    "8/3k4/8/8/8/4B3/4KB2/2B5 w - - 0 1",
    "8/8/1P6/5pr1/8/4R3/7k/2K5 w - - 0 1",
    "8/2p4P/8/kr6/6R1/8/8/1K6 w - - 0 1",
    "8/8/3P3k/8/1p6/8/1P6/1K3n2 b - - 0 1",
    "8/R7/2q5/8/6k1/8/1P5p/K6R w - - 0 124",
    "6k1/3b3r/1p1p4/p1n2p2/1PPNpP1q/P3Q1p1/1R1RB1P1/5K2 b - - 0 1",
    "r2r1n2/pp2bk2/2p1p2p/3q4/3PN1QP/2P3R1/P4PP1/5RK1 w - - 0 1",
};

unsigned tablebasesProbeWDL(Position *pos, int depth, int height);
bool tablebasesProbeDTZ(Position *pos, Move *best, Move *ponder);
