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


void init_values()
{
    for (int i = 0; i<64; i++)
    {
        psq.psqt[WPAWN][i] = (piece_bonus[PAWN][FLIP_SQUARE(WHITE, i)] + S(PAWN_MG, PAWN_EG)) * S2MSIGN(WHITE);
        psq.psqt[BPAWN][i] = (piece_bonus[PAWN][FLIP_SQUARE(BLACK, i)] + S(PAWN_MG, PAWN_EG)) * S2MSIGN(BLACK);
        psq.psqt[WKNIGHT][i] = (piece_bonus[KNIGHT][FLIP_SQUARE(WHITE, i)] + S(KNIGHT_MG, KNIGHT_EG)) * S2MSIGN(WHITE);
        psq.psqt[BKNIGHT][i] = (piece_bonus[KNIGHT][FLIP_SQUARE(BLACK, i)] + S(KNIGHT_MG, KNIGHT_EG)) * S2MSIGN(BLACK);
        psq.psqt[WBISHOP][i] = (piece_bonus[BISHOP][FLIP_SQUARE(WHITE, i)] + S(BISHOP_MG, BISHOP_EG)) * S2MSIGN(WHITE);
        psq.psqt[BBISHOP][i] = (piece_bonus[BISHOP][FLIP_SQUARE(BLACK, i)] + S(BISHOP_MG, BISHOP_EG)) * S2MSIGN(BLACK);
        psq.psqt[WROOK][i] = (piece_bonus[ROOK][FLIP_SQUARE(WHITE, i)] + S(ROOK_MG, ROOK_EG)) * S2MSIGN(WHITE);
        psq.psqt[BROOK][i] = (piece_bonus[ROOK][FLIP_SQUARE(BLACK, i)] + S(ROOK_MG, ROOK_EG)) * S2MSIGN(BLACK);
        psq.psqt[WQUEEN][i] = (piece_bonus[QUEEN][FLIP_SQUARE(WHITE, i)] + S(QUEEN_MG, QUEEN_EG)) * S2MSIGN(WHITE);
        psq.psqt[BQUEEN][i] = (piece_bonus[QUEEN][FLIP_SQUARE(BLACK, i)] + S(QUEEN_MG, QUEEN_EG)) * S2MSIGN(BLACK);
        psq.psqt[WKING][i] = (piece_bonus[KING][FLIP_SQUARE(WHITE, i)] + S(KING_MG, KING_EG)) * S2MSIGN(WHITE);
        psq.psqt[BKING][i] = (piece_bonus[KING][FLIP_SQUARE(BLACK, i)] + S(KING_MG, KING_EG)) * S2MSIGN(BLACK);
    }

    #if 0
    pieceValues[MG][0] = pieceValues[MG][1] = 0;
    pieceValues[MG][WPAWN] = pieceValues[MG][BPAWN] = PAWN_MG;
    pieceValues[MG][WKNIGHT] = pieceValues[MG][BKNIGHT] = KNIGHT_MG;
    pieceValues[MG][WBISHOP] = pieceValues[MG][BBISHOP] = BISHOP_MG;
    pieceValues[MG][WROOK] = pieceValues[MG][BROOK] = ROOK_MG;
    pieceValues[MG][WQUEEN] = pieceValues[MG][BQUEEN] = QUEEN_MG;
    pieceValues[MG][WKING] = pieceValues[MG][BKING] = 0;

    pieceValues[EG][0] = pieceValues[EG][1] = 0;
    pieceValues[EG][WPAWN] = pieceValues[EG][BPAWN] = PAWN_EG;
    pieceValues[EG][WKNIGHT] = pieceValues[EG][BKNIGHT] = KNIGHT_EG;
    pieceValues[EG][WBISHOP] = pieceValues[EG][BBISHOP] = BISHOP_EG;
    pieceValues[EG][WROOK] = pieceValues[EG][BROOK] = ROOK_EG;
    pieceValues[EG][WQUEEN] = pieceValues[EG][BQUEEN] = QUEEN_EG;
    pieceValues[EG][WKING] = pieceValues[EG][BKING] = 0;

    nonPawnValue[0] = nonPawnValue[1] = 0;
    nonPawnValue[WPAWN] = nonPawnValue[BPAWN] = 0;
    nonPawnValue[WKNIGHT] = nonPawnValue[BKNIGHT] = KNIGHT_MG;
    nonPawnValue[WBISHOP] = nonPawnValue[BBISHOP] = BISHOP_MG;
    nonPawnValue[WROOK] = nonPawnValue[BROOK] = ROOK_MG;
    nonPawnValue[WQUEEN] = nonPawnValue[BQUEEN] = QUEEN_MG;
    nonPawnValue[WKING] = nonPawnValue[BKING] = 0;
    #endif

    for (int victim = BLANK; victim <= BKING; victim++)
        for (int attacker = 0; attacker <= BKING; attacker++)
        {
            mvvlva[victim][attacker] = pieceValues[MG][victim] - attacker;
        }
}

Score PSQT::verify_score(Position *p)
{
    Score out = S(0,0);
    int sq;

    for (int pc = WPAWN; pc <= BKING; pc++)
    {
        U64 pieces = p->pieceBB[pc];
        while (pieces)
        {
            sq = popLsb(&pieces);
            out += psqt[pc][sq];
        }
    }

    return out;
}

void PSQT::print()
{
    printf("psqt[14][64] = {");
    for (int i = 0; i<14; i++)
    {
        printf("\n{");
        for (int j = 0; j<64; j++)
        {
            if (j % 8 == 0)
                printf("\n    ");
            printf("S(%d,%d)", mg_value(psqt[i][j]), eg_value(psqt[i][j]));
            if (j < 63)
                printf(", ");
        }
        printf("\n}");
        if (i<13)
            printf(",");
    }
    printf("\n};\n\n");
}
