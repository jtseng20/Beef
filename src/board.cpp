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

U64 PAWN_ATTACKS[2][64];
U64 RANK_MASKS[64];
U64 FILE_MASKS[64];
U64 DIAG_MASKS[64];
U64 ANTIDIAG_MASKS[64];
U64 BETWEEN_MASKS[64][64];
U64 PAWN_2PUSHES[2][64];
U64 PAWN_PUSHES[2][64];
U64 PAWN_2PUSHESFROM[2][64]; //pawn pushes that go TO index
U64 PAWN_PUSHESFROM[2][64];
U64 PAWN_ATTACKSFROM[2][64];
U64 SQUARE_MASKS[64];
U64 BISHOP_MASKS[64];
U64 ROOK_MASKS[64];
U64 RAY_MASKS[64][64];
U64 PseudoAttacks[7][64];
U64 castlekingwalk[4];
U64 epthelper[64];
U64 castlerights[64];
U64 neighborMasks[64];
U64 passedPawnMasks[2][64];
U64 pawnBlockerMasks[2][64];
U64 phalanxMasks[64];
U64 kingRing[64];
int mvvlva[14][14];
U64 distanceRings[64][8];
U64 colorMasks[64];
int squareDistance[64][64];
int reductions[2][64][64];
uint8_t PopCnt16[1 << 16];

materialhashEntry materialHashTable[9*3*3*3*2*9*3*3*3*2];

//Fancy Magic Bitboards
U64 Battacks[5248];
U64 Rattacks[102400];

SMagic mBishopTbl[64];
SMagic mRookTbl[64];

void init_boards()
{
    for (unsigned i = 0; i < (1 << 16); ++i)
        PopCnt16[i] = uint8_t(bitset<16>(i).count());

    for (int i = 1; i < 64; i++)
        for (int j = 1; j < 64; j++)
    {
        reductions[0][i][j] = int(1 + round(log(1.5*i) * log(j) * 0.55));
        reductions[1][i][j] = int(round(log(1.5*i) * log(2*j) * 0.4));
    }

    for (int i = 0; i < 64; i++)
    {
        castlerights[i] = ~0;
        int rank = RANK(i);
        int file = FILE(i);
        if (rank == 0 && (file == 0 || file == 4))
            castlerights[i] &= ~WQCMASK;
        if (rank == 0 && (file == 7 || file == 4))
            castlerights[i] &= ~WKCMASK;
        if (rank == 7 && (file == 0 || file == 4))
            castlerights[i] &= ~BQCMASK;
        if (rank == 7 && (file == 7 || file == 4))
            castlerights[i] &= ~BKCMASK;
    }

    memset(&distanceRings, 0, sizeof(distanceRings));
    for (int i = 0; i < 64; i++)
    {
        for (int j = 0; j < 64; j++)
        {
            if (i != j)
            {
                int rDistance = abs(RANK(i) - RANK(j));
                int fDistance = abs(FILE(i) - FILE(j));
                int distance = max(rDistance, fDistance);
                distanceRings[i][distance - 1] |= BITSET(j);
            }
        }
    }

    U64 passedPawnTemp[2][64];
    U64 kingRing_temp[64];
    for (int i = 0; i < 64; i++)
    {
        PseudoAttacks[KING][i] = 0ULL;
        PseudoAttacks[KNIGHT][i] = 0ULL;
        RANK_MASKS[i] = 0ULL;
        FILE_MASKS[i] = 0ULL;
        DIAG_MASKS[i] = 0ULL;
        ANTIDIAG_MASKS[i] = 0ULL;
        neighborMasks[i] = 0ULL;
        passedPawnMasks[0][i] = passedPawnMasks[1][i] = FileBB[FILE(i)];
        passedPawnTemp[0][i] = passedPawnTemp[1][i] = 0ULL;
        pawnBlockerMasks[0][i] = pawnBlockerMasks[1][i] = FileBB[FILE(i)];
        phalanxMasks[i] = 0ULL;
        PAWN_ATTACKS[0][i] = PAWN_ATTACKS[1][i] = 0ULL;
        PAWN_PUSHES[0][i] = PAWN_PUSHES[1][i] = 0ULL;
        PAWN_2PUSHES[0][i] = PAWN_2PUSHES[1][i] = 0ULL;
        PAWN_2PUSHESFROM[0][i] = PAWN_2PUSHESFROM[1][i] = 0ULL;
        PAWN_PUSHESFROM[0][i] = PAWN_PUSHESFROM[1][i] = 0ULL;
        PAWN_ATTACKSFROM[0][i] = PAWN_ATTACKSFROM[1][i] = 0ULL;
        kingRing_temp[i] = BITSET(i);
        SQUARE_MASKS[i] = BITSET(i);
        colorMasks[i] = (i % 2 == 0) ? DarkSquares : ~DarkSquares;

        for (int j = 0; j<64; j++)
        {
            squareDistance[i][j] = min(abs(FILE(i) - FILE(j)) , abs(RANK(i) - RANK(j)));
            BETWEEN_MASKS[i][j] = 0ULL;
            RAY_MASKS[i][j] = 0ULL;
            if (FILE(i) == FILE(j) && i != j)
            {
                FILE_MASKS[i] |= BITSET(j);
                for (int k = min(RANK(i), RANK(j)) + 1; k < max(RANK(i), RANK(j)); k++)
                    BETWEEN_MASKS[i][j] |= BITSET(INDEX(k, FILE(i)));
            }
            if (RANK(i) == RANK(j) && i != j)
            {
                RANK_MASKS[i] |= BITSET(j);
                for (int k = min(FILE(i), FILE(j)) + 1; k < max(FILE(i), FILE(j)); k++)
                    BETWEEN_MASKS[i][j] |= BITSET(INDEX(RANK(i), k));
            }
            if (abs(RANK(i) - RANK(j)) == abs(FILE(i) - FILE(j)) && i != j)
            {
                int dx = (FILE(i) < FILE(j) ? 1 : -1);
                int dy = (RANK(i) < RANK(j) ? 1 : -1);
                for (int k = 1; FILE(i) + k * dx != FILE(j); k++)
                {
                    BETWEEN_MASKS[i][j] |= BITSET(INDEX((RANK(i) + k*dy), (FILE(i) + k*dx)));
                }
                if(abs(dx + dy) == 2)
                {
                    DIAG_MASKS[i] |= BITSET(j);
                }
                else
                {
                    ANTIDIAG_MASKS[i] |= BITSET(j);
                }
            }
        }

        ROOK_MASKS[i] = (RANK_MASKS[i] & ~ (FileABB | FileHBB) )| (FILE_MASKS[i] & ~ (Rank1BB | Rank8BB));
        BISHOP_MASKS[i] = (DIAG_MASKS[i] | ANTIDIAG_MASKS[i]) & ~ (FileABB | FileHBB | Rank1BB | Rank8BB);
        int to;

        for(int j = 0; j < 8; j++)
        {
            to = i + orthogonalanddiagonaloffset[j];
            if(to >= 0 && to < 64 && abs(FILE(i) - FILE(to)) <= 1)
                PseudoAttacks[KING][i] |= BITSET(to);
            to = i + knightoffset[j];
            if(to >= 0 && to < 64 && abs(FILE(i) - FILE(to)) <= 2)
                PseudoAttacks[KNIGHT][i] |= BITSET(to);
        }

        kingRing_temp[i] |= PseudoAttacks[KING][i];

        for(int s = 0; s < 2; s++)
        {
            if(RRANK(i, s) < 7)
                PAWN_PUSHES[s][i] |= BITSET(i + S2MSIGN(s) * 8);
            if(RRANK(i, s) > 0)
                PAWN_PUSHESFROM[s][i] |= BITSET(i - S2MSIGN(s) * 8);
            if(RRANK(i, s) == 1)
                PAWN_2PUSHES[s][i] |= BITSET(i + S2MSIGN(s) * 16);
            if(RRANK(i,s) == 3)
                PAWN_2PUSHESFROM[s][i] |= BITSET(i - S2MSIGN(s) * 16);

            for(int j = i + S2MSIGN(s) *8; 0 <= j && j <= 63; j += S2MSIGN(s) *8 )
            {
                passedPawnTemp[s][i] |= RankBB[RANK(j)];
            }

            if (FILE(i) != 0)
            {
                neighborMasks[i] |= FileBB[FILE(i - 1)];
                passedPawnMasks[s][i] |= FileBB[FILE(i - 1)];
                phalanxMasks[i] |= BITSET(i - 1);
            }

            if (FILE(i) != 7)
            {
                neighborMasks[i] |= FileBB[FILE(i + 1)];
                passedPawnMasks[s][i] |= FileBB[FILE(i + 1)];
                phalanxMasks[i] |= BITSET(i + 1);
            }

            for(int d = -1; d <= 1; d++)
            {
                to = i + S2MSIGN(s) * 8 + d;

                if(d && abs(FILE(i) - FILE(to)) <= 1 && to >= 0 && to < 64)
                    PAWN_ATTACKS[s][i] |= BITSET(to);
                to = i - S2MSIGN(s) * 8 + d;
                if(d && abs(FILE(i) - FILE(to)) <= 1 && to >= 0 && to < 64)
                    PAWN_ATTACKSFROM[s][i] |= BITSET(to);
            }
            passedPawnMasks[s][i] &= passedPawnTemp[s][i];
            pawnBlockerMasks[s][i] &= passedPawnTemp[s][i];
        }
        epthelper[i] = 0ULL;
        if (RANK(i) == 3 || RANK(i) == 4)
        {
            if (RANK(i - 1) == RANK(i))
                epthelper[i] |= BITSET(i-1);
            if (RANK(i + 1) == RANK(i))
                epthelper[i] |= BITSET(i+1);
        }
    }

    for (int i = 0; i < 64; i++)
    {
        int r = max(1, min(RANK(i), 6));
        int f = max(1, min(FILE(i), 6));
        kingRing[i] = kingRing_temp[INDEX(r,f)];
    }

    init_magics(Rattacks, mRookTbl, ROOK_MASKS, rookAttacks_slow, rookMagics);
    init_magics(Battacks, mBishopTbl, BISHOP_MASKS, bishopAttacks_slow, bishopMagics);

    for (int i = 0; i < 64; i++)
    {
        PseudoAttacks[QUEEN][i] = PseudoAttacks[BISHOP][i] = bishopAttacks(0, i);
        PseudoAttacks[QUEEN][i] |= PseudoAttacks[ROOK][i] = rookAttacks(0, i);
        for (int j = 0; j < 64; j++)
        {
            if (PseudoAttacks[ROOK][i] & BITSET(j))
            {
                RAY_MASKS[i][j] |= (PseudoAttacks[ROOK][i] & rookAttacks(0, j)) | BITSET(i) | BITSET(j);
            }
            if (PseudoAttacks[BISHOP][i] & BITSET(j))
            {
                RAY_MASKS[i][j] |= (PseudoAttacks[BISHOP][i] & bishopAttacks(0, j)) | BITSET(i) | BITSET(j);
            }
        }
    }
    for (int i = 0; i< 4; i++)
    {
        int side = i/2;
        castlekingwalk[i] = BETWEEN_MASKS[4 + 56 * side][castleKingTo[i]] | BITSET(castleKingTo[i]);
    }

}




int imbalance(const int piece_count[5][5], Color color) {
    int bonus = 0;

    // Second-degree polynomial material imbalance, by Tord Romstad
    for (int pt1 = 0; pt1 < 5; ++pt1) {
        if (!piece_count[color][pt1]) {
            continue;
        }

        for (int pt2 = 0; pt2 <= pt1; ++pt2) {
            bonus += my_pieces[pt1][pt2] * piece_count[color][pt1] * piece_count[color][pt2] +
                     opponent_pieces[pt1][pt2] * piece_count[color][pt1] * piece_count[~color][pt2];
        }
    }

    return bonus;
}

/*
void init_imbalance(){
    for (int wp = 0 ; wp < 9 ; wp++) {
        for (int wn = 0 ; wn < 3 ; wn++) {
            for (int wb = 0 ; wb < 3 ; wb++) {
                for (int wr = 0 ; wr < 3 ; wr++) {
                    for (int wq = 0 ; wq < 2 ; wq++){
                        for (int bp = 0 ; bp < 9 ; bp++) {
                            for (int bn = 0 ; bn < 3 ; bn++) {
                                for (int bb = 0 ; bb < 3 ; bb++) {
                                    for (int br = 0 ; br < 3 ; br++) {
                                        for (int bq = 0 ; bq < 2 ; bq++){

        int index = wq * material_balance[WQUEEN]  +
                    bq * material_balance[BQUEEN]  +
                    wr * material_balance[WROOK]   +
                    br * material_balance[BROOK]   +
                    wb * material_balance[WBISHOP] +
                    bb * material_balance[BBISHOP] +
                    wn * material_balance[WKNIGHT] +
                    bn * material_balance[BKNIGHT] +
                    wp * material_balance[WPAWN]   +
                    bp * material_balance[BPAWN];
        materialhashEntry *material = &materialHashTable[index];
        material->phase = ((24 - (wb + bb + wn + bn) - 2*(wr + br) - 4*(wq + bq)) * 255 + 12) / 24;

        const int piece_count[2][5] = {
            { wp, wn, wb, wr, wq },
            { bp, bn, bb, br, bq }
        };

        int value = (imbalance(piece_count, WHITE) - imbalance(piece_count, BLACK)) / 16;

        // Bishop pair
        if (wb > 1) {
            value += bishop_pair;
        }
        if (bb > 1) {
            value -= bishop_pair;
        }

        material->score = S(value, value);



        // Endgames
        int white_minor = wn + wb;
        int white_major = wr + wq;
        int black_minor = bn + bb;
        int black_major = br + bq;
        int all_minor = white_minor + black_minor;
        int all_major = white_major + black_major;
        bool no_pawns = wp == 0 && bp == 0;

        material->endgame_type = NORMAL_ENDGAME;

        if (wp + bp + all_minor + all_major == 0) {
            material->endgame_type = DRAW_ENDGAME;
        }
        else if (no_pawns && all_major == 0 && white_minor < 2 && black_minor < 2) {
            material->endgame_type = DRAW_ENDGAME;
        }
        else if (no_pawns && all_major == 0 && all_minor == 2 && (wn == 2 || bn == 2)) {
            material->endgame_type = DRAW_ENDGAME;
        }

        material->scale = -1; //HACK: Sentinel
        material->hasSpecialEndgame = false;
        material->evaluation = nullptr;

        /// Special endgame for KBNvK
        if (no_pawns && all_major == 0 && all_minor == 2 && ((wn == 1 && wb == 1) || (bn == 1 && bb == 1)))
        {
            material->hasSpecialEndgame = true;
            material->evaluation = &KBNvK;
        }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}*/
