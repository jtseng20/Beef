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
#ifdef __TUNE__
#include "Beef.h"

/// Choose a tuning method. Turn off both to use simple gradient descent.
#define RMSProp 1
#define adam 0

#define tuneMobility 1
#define tuneWeights 0
#define tuneImbalance 0
#define tunePassed 0
#define tuneKing 0
#define tuneThreat 0
#define tunePawns 0
#define tuneMinors 0
#define tuneRooks 0

/// RMSProp / adam tuner used for analyzing very large datasets.
/// Thanks to Andrew Grant for doing the math, and providing an example framework with his own tuner.

/// In my implementation, RMSProp has been historically stronger in high-gradient fields, due to higher momentum
/// and adam is stronger where the weights are believed to be close to optimal values.

constexpr int NTERMS = 629; // Number of weights to be tuned (I don't change this; coefficients that aren't exposed for tuning are just zeroed)
constexpr int EPOCHS = 10000; // How many iterations to tune over
constexpr double LR = 2.0; // Learning rate
constexpr double RMS_E = 0.9; // Probably don't modify this one
constexpr double decayrate = 0.93; // The learning rate is scaled down this much each decay step
constexpr int decaySteps = 500; // How often to lower the learning rate
constexpr int reportSteps = 100; // How often to print out weights
constexpr double adam_b1 = 0.9; // Probably don't modify this one
constexpr double adam_b2 = 0.999; // Probably don't modify this one
constexpr int cutStep = 600; // In case we want to start with a very high learning rate and drop it suddenly at a certain point

#define NFENS 9999740
#define NTHREADS 7
#define BATCHSIZE 16384


extern tunerTrace Trace;

struct tunerTuple
{
    uint16_t index;
    int8_t wcoeff;
    int8_t bcoeff;
};

struct RMStunerEntry
{
    double actualscore;
    bool activeSide;
    int phase;
    double pfactors[2]; // saved for gradient calculation
    double scale;
    int staticEval; // the merged version of originalScore
    Score originalScore; // the unmerged Score before phasing
    int16_t ntuples;
    tunerTuple* tuples;
    //int safety[2];
};

RMStunerEntry *RMStuningData;

typedef double TVector[NTERMS][2]; // indexed MG, EG

TVector params; // indexed MG, EG
int8_t coeffs[NTERMS][2]; // indexed WHITE, BLACK
TVector deltas = {0}; // indexed MG, EG
#if RMSProp
TVector RMS = {0}; // indexed MG, EG
#endif
#if adam
TVector adam_m = {0};
TVector adam_v = {0};
#endif

double K = 0.912078;

double sigmoid(double s, double k) {
    return 1.0 / (1.0 + pow(10.0, -k * s / 400.0));
}

void print0(const char *name, int *index, TVector terms)
{
    printf("const Score %s = S(%d, %d);\n", name, (int)(terms[*index][MG]), (int)(terms[(*index)][EG]));
    (*index)++;
}

void print1(const char *name, int *index, TVector terms, int L)
{
    printf("const Score %s[%d] = {", name, L);

    if (L > 4)
    {
        for (int i = 0; i < L; i++, (*index)++)
        {
            if (i % 4 == 0)
                printf("\n    ");
            printf("S(%d, %d)",(int)(terms[*index][MG]), (int)(terms[(*index)][EG]));
            if (i < L - 1)
                printf(", ");
        }
        printf("\n};\n\n");
    }
    else
    {
        for (int i = 0; i < L; i++, (*index)++)
        {
            printf(" S(%d, %d)", (int)(terms[*index][MG]), (int)(terms[(*index)][EG]));
            if (i < L - 1)
                printf(", ");
            else
                printf(" };\n\n");
        }
    }
}

void print2(const char *name, int *index, TVector terms, int L, int W)
{
    printf("const Score %s[%d][%d] = {\n", name, L, W);
    for (int i = 0; i < L; i++)
    {
        printf("    {");
        for (int j = 0; j < W; j++, (*index)++)
        {
            if (j && j % 8 == 0) printf("\n    ");
            printf("S(%d, %d)", (int)(terms[*index][MG]), (int)(terms[(*index)][EG]));
            if (j < W - 1)
                printf(", ");
        }
        printf("},\n");
    }
    printf("};\n\n");
}

void print3(const char *name, int *index, TVector terms, int L, int W, int H)
{
    printf("const Score %s[%d][%d][%d] = {\n", name, L, W, H);
    for (int i = 0; i < L; i++)
    {
        printf("{");
        for (int j = 0; j < W; j++)
        {
            printf("\n    {");
            for (int k = 0; k < H; k++, (*index)++)
            {
                if (k && k % 8 == 0)
                    printf("\n    ");
                printf("S(%d, %d)", (int)(terms[*index][MG]), (int)(terms[(*index)][EG]));
                if (k < H - 1)
                    printf(", ");
            }
            printf("}");
            if (j < W - 1)
                printf(", ");
        }
        printf("\n},\n");
    }
    printf("};\n\n");
}

void print_imbalance_weights(int *index, TVector terms)
{
    printf("const int my_pieces[5][5] = {");
    for (int i = 0; i < 5; i++)
    {
        printf("\n    {");
        for (int j = 0; j < 5; j++)
        {
            if (j <= i)
                printf("%4d", (int)terms[(*index)++][MG]);
            else
                printf("     ");

            if (j < i)
                printf(",");
        }
        printf("}");
        if (i < 4)
            printf(", ");
    }
    printf("\n};\n\n");

    printf("const int opponent_pieces[5][5] = {");
    for (int i = 0; i < 5; i++)
    {
        printf("\n    {");
        for (int j = 0; j < 5; j++)
        {
            if (j <= i)
                printf("%4d", (j < i) ? (int)terms[(*index)++][MG] : 0);
            else
                printf("     ");

            if (j < i)
                printf(",");
        }
        printf("}");
        if (i < 4)
            printf(", ");
    }
    printf("\n};\n\n");
    printf("const int bishop_pair = %d;\n\n", (int)terms[(*index)++][MG]);
}

void init_imbalance_weights(int *index)
{
    params[*index][0] =  my_pieces[0][0];
    params[(*index)++][1] = my_pieces[0][0];


    params[*index][0] =  my_pieces[1][0];
    params[(*index)++][1] = my_pieces[1][0];
    params[*index][0] =  my_pieces[1][1];
    params[(*index)++][1] = my_pieces[1][1];


    params[*index][0] =  my_pieces[2][0];
    params[(*index)++][1] = my_pieces[2][0];
    params[*index][0] =  my_pieces[2][1];
    params[(*index)++][1] = my_pieces[2][1];
    params[*index][0] =  my_pieces[2][2];
    params[(*index)++][1] = my_pieces[2][2];


    params[*index][0] =  my_pieces[3][0];
    params[(*index)++][1] = my_pieces[3][0];
    params[*index][0] =  my_pieces[3][1];
    params[(*index)++][1] = my_pieces[3][1];
    params[*index][0] =  my_pieces[3][2];
    params[(*index)++][1] = my_pieces[3][2];
    params[*index][0] =  my_pieces[3][3];
    params[(*index)++][1] = my_pieces[3][3];


    params[*index][0] =  my_pieces[4][0];
    params[(*index)++][1] = my_pieces[4][0];
    params[*index][0] =  my_pieces[4][1];
    params[(*index)++][1] = my_pieces[4][1];
    params[*index][0] =  my_pieces[4][2];
    params[(*index)++][1] = my_pieces[4][2];
    params[*index][0] =  my_pieces[4][3];
    params[(*index)++][1] = my_pieces[4][3];
    params[*index][0] =  my_pieces[4][4];
    params[(*index)++][1] = my_pieces[4][4];


    params[*index][0] =  opponent_pieces[1][0];
    params[(*index)++][1] = opponent_pieces[1][0];


    params[*index][0] =  opponent_pieces[2][0];
    params[(*index)++][1] = opponent_pieces[2][0];
    params[*index][0] =  opponent_pieces[2][1];
    params[(*index)++][1] = opponent_pieces[2][1];


    params[*index][0] =  opponent_pieces[3][0];
    params[(*index)++][1] = opponent_pieces[3][0];
    params[*index][0] =  opponent_pieces[3][1];
    params[(*index)++][1] = opponent_pieces[3][1];
    params[*index][0] =  opponent_pieces[3][2];
    params[(*index)++][1] = opponent_pieces[3][2];


    params[*index][0] =  opponent_pieces[4][0];
    params[(*index)++][1] = opponent_pieces[4][0];
    params[*index][0] =  opponent_pieces[4][1];
    params[(*index)++][1] = opponent_pieces[4][1];
    params[*index][0] =  opponent_pieces[4][2];
    params[(*index)++][1] = opponent_pieces[4][2];
    params[*index][0] =  opponent_pieces[4][3];
    params[(*index)++][1] = opponent_pieces[4][3];


    params[*index][0] =  bishop_pair;
    params[(*index)++][1] = bishop_pair;
}

void init_imbalance_coeffs(int *index)
{
    coeffs[*index][0] =  Trace.my_pieces[0][0][0];
    coeffs[(*index)++][1] = Trace.my_pieces[1][0][0];


    coeffs[*index][0] =  Trace.my_pieces[0][1][0];
    coeffs[(*index)++][1] = Trace.my_pieces[1][1][0];
    coeffs[*index][0] =  Trace.my_pieces[0][1][1];
    coeffs[(*index)++][1] = Trace.my_pieces[1][1][1];


    coeffs[*index][0] =  Trace.my_pieces[0][2][0];
    coeffs[(*index)++][1] = Trace.my_pieces[1][2][0];
    coeffs[*index][0] =  Trace.my_pieces[0][2][1];
    coeffs[(*index)++][1] = Trace.my_pieces[1][2][1];
    coeffs[*index][0] =  Trace.my_pieces[0][2][2];
    coeffs[(*index)++][1] = Trace.my_pieces[1][2][2];


    coeffs[*index][0] =  Trace.my_pieces[0][3][0];
    coeffs[(*index)++][1] = Trace.my_pieces[1][3][0];
    coeffs[*index][0] =  Trace.my_pieces[0][3][1];
    coeffs[(*index)++][1] = Trace.my_pieces[1][3][1];
    coeffs[*index][0] =  Trace.my_pieces[0][3][2];
    coeffs[(*index)++][1] = Trace.my_pieces[1][3][2];
    coeffs[*index][0] =  Trace.my_pieces[0][3][3];
    coeffs[(*index)++][1] = Trace.my_pieces[1][3][3];


    coeffs[*index][0] =  Trace.my_pieces[0][4][0];
    coeffs[(*index)++][1] = Trace.my_pieces[1][4][0];
    coeffs[*index][0] =  Trace.my_pieces[0][4][1];
    coeffs[(*index)++][1] = Trace.my_pieces[1][4][1];
    coeffs[*index][0] =  Trace.my_pieces[0][4][2];
    coeffs[(*index)++][1] = Trace.my_pieces[1][4][2];
    coeffs[*index][0] =  Trace.my_pieces[0][4][3];
    coeffs[(*index)++][1] = Trace.my_pieces[1][4][3];
    coeffs[*index][0] =  Trace.my_pieces[0][4][4];
    coeffs[(*index)++][1] = Trace.my_pieces[1][4][4];




    coeffs[*index][0] =  Trace.opponent_pieces[0][1][0];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][1][0];


    coeffs[*index][0] =  Trace.opponent_pieces[0][2][0];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][2][0];
    coeffs[*index][0] =  Trace.opponent_pieces[0][2][1];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][2][1];


    coeffs[*index][0] =  Trace.opponent_pieces[0][3][0];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][3][0];
    coeffs[*index][0] =  Trace.opponent_pieces[0][3][1];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][3][1];
    coeffs[*index][0] =  Trace.opponent_pieces[0][3][2];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][3][2];


    coeffs[*index][0] =  Trace.opponent_pieces[0][4][0];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][4][0];
    coeffs[*index][0] =  Trace.opponent_pieces[0][4][1];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][4][1];
    coeffs[*index][0] =  Trace.opponent_pieces[0][4][2];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][4][2];
    coeffs[*index][0] =  Trace.opponent_pieces[0][4][3];
    coeffs[(*index)++][1] = Trace.opponent_pieces[1][4][3];


    coeffs[*index][0] =  Trace.bishop_pair[0];
    coeffs[(*index)++][1] = Trace.bishop_pair[1];
}


void print_kingTerms(int *index, TVector terms)
{
    print0("kingflankAttack", index, terms);
    print0("pawnDistancePenalty", index, terms);
}

void init_kingTerms(int *index)
{
    params[*index][0] =  mg_value(kingflankAttack);
    params[(*index)++][1] = eg_value(kingflankAttack);
    params[*index][0] =  mg_value(pawnDistancePenalty);
    params[(*index)++][1] = eg_value(pawnDistancePenalty);
}

void init_kingCoeffs(int *index)
{
    coeffs[*index][0] =  Trace.kingflankAttack[0];
    coeffs[(*index)++][1] = Trace.kingflankAttack[1];
    coeffs[*index][0] =  Trace.pawnDistancePenalty[0];
    coeffs[(*index)++][1] = Trace.pawnDistancePenalty[1];
}


void print_threatTerms(int *index, TVector terms)
{
    print1("minorThreat", index, terms, 7);
    print1("rookThreat", index, terms, 7);
    print0("kingThreat", index, terms);
    print0("kingMultipleThreat", index, terms);
    print0("pawnPushThreat", index, terms);
    print0("safePawnThreat", index, terms);
    print0("hangingPiece", index, terms);
}

void init_threatTerms(int *index)
{

    for (int i = 0; i < 7; i++)
    {
        params[*index][0] =  mg_value(minorThreat[i]);
        params[(*index)++][1] = eg_value(minorThreat[i]);
    }


    for (int i = 0; i < 7; i++)
    {
        params[*index][0] =  mg_value(rookThreat[i]);
        params[(*index)++][1] = eg_value(rookThreat[i]);
    }


    params[*index][0] =  mg_value(kingThreat);
    params[(*index)++][1] = eg_value(kingThreat);


    params[*index][0] =  mg_value(kingMultipleThreat);
    params[(*index)++][1] = eg_value(kingMultipleThreat);


    params[*index][0] =  mg_value(pawnPushThreat);
    params[(*index)++][1] = eg_value(pawnPushThreat);


    params[*index][0] =  mg_value(safePawnThreat);
    params[(*index)++][1] = eg_value(safePawnThreat);


    params[*index][0] =  mg_value(hangingPiece);
    params[(*index)++][1] = eg_value(hangingPiece);
}

void init_threatCoeffs(int *index)
{
    for (int i = 0; i < 7; i++)
    {
        coeffs[*index][0] =  (Trace.minorThreat[0][i]);
        coeffs[(*index)++][1] = (Trace.minorThreat[1][i]);
    }


    for (int i = 0; i < 7; i++)
    {
        coeffs[*index][0] =  (Trace.rookThreat[0][i]);
        coeffs[(*index)++][1] = (Trace.rookThreat[1][i]);
    }


    coeffs[*index][0] =  (Trace.kingThreat[0]);
    coeffs[(*index)++][1] = (Trace.kingThreat[1]);


    coeffs[*index][0] =  (Trace.kingMultipleThreat[0]);
    coeffs[(*index)++][1] = (Trace.kingMultipleThreat[1]);


    coeffs[*index][0] =  (Trace.pawnPushThreat[0]);
    coeffs[(*index)++][1] = (Trace.pawnPushThreat[1]);


    coeffs[*index][0] =  (Trace.safePawnThreat[0]);
    coeffs[(*index)++][1] = (Trace.safePawnThreat[1]);


    coeffs[*index][0] =  (Trace.hangingPiece[0]);
    coeffs[(*index)++][1] = (Trace.hangingPiece[1]);
}


void print_pieceWeights(int *index, TVector terms)
{
    printf("const int PAWN_MG = %d;\n", (int)terms[*index][MG]);
    printf("const int PAWN_EG = %d;\n", (int)terms[(*index)++][EG]);
    printf("const int KNIGHT_MG = %d;\n", (int)terms[*index][MG]);
    printf("const int KNIGHT_EG = %d;\n", (int)terms[(*index)++][EG]);
    printf("const int BISHOP_MG = %d;\n", (int)terms[*index][MG]);
    printf("const int BISHOP_EG = %d;\n", (int)terms[(*index)++][EG]);
    printf("const int ROOK_MG = %d;\n", (int)terms[*index][MG]);
    printf("const int ROOK_EG = %d;\n", (int)terms[(*index)++][EG]);
    printf("const int QUEEN_MG = %d;\n", (int)terms[*index][MG]);
    printf("const int QUEEN_EG = %d;\n", (int)terms[(*index)++][EG]);
    printf("const int KING_MG = %d;\n", 0);
    printf("const int KING_EG = %d;\n\n", 0);

    printf("const Score piece_bonus[7][64] = {");
    for (int i = 0; i < 7; i++)
    {
        printf("\n{");

        if (i == 0)
        {
            for (int j = 0; j < 64; j++)
            {
                if (j % 8 == 0)
                    printf("\n    ");
                printf("S(0,0)");
                if (j < 63)
                    printf(", ");
            }
        }
        else
        {
            for (int j = 0; j < 64; j++, (*index)++)
            {
                if (j % 8 == 0)
                    printf("\n    ");
                printf("S(%d,%d)", (int)terms[*index][MG], (int)terms[*index][EG]);
                if (j < 63)
                    printf(", ");
            }
        }

        printf("\n}");
        if (i < 6)
            printf(",");
    }
    printf("\n};\n\n");
}

void init_pieceWeights(int *index)
{
    params[*index][0] =  PAWN_MG;
    params[(*index)++][1] = PAWN_EG;
    params[*index][0] =  KNIGHT_MG;
    params[(*index)++][1] = KNIGHT_EG;
    params[*index][0] =  BISHOP_MG;
    params[(*index)++][1] = BISHOP_EG;
    params[*index][0] =  ROOK_MG;
    params[(*index)++][1] = ROOK_EG;
    params[*index][0] =  QUEEN_MG;
    params[(*index)++][1] = QUEEN_EG;

    for (int j = 0; j < 64; j++)
    {
        params[*index][0] =  mg_value(piece_bonus[PAWN][j]);
        params[(*index)++][1] = eg_value(piece_bonus[PAWN][j]);
    }

    for (int j = 0; j < 64; j++)
    {
        params[*index][0] =  mg_value(piece_bonus[KNIGHT][j]);
        params[(*index)++][1] = eg_value(piece_bonus[KNIGHT][j]);
    }


    for (int j = 0; j < 64; j++)
    {
        params[*index][0] =  mg_value(piece_bonus[BISHOP][j]);
        params[(*index)++][1] = eg_value(piece_bonus[BISHOP][j]);
    }

    for (int j = 0; j < 64; j++)
    {
        params[*index][0] =  mg_value(piece_bonus[ROOK][j]);
        params[(*index)++][1] = eg_value(piece_bonus[ROOK][j]);
    }

    for (int j = 0; j < 64; j++)
    {
        params[*index][0] =  mg_value(piece_bonus[QUEEN][j]);
        params[(*index)++][1] = eg_value(piece_bonus[QUEEN][j]);
    }

    for (int j = 0; j < 64; j++)
    {
        params[*index][0] =  mg_value(piece_bonus[KING][j]);
        params[(*index)++][1] = eg_value(piece_bonus[KING][j]);
    }
}

void init_pieceCoeffs(int *index)
{
    coeffs[*index][0] =  Trace.piece_values[0][PAWN];
    coeffs[(*index)++][1] = Trace.piece_values[1][PAWN];
    coeffs[*index][0] =  Trace.piece_values[0][KNIGHT];
    coeffs[(*index)++][1] = Trace.piece_values[1][KNIGHT];
    coeffs[*index][0] =  Trace.piece_values[0][BISHOP];
    coeffs[(*index)++][1] = Trace.piece_values[1][BISHOP];
    coeffs[*index][0] =  Trace.piece_values[0][ROOK];
    coeffs[(*index)++][1] = Trace.piece_values[1][ROOK];
    coeffs[*index][0] =  Trace.piece_values[0][QUEEN];
    coeffs[(*index)++][1] = Trace.piece_values[1][QUEEN];

    for (int j = 0; j < 64; j++)
    {
        coeffs[*index][0] =  (Trace.piece_bonus[0][PAWN][j]);
        coeffs[(*index)++][1] = (Trace.piece_bonus[1][PAWN][j]);
    }

    for (int j = 0; j < 64; j++)
    {
        coeffs[*index][0] =  (Trace.piece_bonus[0][KNIGHT][j]);
        coeffs[(*index)++][1] = (Trace.piece_bonus[1][KNIGHT][j]);
    }

    for (int j = 0; j < 64; j++)
    {
        coeffs[*index][0] =  (Trace.piece_bonus[0][BISHOP][j]);
        coeffs[(*index)++][1] = (Trace.piece_bonus[1][BISHOP][j]);
    }

    for (int j = 0; j < 64; j++)
    {
        coeffs[*index][0] =  (Trace.piece_bonus[0][ROOK][j]);
        coeffs[(*index)++][1] = (Trace.piece_bonus[1][ROOK][j]);
    }

    for (int j = 0; j < 64; j++)
    {
        coeffs[*index][0] =  (Trace.piece_bonus[0][QUEEN][j]);
        coeffs[(*index)++][1] = (Trace.piece_bonus[1][QUEEN][j]);
    }

    for (int j = 0; j < 64; j++)
    {
        coeffs[*index][0] =  (Trace.piece_bonus[0][KING][j]);
        coeffs[(*index)++][1] = (Trace.piece_bonus[1][KING][j]);
    }
}


void print_passedTerms(int *index, TVector terms)
{
    print1("passedRankBonus", index, terms, 8);
    print2("passedUnsafeBonus", index, terms, 2, 8);
    print2("passedBlockedBonus", index, terms, 2, 8);
    print1("passedFriendlyDistance", index, terms, 8);
    print1("passedEnemyDistance", index, terms, 8);
    print0("tarraschRule_enemy", index, terms);
    print1("tarraschRule_friendly", index, terms, 8);
}

void init_passedTerms(int *index)
{
    for (int i = 0; i < 8; i++)
    {
        params[*index][0] =  mg_value(passedRankBonus[i]);
        params[(*index)++][1] = eg_value(passedRankBonus[i]);
    }



    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 8; j++)
        {
            params[*index][0] =  mg_value(passedUnsafeBonus[i][j]);
            params[(*index)++][1] = eg_value(passedUnsafeBonus[i][j]);
        }



    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 8; j++)
        {
            params[*index][0] =  mg_value(passedBlockedBonus[i][j]);
            params[(*index)++][1] = eg_value(passedBlockedBonus[i][j]);
        }



    for (int i = 0; i < 8; i++)
    {
        params[*index][0] =  mg_value(passedFriendlyDistance[i]);
        params[(*index)++][1] = eg_value(passedFriendlyDistance[i]);
    }



    for (int i = 0; i < 8; i++)
    {
        params[*index][0] =  mg_value(passedEnemyDistance[i]);
        params[(*index)++][1] = eg_value(passedEnemyDistance[i]);
    }



    params[*index][0] =  mg_value(tarraschRule_enemy);
    params[(*index)++][1] = eg_value(tarraschRule_enemy);


    for (int i = 0; i < 8; i++)
    {
        params[*index][0] =  mg_value(tarraschRule_friendly[i]);
        params[(*index)++][1] = eg_value(tarraschRule_friendly[i]);
    }
}

void init_passedCoeffs(int *index)
{
    for (int i = 0; i < 8; i++)
    {
        coeffs[*index][0] =  (Trace.passedRankBonus[0][i]);
        coeffs[(*index)++][1] = (Trace.passedRankBonus[1][i]);
    }



    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 8; j++)
        {
            coeffs[*index][0] =  (Trace.passedUnsafeBonus[0][i][j]);
            coeffs[(*index)++][1] = (Trace.passedUnsafeBonus[1][i][j]);
        }



    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 8; j++)
        {
            coeffs[*index][0] =  (Trace.passedBlockedBonus[0][i][j]);
            coeffs[(*index)++][1] = (Trace.passedBlockedBonus[1][i][j]);
        }



    for (int i = 0; i < 8; i++)
    {
        coeffs[*index][0] =  (Trace.passedFriendlyDistance[0][i]);
        coeffs[(*index)++][1] = (Trace.passedFriendlyDistance[1][i]);
    }



    for (int i = 0; i < 8; i++)
    {
        coeffs[*index][0] =  (Trace.passedEnemyDistance[0][i]);
        coeffs[(*index)++][1] = (Trace.passedEnemyDistance[1][i]);
    }



    coeffs[*index][0] =  (Trace.tarraschRule_enemy[0]);
    coeffs[(*index)++][1] = (Trace.tarraschRule_enemy[1]);


    for (int i = 0; i < 8; i++)
    {
        coeffs[*index][0] =  (Trace.tarraschRule_friendly[0][i]);
        coeffs[(*index)++][1] = (Trace.tarraschRule_friendly[1][i]);
    }

}


void print_pawnTerms(int *index, TVector terms)
{
    print1("isolated_penalty", index, terms, 2);
    print1("isolated_penaltyAH", index, terms, 2);
    print1("isolated_doubled_penalty", index, terms, 2);
    print1("isolated_doubled_penaltyAH", index, terms, 2);
    print1("doubled_penalty", index, terms, 2);
    print1("doubled_penalty_undefended", index, terms, 2);
    print1("backward_penalty", index, terms, 2);
    print3("connected_bonus", index, terms, 2, 2, 8);
}

void init_pawnTerms(int *index)
{
    for (int i = 0; i < 2; i++)
    {
        params[*index][0] =  mg_value(isolated_penalty[i]);
        params[(*index)++][1] = eg_value(isolated_penalty[i]);
    }

    for (int i = 0; i < 2; i++)
    {
        params[*index][0] =  mg_value(isolated_penaltyAH[i]);
        params[(*index)++][1] = eg_value(isolated_penaltyAH[i]);
    }

    for (int i = 0; i < 2; i++)
    {
        params[*index][0] =  mg_value(isolated_doubled_penalty[i]);
        params[(*index)++][1] = eg_value(isolated_doubled_penalty[i]);
    }

    for (int i = 0; i < 2; i++)
    {
        params[*index][0] =  mg_value(isolated_doubled_penaltyAH[i]);
        params[(*index)++][1] = eg_value(isolated_doubled_penaltyAH[i]);
    }

    for (int i = 0; i < 2; i++)
    {
        params[*index][0] =  mg_value(doubled_penalty[i]);
        params[(*index)++][1] = eg_value(doubled_penalty[i]);
    }

    for (int i = 0; i < 2; i++)
    {
        params[*index][0] =  mg_value(doubled_penalty_undefended[i]);
        params[(*index)++][1] = eg_value(doubled_penalty_undefended[i]);
    }

    for (int i = 0; i < 2; i++)
    {
        params[*index][0] =  mg_value(backward_penalty[i]);
        params[(*index)++][1] = eg_value(backward_penalty[i]);
    }

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 8; k++)
            {
                params[*index][0] =  mg_value(connected_bonus[i][j][k]);
                params[(*index)++][1] = eg_value(connected_bonus[i][j][k]);
            }
    }

}

void init_pawnCoeffs(int *index)
{
    for (int i = 0; i < 2; i++)
    {
        coeffs[*index][0] =  (Trace.isolated_penalty[0][i]);
        coeffs[(*index)++][1] = (Trace.isolated_penalty[1][i]);
    }

    for (int i = 0; i < 2; i++)
    {
        coeffs[*index][0] =  (Trace.isolated_penaltyAH[0][i]);
        coeffs[(*index)++][1] = (Trace.isolated_penaltyAH[1][i]);
    }

    for (int i = 0; i < 2; i++)
    {
        coeffs[*index][0] =  (Trace.isolated_doubled_penalty[0][i]);
        coeffs[(*index)++][1] = (Trace.isolated_doubled_penalty[1][i]);
    }

    for (int i = 0; i < 2; i++)
    {
        coeffs[*index][0] =  (Trace.isolated_doubled_penaltyAH[0][i]);
        coeffs[(*index)++][1] = (Trace.isolated_doubled_penaltyAH[1][i]);
    }

    for (int i = 0; i < 2; i++)
    {
        coeffs[*index][0] =  (Trace.doubled_penalty[0][i]);
        coeffs[(*index)++][1] = (Trace.doubled_penalty[1][i]);
    }

    for (int i = 0; i < 2; i++)
    {
        coeffs[*index][0] =  (Trace.doubled_penalty_undefended[0][i]);
        coeffs[(*index)++][1] = (Trace.doubled_penalty_undefended[1][i]);
    }

    for (int i = 0; i < 2; i++)
    {
        coeffs[*index][0] =  (Trace.backward_penalty[0][i]);
        coeffs[(*index)++][1] = (Trace.backward_penalty[1][i]);
    }

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 8; k++)
            {
                coeffs[*index][0] =  (Trace.connected_bonus[0][i][j][k]);
                coeffs[(*index)++][1] = (Trace.connected_bonus[1][i][j][k]);
            }
    }
}


void print_mobilityTerms(int *index, TVector terms)
{
    print1("knightMobilityBonus", index, terms, 9);
    print1("bishopMobilityBonus", index, terms, 14);
    print1("rookMobilityBonus", index, terms, 15);
    print1("queenMobilityBonus", index, terms, 28);
}

void init_mobilityTerms(int *index)
{
    for (int i = 0; i < 9; i++)
    {
        params[*index][0] =  mg_value(knightMobilityBonus[i]);
        params[(*index)++][1] = eg_value(knightMobilityBonus[i]);
    }

    for (int i = 0; i < 14; i++)
    {
        params[*index][0] =  mg_value(bishopMobilityBonus[i]);
        params[(*index)++][1] = eg_value(bishopMobilityBonus[i]);
    }

    for (int i = 0; i < 15; i++)
    {
        params[*index][0] =  mg_value(rookMobilityBonus[i]);
        params[(*index)++][1] = eg_value(rookMobilityBonus[i]);
    }

    for (int i = 0; i < 28; i++)
    {
        params[*index][0] =  mg_value(queenMobilityBonus[i]);
        params[(*index)++][1] = eg_value(queenMobilityBonus[i]);
    }

}

void init_mobilityCoeffs(int *index)
{
    for (int i = 0; i < 9; i++)
    {
        coeffs[*index][0] =  (Trace.knightMobilityBonus[0][i]);
        coeffs[(*index)++][1] = (Trace.knightMobilityBonus[1][i]);
    }



    for (int i = 0; i < 14; i++)
    {
        coeffs[*index][0] =  (Trace.bishopMobilityBonus[0][i]);
        coeffs[(*index)++][1] = (Trace.bishopMobilityBonus[1][i]);
    }



    for (int i = 0; i < 15; i++)
    {
        coeffs[*index][0] =  (Trace.rookMobilityBonus[0][i]);
        coeffs[(*index)++][1] = (Trace.rookMobilityBonus[1][i]);
    }



    for (int i = 0; i < 28; i++)
    {
        coeffs[*index][0] =  (Trace.queenMobilityBonus[0][i]);
        coeffs[(*index)++][1] = (Trace.queenMobilityBonus[1][i]);
    }

}


void print_minorTerms(int *index, TVector terms)
{
    print0("bishopPawns", index, terms);
    print0("kingProtector", index, terms);
    print2("outpostBonus", index, terms, 2, 2);
    print1("reachableOutpost", index, terms, 2);
    print0("bishopOpposerBonus", index, terms);
    print0("trappedBishopPenalty", index, terms);
    print0("veryTrappedBishopPenalty", index, terms);
}

void init_minorTerms(int *index)
{
    params[*index][0] =  mg_value(bishopPawns);
    params[(*index)++][1] = eg_value(bishopPawns);


    params[*index][0] =  mg_value(kingProtector);
    params[(*index)++][1] = eg_value(kingProtector);


    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
        {
            params[*index][0] =  mg_value(outpostBonus[i][j]);
            params[(*index)++][1] = eg_value(outpostBonus[i][j]);
        }


    for (int i = 0; i < 2; i++)
    {
    params[*index][0] =  mg_value(reachableOutpost[i]);
    params[(*index)++][1] = eg_value(reachableOutpost[i]);
    }



    params[*index][0] =  mg_value(bishopOpposerBonus);
    params[(*index)++][1] = eg_value(bishopOpposerBonus);


    params[*index][0] =  mg_value(trappedBishopPenalty);
    params[(*index)++][1] = eg_value(trappedBishopPenalty);


    params[*index][0] =  mg_value(veryTrappedBishopPenalty);
    params[(*index)++][1] = eg_value(veryTrappedBishopPenalty);
}

void init_minorCoeffs(int *index)
{

    coeffs[*index][0] =  (Trace.bishopPawns[0]);
    coeffs[(*index)++][1] = (Trace.bishopPawns[1]);


    coeffs[*index][0] =  (Trace.kingProtector[0]);
    coeffs[(*index)++][1] = (Trace.kingProtector[1]);


    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
        {
            coeffs[*index][0] =  (Trace.outpostBonus[0][i][j]);
            coeffs[(*index)++][1] = (Trace.outpostBonus[1][i][j]);
        }

    for (int i = 0; i < 2; i++)
    {
        coeffs[*index][0] =  (Trace.reachableOutpost[0][i]);
        coeffs[(*index)++][1] = (Trace.reachableOutpost[1][i]);
    }


    coeffs[*index][0] =  (Trace.bishopOpposerBonus[0]);
    coeffs[(*index)++][1] = (Trace.bishopOpposerBonus[1]);


    coeffs[*index][0] =  (Trace.trappedBishopPenalty[0]);
    coeffs[(*index)++][1] = (Trace.trappedBishopPenalty[1]);


    coeffs[*index][0] =  (Trace.veryTrappedBishopPenalty[0]);
    coeffs[(*index)++][1] = (Trace.veryTrappedBishopPenalty[1]);
}


void print_rookTerms(int *index, TVector terms)
{
    print0("defendedRookFile", index, terms);
    print0("rank7Rook", index, terms);
    print1("rookFile", index, terms, 2);
    print0("battery", index, terms);
}

void init_rookTerms(int *index)
{
    params[*index][0] =  mg_value(defendedRookFile);
    params[(*index)++][1] = eg_value(defendedRookFile);

    params[*index][0] =  mg_value(rank7Rook);
    params[(*index)++][1] = eg_value(rank7Rook);

    for (int i = 0; i < 2; i++)
    {
        params[*index][0] =  mg_value(rookFile[i]);
        params[(*index)++][1] = eg_value(rookFile[i]);
    }

    params[*index][0] =  mg_value(battery);
    params[(*index)++][1] = eg_value(battery);
}

void init_rookCoeffs(int *index)
{
    coeffs[*index][0] =  (Trace.defendedRookFile[0]);
    coeffs[(*index)++][1] = (Trace.defendedRookFile[1]);


    coeffs[*index][0] =  (Trace.rank7Rook[0]);
    coeffs[(*index)++][1] = (Trace.rank7Rook[1]);

    for (int i = 0; i < 2; i++)
    {
        coeffs[*index][0] =  (Trace.rookFile[0][i]);
        coeffs[(*index)++][1] = (Trace.rookFile[1][i]);
    }

    coeffs[*index][0] =  (Trace.battery[0]);
    coeffs[(*index)++][1] = (Trace.battery[1]);
}


void print_params(TVector terms)
{
    int index = 0;
    print_mobilityTerms(&index, terms);
    print_pieceWeights(&index, terms);
    print_imbalance_weights(&index, terms);
    print_passedTerms(&index, terms);
    print_kingTerms(&index, terms);
    print_threatTerms(&index, terms);
    print_pawnTerms(&index, terms);
    print_minorTerms(&index, terms);
    print_rookTerms(&index, terms);
}

void init_params()
{
    int index = 0;
    init_mobilityTerms(&index);
    init_pieceWeights(&index);
    init_imbalance_weights(&index);
    init_passedTerms(&index);
    init_kingTerms(&index);
    init_threatTerms(&index);
    init_pawnTerms(&index);
    init_minorTerms(&index);
    init_rookTerms(&index);
}

void init_coeffs()
{
    memset(&coeffs, 0, sizeof(coeffs));
    int index = 0;
    if (tuneMobility) init_mobilityCoeffs(&index); else index += 66;
    if (tuneWeights) init_pieceCoeffs(&index); else index += 389;
    if (tuneImbalance) init_imbalance_coeffs(&index); else index += 26;
    if (tunePassed) init_passedCoeffs(&index); else index += 65;
    if (tuneKing) init_kingCoeffs(&index); else index += 2;
    if (tuneThreat) init_threatCoeffs(&index); else index += 19;
    if (tunePawns) init_pawnCoeffs(&index); else index += 46;
    if (tuneMinors) init_minorCoeffs(&index); else index += 11;
    if (tuneRooks) init_rookCoeffs(&index);
}

void initTuples(RMStunerEntry *entry)
{
    init_coeffs();
    int length = 0;
    for (unsigned i = 0; i < NTERMS; i++)
    {
        if (coeffs[i][0] - coeffs[i][1])
            length++;
    }
    entry->ntuples = length;
    entry->tuples = (tunerTuple * )calloc(length, sizeof(tunerTuple));

    for (uint16_t i = 0, index = 0; i < NTERMS; i++)
    {
        if (coeffs[i][0] - coeffs[i][1])
            entry->tuples[index++] = { i, coeffs[i][0], coeffs[i][1] };
    }
}

void addRMSentry(RMStunerEntry *entry, Position *pos, double actual)
{
    int phase = (24 - (pos->pieceCount[WBISHOP] + pos->pieceCount[BBISHOP] + pos->pieceCount[WKNIGHT] + pos->pieceCount[BKNIGHT])
        - 2 * (pos->pieceCount[WROOK] + pos->pieceCount[BROOK])
        - 4 * (pos->pieceCount[WQUEEN] + pos->pieceCount[BQUEEN]));

    entry->phase = (phase * 255 + 12) / 24;
    entry->pfactors[MG] = 1 - phase / 24.0;
    entry->pfactors[EG] = phase / 24.0;

    entry->staticEval = pos->activeSide ? -evaluate(*pos) : evaluate(*pos);
    entry->actualscore = actual;
    entry->scale = Trace.scale;
    entry->originalScore = Trace.originalScore;
    entry->activeSide = pos->activeSide == BLACK;
    initTuples(entry);
}

void read_file_data() {
    ifstream fens;
    fens.open("../FENS.book");
    string line;
    Position *pos;
    init_params();
    RMStuningData = (RMStunerEntry*)calloc(NFENS, sizeof(RMStunerEntry));

    for (int i = 0; i < NFENS; i++) {
        getline(fens, line);
        double result;

        if (strstr(line.c_str(), "[1.0]")) {
            result = 1.0;
        }
        else if (strstr(line.c_str(), "[0.0]")) {
            result = 0.0;
        }
        else if (strstr(line.c_str(), "[0.5]")) {
            result = 0.5;
        }
        else {
            cout << line << endl;
            result = -1.0;
            exit(1);
        }

        memset(&Trace, 0, sizeof(tunerTrace));
        pos = import_fen(line.c_str(), 0);

        addRMSentry(&RMStuningData[i], pos, result);

        if (i % 1000000 == 0) {
            cout << "Reading line " << i << endl;
        }
    }
    cout << "Read " << NFENS << " lines"<<endl;
    fens.close();
}

double initialError(double guess) {

    double total = 0.0;

    #pragma omp parallel shared(total)
    {
        #pragma omp for schedule(static, NFENS / NTHREADS) reduction(+:total)
        for (int i = 0; i < NFENS; i++)
            total += pow(RMStuningData[i].actualscore - sigmoid(RMStuningData[i].staticEval, guess), 2);
    }

    return total / (double) NFENS;
}

double find_optimal_k()
{
    int granularity = 6;
    double delta = 0.1;
    double guess = 0.6;
    double start = 0.0;
    double stop = 1.5;
    double best = 1.0;

    for (int i = 0; i < granularity; i++)
    {
        guess = start;
        while (guess < stop)
        {
            double newerror = initialError(guess);
            if (newerror <= best)
            {
                best = newerror;
                start = guess;
            }
            guess += delta;
        }

        printf("COMPUTING K ITERATION [%d] K = %f E = %f\n", i, start, best);
        stop = start + delta;
        start = start - delta;
        delta /= 10.0;
    }
    return start;
}

double fastEval(RMStunerEntry *entry)
{
    double mg = 0, eg = 0;
    // Get deltas
    for (int i = 0; i < entry->ntuples; i++)
    {
        mg += (double)entry->tuples[i].wcoeff * deltas[entry->tuples[i].index][MG];
        mg -= (double)entry->tuples[i].bcoeff * deltas[entry->tuples[i].index][MG];
        eg += (double)entry->tuples[i].wcoeff * deltas[entry->tuples[i].index][EG];
        eg -= (double)entry->tuples[i].bcoeff * deltas[entry->tuples[i].index][EG];
    }

    // add deltas to original eval
    mg += (double)mg_value(entry->originalScore);
    eg += (double)eg_value(entry->originalScore);

    double out = (mg*(256.0-entry->phase) + (eg*entry->phase*entry->scale)) / 256.0;
    return out + (entry->activeSide ? -tempo : tempo);
}

void calculateGradient(RMStunerEntry *entry, TVector localgradient)
{
    double eval = fastEval(entry);
    double sg = sigmoid(eval, K);
    double sgPrime = (entry->actualscore - sg) * sg * (1 - sg);

    double mgBase = sgPrime * entry->pfactors[MG];
    double egBase = sgPrime * entry->pfactors[EG];

    for (int i = 0; i < entry->ntuples; i++)
    {
        int index = entry->tuples[i].index;
        int wcoeff = entry->tuples[i].wcoeff;
        int bcoeff = entry->tuples[i].bcoeff;

        localgradient[index][MG] += mgBase * (wcoeff - bcoeff);
        localgradient[index][EG] += egBase * (wcoeff - bcoeff) * entry->scale;
    }
}

void calculateTotalGradient(TVector gradient, int batch)
{
    #pragma omp parallel shared(gradient)
    {
        TVector localgradient = {0};

        #pragma omp for schedule(static, BATCHSIZE / NTHREADS)
        for (int i = batch * BATCHSIZE; i < (batch + 1) * BATCHSIZE; i++)
            calculateGradient(&RMStuningData[i], localgradient);

        for (int i = 0; i < NTERMS; i++) {
            gradient[i][MG] += localgradient[i][MG];
            gradient[i][EG] += localgradient[i][EG];
        }
    }
}

double tunedError() {

    double total = 0.0;

    #pragma omp parallel shared(total)
    {
        #pragma omp for schedule(static, NFENS / NTHREADS) reduction(+:total)
        for (int i = 0; i < NFENS; i++)
            total += pow(RMStuningData[i].actualscore - sigmoid(fastEval(&RMStuningData[i]), K), 2);
    }

    return total / (double) NFENS;
}

void reportParams()
{
    TVector tempParams;

    for (int i = 0; i < NTERMS; i++)
    {
        tempParams[i][MG] = round(params[i][MG] + deltas[i][MG]);
        tempParams[i][EG] = round(params[i][EG] + deltas[i][EG]);
    }

    printf("\n\n============================================================\n\n");
    print_params(tempParams);
    printf("\n\n============================================================\n\n");
}

void tune()
{
    read_file_data();
    //K = find_optimal_k();
    double rate = LR;

    #if adam
    double mhatMG, mhatEG, vhatMG, vhatEG;
    #endif

    #if 1
    for (int epoch = 0; epoch < EPOCHS; epoch++)
    {
        printf("Epoch [%d] Error = [%.13f], Rate = [%g]\n", epoch, tunedError(), rate);

        for (int batch = 0; batch < NFENS / BATCHSIZE; batch++)
        {
            TVector gradient = {0};
            calculateTotalGradient(gradient, batch);

            for (int i = 0; i < NTERMS; i++)
            {
                #if RMSProp
                RMS[i][MG] = RMS_E * RMS[i][MG] + (1 - RMS_E) * pow((K / 200.0) * gradient[i][MG] / BATCHSIZE, 2.0);
                RMS[i][EG] = RMS_E * RMS[i][EG] + (1 - RMS_E) * pow((K / 200.0) * gradient[i][EG] / BATCHSIZE, 2.0);
                deltas[i][MG] += (K / 200.0) * (gradient[i][MG] / BATCHSIZE) * (rate / sqrt(1e-8 + RMS[i][MG]));
                deltas[i][EG] += (K / 200.0) * (gradient[i][EG] / BATCHSIZE) * (rate / sqrt(1e-8 + RMS[i][EG]));
                #elif adam
                adam_m[i][MG] = adam_b1 * adam_m[i][MG] + (1 - adam_b1) * ((K / 200.0) * gradient[i][MG] / BATCHSIZE);
                adam_m[i][EG] = adam_b1 * adam_m[i][EG] + (1 - adam_b1) * ((K / 200.0) * gradient[i][EG] / BATCHSIZE);

                adam_v[i][MG] = adam_b2 * adam_v[i][MG] + (1 - adam_b2) * pow((K / 200.0) * gradient[i][MG] / BATCHSIZE, 2.0);
                adam_v[i][EG] = adam_b2 * adam_v[i][EG] + (1 - adam_b2) * pow((K / 200.0) * gradient[i][EG] / BATCHSIZE, 2.0);

                mhatMG = (adam_m[i][MG]) / (1.0 - pow(adam_b1, epoch+1));
                mhatEG = (adam_m[i][EG]) / (1.0 - pow(adam_b1, epoch+1));
                vhatMG = (adam_v[i][MG]) / (1.0 - pow(adam_b2, epoch+1));
                vhatEG = (adam_v[i][EG]) / (1.0 - pow(adam_b2, epoch+1));

                deltas[i][MG] += (K / 200.0) * (gradient[i][MG] / BATCHSIZE) * (rate * mhatMG / (1e-8 + sqrt(vhatMG)));
                deltas[i][EG] += (K / 200.0) * (gradient[i][EG] / BATCHSIZE) * (rate * mhatEG / (1e-8 + sqrt(vhatEG)));
                #else
                deltas[i][MG] += (K / 200.0) * (gradient[i][MG] / BATCHSIZE) * rate;
                deltas[i][EG] += (K / 200.0) * (gradient[i][EG] / BATCHSIZE) * rate;
                #endif
            }
        }

        if (cutStep && epoch > cutStep && rate > 1.0)
            rate = max(rate * 0.95, 1.0);
        if (epoch && epoch % decaySteps == 0)
            rate *= decayrate;
        if (epoch && epoch % reportSteps == 0)
            reportParams();
    }
    //for (int i = 0; i < NTERMS; i++)
        //cout <<deltas[i][MG]<<" "<<deltas[i][EG]<<endl;

    reportParams();
    #endif
}

#endif

