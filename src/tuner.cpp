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

#define printAll 1
#define tuneMobility 0
#define tuneWeights 0
#define tunePSQT 0
#define tuneImbalance 0
#define tunePassed 1
#define tuneKing 1
#define tuneThreat 0
#define tunePawns 0
#define tuneMinors 1
#define tuneRooks 0

/// RMSProp / adam tuner used for analyzing very large datasets.
/// Thanks to Andrew Grant for doing the math, and providing an example framework with his own tuner.

/// In my implementation, RMSProp has been historically stronger in high-gradient fields, due to higher momentum
/// and adam is stronger where the weights are believed to be close to optimal values.

constexpr int NTERMS = 619; // Number of weights to be tuned (I don't change this; coefficients that aren't exposed for tuning are just zeroed)
constexpr int EPOCHS = 1000; // How many iterations to tune over
constexpr double LR = 1.0; // Learning rate
constexpr double RMS_E = 0.9; // Probably don't modify this one
constexpr double decayrate = 0.93; // The learning rate is scaled down this much each decay step
constexpr int decaySteps = 300; // How often to lower the learning rate
constexpr int reportSteps = 100; // How often to print out weights
constexpr double adam_b1 = 0.9; // Probably don't modify this one
constexpr double adam_b2 = 0.999; // Probably don't modify this one
constexpr int cutStep = 600; // In case we want to start with a very high learning rate and drop it suddenly at a certain point

#define NFENS 2500000//9999740
#define NTHREADS 7
#define BATCHSIZE 16384
#define QSRESOLVE 0

extern tunerTrace Trace;
extern localTrace safetyTrace;

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
    Score dangerScore[2];
    bool forcedDraw;
};

enum functionType {
    NORMALTYPE,
    IMBALANCETYPE
};

RMStunerEntry *RMStuningData;

typedef double TVector[NTERMS][2]; // indexed MG, EG

TVector params; // indexed MG, EG
int8_t coeffs[NTERMS][2]; // indexed WHITE, BLACK
functionType types[NTERMS] = {NORMALTYPE}; // is this term normal or imbalance?
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
    for (int i = 0; i < 25; i++)
        types[*index+i] = IMBALANCETYPE;

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
    //cout << *index << endl;
    print1("rookThreat", index, terms, 7);
    //cout << *index << endl;
    print0("kingThreat", index, terms);
    //cout << *index << endl;
    print0("kingMultipleThreat", index, terms);
    //cout << *index << endl;
    print0("pawnPushThreat", index, terms);
    //cout << *index << endl;
    print0("safePawnThreat", index, terms);
    //cout << *index << endl;
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
}


void print_PSQTs(int *index, TVector terms)
{
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

void init_PSQTs(int *index)
{
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

void init_PSQTCoeffs(int *index)
{
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
    printf("const Score %s[%d] = {\n    S(0,0), S(0,0), ", "passedRankBonus", 8);
    for (int i = 2; i < 8; i++, (*index)++)
    {
        if (i % 4 == 0)
            printf("\n    ");
        printf("S(%d, %d)",(int)(terms[*index][MG]), (int)(terms[(*index)][EG]));
        if (i < 7)
            printf(", ");
    }
    printf("\n};\n\n");
    //cout << *index << endl;
    //print2("passedUnsafeBonus", index, terms, 2, 8);

    printf("const Score %s[%d][%d] = {\n", "passedUnsafeBonus", 2, 8);
    for (int i = 0; i < 2; i++)
    {
        printf("    {S(0,0), S(0,0), ");
        for (int j = 2; j < 8; j++, (*index)++)
        {
            if (j && j % 8 == 0) printf("\n    ");
            printf("S(%d, %d)", (int)(terms[*index][MG]), (int)(terms[(*index)][EG]));
            if (j < 7)
                printf(", ");
        }
        printf("},\n");
    }
    printf("};\n\n");
    //cout << *index << endl;
    //print2("passedBlockedBonus", index, terms, 2, 8);
    printf("const Score %s[%d][%d] = {\n", "passedBlockedBonus", 2, 8);
    for (int i = 0; i < 2; i++)
    {
        printf("    {S(0,0), S(0,0), ");
        for (int j = 2; j < 8; j++, (*index)++)
        {
            if (j && j % 8 == 0) printf("\n    ");
            printf("S(%d, %d)", (int)(terms[*index][MG]), (int)(terms[(*index)][EG]));
            if (j < 7)
                printf(", ");
        }
        printf("},\n");
    }
    printf("};\n\n");

    //cout << *index << endl;
    print1("passedFriendlyDistance", index, terms, 8);
    //cout << *index << endl;
    print1("passedEnemyDistance", index, terms, 8);
    //cout << *index << endl;
    print0("tarraschRule_enemy", index, terms);
    //cout << *index << endl;
    print1("tarraschRule_friendly", index, terms, 8);
}

void init_passedTerms(int *index)
{
    for (int i = 2; i < 8; i++)
    {
        params[*index][0] =  mg_value(passedRankBonus[i]);
        params[(*index)++][1] = eg_value(passedRankBonus[i]);
    }



    for (int i = 0; i < 2; i++)
        for (int j = 2; j < 8; j++)
        {
            params[*index][0] =  mg_value(passedUnsafeBonus[i][j]);
            params[(*index)++][1] = eg_value(passedUnsafeBonus[i][j]);
        }



    for (int i = 0; i < 2; i++)
        for (int j = 2; j < 8; j++)
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
    for (int i = 2; i < 8; i++)
    {
        coeffs[*index][0] =  (Trace.passedRankBonus[0][i]);
        coeffs[(*index)++][1] = (Trace.passedRankBonus[1][i]);
    }



    for (int i = 0; i < 2; i++)
        for (int j = 2; j < 8; j++)
        {
            coeffs[*index][0] =  (Trace.passedUnsafeBonus[0][i][j]);
            coeffs[(*index)++][1] = (Trace.passedUnsafeBonus[1][i][j]);
        }



    for (int i = 0; i < 2; i++)
        for (int j = 2; j < 8; j++)
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
    //cout << *index << endl;
    print1("isolated_penaltyAH", index, terms, 2);
    //cout << *index << endl;
    print1("isolated_doubled_penalty", index, terms, 2);
    //cout << *index << endl;
    print1("isolated_doubled_penaltyAH", index, terms, 2);
    //cout << *index << endl;
    print1("doubled_penalty", index, terms, 2);
    //cout << *index << endl;
    print1("doubled_penalty_undefended", index, terms, 2);
    //cout << *index << endl;
    print1("backward_penalty", index, terms, 2);
    //cout << *index << endl;
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
    //cout << *index << endl;
    print1("bishopMobilityBonus", index, terms, 14);
    //cout << *index << endl;
    print1("rookMobilityBonus", index, terms, 15);
    //cout << *index << endl;
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
    //cout << *index << endl;
    print0("kingProtector", index, terms);
    //cout << *index << endl;
    print2("outpostBonus", index, terms, 2, 2);
    //cout << *index << endl;
    print1("reachableOutpost", index, terms, 2);
    //cout << *index << endl;
    print0("bishopOpposerBonus", index, terms);
    //cout << *index << endl;
    print0("trappedBishopPenalty", index, terms);
    //cout << *index << endl;
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
    //cout << *index << endl;
    print0("rank7Rook", index, terms);
    //cout << *index << endl;
    print1("rookFile", index, terms, 2);
    //cout << *index << endl;
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
    if (tuneMobility || printAll) print_mobilityTerms(&index, terms); else index += 66;
    if (tuneWeights || printAll) print_pieceWeights(&index, terms); else index += 5;
    if (tunePSQT || printAll) print_PSQTs(&index, terms); else index += 384;
    if (tuneImbalance || printAll) print_imbalance_weights(&index, terms); else index += 26;
    if (tunePassed || printAll) print_passedTerms(&index, terms); else index += 55;
    if (tuneKing || printAll) print_kingTerms(&index, terms); else index += 2;
    if (tuneThreat || printAll) print_threatTerms(&index, terms); else index += 19;
    if (tunePawns || printAll) print_pawnTerms(&index, terms); else index += 46;
    if (tuneMinors || printAll) print_minorTerms(&index, terms); else index += 11;
    if (tuneRooks || printAll) print_rookTerms(&index, terms);
}

void init_params()
{
    int index = 0;
    init_mobilityTerms(&index);
    init_pieceWeights(&index);
    init_PSQTs(&index);
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
    int index = 0;
    if (tuneMobility) init_mobilityCoeffs(&index); else index += 66;
    if (tuneWeights) init_pieceCoeffs(&index); else index += 5;
    if (tunePSQT) init_PSQTCoeffs(&index); else index += 384;
    if (tuneImbalance) init_imbalance_coeffs(&index); else index += 26;
    if (tunePassed) init_passedCoeffs(&index); else index += 55;
    if (tuneKing) init_kingCoeffs(&index); else index += 2;
    if (tuneThreat) init_threatCoeffs(&index); else index += 19;
    if (tunePawns) init_pawnCoeffs(&index); else index += 46;
    if (tuneMinors) init_minorCoeffs(&index); else index += 11;
    if (tuneRooks) init_rookCoeffs(&index);
}

void initTuples(RMStunerEntry *entry)
{
    memset(&coeffs, 0, sizeof(coeffs));
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
    entry->dangerScore[WHITE] = Trace.kingDangerScore[WHITE];
    entry->dangerScore[BLACK] = Trace.kingDangerScore[BLACK];
    entry->forcedDraw = Trace.forcedDraw;
    initTuples(entry);
}

int sanityCheckEval(RMStunerEntry *entry)
{
    double mg = 0, eg = 0;
    double imbalancevalue = 0;

    for (int i = 0; i < entry->ntuples; i++)
    {
        if (types[entry->tuples[i].index] == NORMALTYPE)
        {
            mg += (double)entry->tuples[i].wcoeff * params[entry->tuples[i].index][MG];
            mg -= (double)entry->tuples[i].bcoeff * params[entry->tuples[i].index][MG];
            eg += (double)entry->tuples[i].wcoeff * params[entry->tuples[i].index][EG];
            eg -= (double)entry->tuples[i].bcoeff * params[entry->tuples[i].index][EG];
            //cout << "Index " << (entry->tuples[i].index) << " " << (entry->tuples[i].wcoeff - entry->tuples[i].bcoeff) << " * " << params[entry->tuples[i].index][EG]<<" = "<<(entry->tuples[i].wcoeff - entry->tuples[i].bcoeff)* params[entry->tuples[i].index][EG]<<endl;
        }
        else
        {
            imbalancevalue += (double)entry->tuples[i].wcoeff * params[entry->tuples[i].index][MG];
            imbalancevalue -= (double)entry->tuples[i].bcoeff * params[entry->tuples[i].index][MG];
            //cout << "Index " << (entry->tuples[i].index) << " " << (entry->tuples[i].wcoeff - entry->tuples[i].bcoeff) << " * " << params[entry->tuples[i].index][MG]<<" = "<<(entry->tuples[i].wcoeff - entry->tuples[i].bcoeff)* params[entry->tuples[i].index][MG]<<endl;
        }
    }

    imbalancevalue /= 16.0;
    mg += imbalancevalue;
    eg += imbalancevalue;

    mg += mg_value(entry->dangerScore[WHITE] - entry->dangerScore[BLACK]);
    eg += eg_value(entry->dangerScore[WHITE] - entry->dangerScore[BLACK]);


    #if 0
    cout << "imbalance value "<<imbalancevalue<<endl;
    cout <<"dangerM "<<mg_value(entry->dangerScore[WHITE] - entry->dangerScore[BLACK])<<endl;
    cout <<"dangerE "<<eg_value(entry->dangerScore[WHITE] - entry->dangerScore[BLACK])<<endl;
    cout << "MG total "<<mg << " EG total "<<eg<<endl;
    cout << "MG original "<<mg_value(entry->originalScore)<<" EG value "<<eg_value(entry->originalScore)<<endl;
    cout << "phase "<<entry->phase<<endl;
    #endif
    double out = (mg*(256.0-entry->phase) + (eg*entry->phase*entry->scale)) / 256.0;
    return round(out + (entry->forcedDraw ? 0 : 1)*(entry->activeSide ? -tempo : tempo));
}

void read_file_data() {
    ifstream fens;
    fens.open("../lichess-quiet.book");
    string line;
    Position *pos;
    init_params();
    RMStuningData = (RMStunerEntry*)calloc(NFENS, sizeof(RMStunerEntry));

    for (int i = 0; i < NFENS; i++) {
        getline(fens, line);
        double result;

        if (strstr(line.c_str(), "[1.0]") || strstr(line.c_str(), "White")) {
            result = 1.0;
        }
        else if (strstr(line.c_str(), "[0.0]") || strstr(line.c_str(), "Black")) {
            result = 0.0;
        }
        else if (strstr(line.c_str(), "[0.5]") || strstr(line.c_str(), "Draw")) {
            result = 0.5;
        }
        else {
            cout << line << endl;
            result = -1.0;
            exit(1);
        }

        memset(&Trace, 0, sizeof(tunerTrace));
        pos = import_fen(line.c_str(), 0);

        #if QSRESOLVE
        if (pos->checkBB)
        {
            get_ready();
            SearchThread* thr = pos->my_thread;
            searchInfo* info = &thr->ss[3];
            info->chosenMove = MOVE_NONE;
            info->staticEval = UNDEFINED;
            info->ply = 0;
            info->pvLen = 0;
            qSearch(thr, info, 0, VALUE_MATED, VALUE_MATE);
            for (int i = 0; i < info->pvLen; i++)
                pos->do_move(info->pv[i]);
        }
        #endif

        addRMSentry(&RMStuningData[i], pos, result);

        #if 0
        int sanityEval = sanityCheckEval(&RMStuningData[i]);
        if (abs(sanityEval - RMStuningData[i].staticEval) > 5)
            cout << sanityEval << " vs " << RMStuningData[i].staticEval<<" "<<line<<endl;
        #endif

        if (i && i % 1000000 == 0) {
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
    double imbalancevalue = 0;
    // Get deltas
    for (int i = 0; i < entry->ntuples; i++)
    {
        if (types[entry->tuples[i].index] == NORMALTYPE)
        {
            mg += (double)entry->tuples[i].wcoeff * deltas[entry->tuples[i].index][MG];
            mg -= (double)entry->tuples[i].bcoeff * deltas[entry->tuples[i].index][MG];
            eg += (double)entry->tuples[i].wcoeff * deltas[entry->tuples[i].index][EG];
            eg -= (double)entry->tuples[i].bcoeff * deltas[entry->tuples[i].index][EG];
        }
        else
        {
            imbalancevalue += (double)entry->tuples[i].wcoeff * deltas[entry->tuples[i].index][MG];
            imbalancevalue -= (double)entry->tuples[i].bcoeff * deltas[entry->tuples[i].index][MG];
        }
    }

    imbalancevalue /= 16.0;

    // add deltas to original eval
    mg += (double)mg_value(entry->originalScore) + imbalancevalue;
    eg += (double)eg_value(entry->originalScore) + imbalancevalue;

    double out = (mg*(256.0-entry->phase) + (eg*entry->phase*entry->scale)) / 256.0;
    return out + (entry->forcedDraw ? 0 : 1)*(entry->activeSide ? -tempo : tempo);
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

    reportParams();

    int correctCounter = 0;
    for (int i = 0; i < NFENS; i++)
    {
        switch(int(RMStuningData[i].actualscore*10))
        {
        case 0:
            if (fastEval(&RMStuningData[i]) < RMStuningData[i].staticEval - 5)
                correctCounter++;
            break;
        case 10:
            if (fastEval(&RMStuningData[i]) > RMStuningData[i].staticEval + 5)
                correctCounter++;
            break;
        case 5:
            if (abs(fastEval(&RMStuningData[i])) < abs(RMStuningData[i].staticEval) - 5)
                correctCounter++;
            break;
        }
    }
    cout <<  correctCounter / (double)NFENS << endl;
    #endif
}


/*********************************************** Local Search Stuff *************************************************************************/
struct tunerEntry
{
    string fen;
    double score;
    localTrace myTrace;

    bool activeSide;
    int phase;
};

vector <tunerEntry> tuningData;

void addTunerEntry(string line, Position *pos, double result)
{
    localTrace myTrace;
    memcpy(&myTrace, &safetyTrace, sizeof(localTrace));

    int phase = (24 - (pos->pieceCount[WBISHOP] + pos->pieceCount[BBISHOP] + pos->pieceCount[WKNIGHT] + pos->pieceCount[BKNIGHT])
        - 2 * (pos->pieceCount[WROOK] + pos->pieceCount[BROOK])
        - 4 * (pos->pieceCount[WQUEEN] + pos->pieceCount[BQUEEN]));

    phase = (phase * 255 + 12) / 24;

    tuningData.push_back({line, result, myTrace, pos->activeSide, phase});
}

void read_localdata() {
    ifstream fens;
    fens.open("../lichess-quiet.book");
    string line;

    for (int i = 0; i < NFENS; i++) {
        getline(fens, line);
        double result;
        Position *p;

        if (strstr(line.c_str(), "[1.0]") || strstr(line.c_str(), "White")) {
            result = 1.0;
        }
        else if (strstr(line.c_str(), "[0.0]") || strstr(line.c_str(), "Black")) {
            result = 0.0;
        }
        else if (strstr(line.c_str(), "[0.5]") || strstr(line.c_str(), "Draw")) {
            result = 0.5;
        }
        else {
            cout << line << endl;
            result = -1.0;
            exit(1);
        }

        memset(&safetyTrace, 0, sizeof(localTrace));
        p = import_fen(line.c_str(), 0);
        addTunerEntry(line, p, result);

        if (i && i % 1000000 == 0) {
            cout << "Reading line " << i << endl;
        }
    }
    cout << "Read " << NFENS << " lines"<<endl;
    fens.close();
}

double fastKingEval(tunerEntry *entry)
{
    if (entry->myTrace.forcedDraw)
        return 0;

    double mg, eg = 0;
    double danger[2] = {0};
    for (int side = 0; side < 2; side++)
    {
        if (entry->myTrace.kingDangerBase[side])
        {
            int shelterTotal = 0;
            for (int j = 0; j < 4; j++)
                for (int k = 0; k < 8; k++)
            {
                shelterTotal += entry->myTrace.kingShield[side][j][k] * kingShield[j][k];
                shelterTotal += entry->myTrace.pawnStormBlocked[side][j][k] * pawnStormBlocked[j][k];
                shelterTotal += entry->myTrace.pawnStormFree[side][j][k] * pawnStormFree[j][k];
            }

            mg += side ? -shelterTotal : shelterTotal;

            int attackWeightTotal = 0;
            for (int piece = 0; piece < 7; piece++)
            {
                attackWeightTotal += entry->myTrace.attackerWeights[side][piece] * attackerWeights[piece];
            }
            danger[side] = kingDangerBase * entry->myTrace.kingDangerBase[side]
                            - shelterTotal * kingShieldBonus / 10.0
                            - entry->myTrace.noQueen[side] * noQueen
                            + entry->myTrace.attackerCount[side] * attackWeightTotal
                            + entry->myTrace.attackCount[side] * kingringAttack
                            + entry->myTrace.kingpinnedPenalty[side] * kingpinnedPenalty
                            + entry->myTrace.kingweakPenalty[side] * kingweakPenalty;
            for (int piece = 0; piece < 7; piece++)
            {
                danger[side] += entry->myTrace.checkPenalty[side][piece] * checkPenalty[piece];
                danger[side] += entry->myTrace.unsafeCheckPenalty[side][piece] * unsafeCheckPenalty[piece];
            }
        }

        danger[side] = max(danger[side], 0.0);
    }

    mg = (danger[BLACK] * danger[BLACK] - danger[WHITE] * danger[WHITE]) / 4096.0;
    eg = (danger[BLACK] - danger[WHITE]) / 20.0;

    mg += (double)mg_value(entry->myTrace.originalScore);
    eg += (double)eg_value(entry->myTrace.originalScore);

    double out = (mg*(256.0-entry->phase) + (eg*entry->phase*entry->myTrace.scale)) / 256.0;
    return out + (entry->activeSide ? -tempo : tempo);
}

double getSlowError(tunerEntry *entry, int tID)
{
    Position* p = import_fen(entry->fen.c_str(), tID);

    SearchThread* thr = p->my_thread;
    searchInfo* info = &thr->ss[2];
    info->chosenMove = MOVE_NONE;
    info->staticEval = UNDEFINED;
    info->ply = 0;

    int qi = evaluate(thr->position) * S2MSIGN(p->activeSide);

    double sg = sigmoid(qi, K);
    return (pow(entry->score - sg, 2.0));
}

double localError() {

    double total = 0.0;

    #pragma omp parallel shared(total)
    {
        #pragma omp for schedule(static, 1) reduction(+:total)
        for (int tID = 0; tID < NTHREADS; tID++)
            for (int i = tID * (NFENS / NTHREADS); i < (tID + 1) * (NFENS / NTHREADS); i++)
                total += getSlowError(&tuningData[i], tID);
    }

    return total / (double) NFENS;
}

void init_LocalTerms(vector <Parameter>& p)
{
    for (int i = 0; i < 7; i++)
    {
        if (i < 2 || i > 5)
            continue;
        p.push_back({ nullptr, &attackerWeights[i], attackerWeights[i], 2, 1, 0, 0 });
    }

    for (int i = 0; i < 7; i++)
    {
        if (i < 2 || i > 5)
            continue;
        p.push_back({ nullptr, &checkPenalty[i], checkPenalty[i], 2, 1, 0, 0 });
    }

    for (int i = 0; i < 7; i++)
    {
        if (i < 2 || i > 5)
            continue;
        p.push_back({ nullptr, &unsafeCheckPenalty[i], unsafeCheckPenalty[i], 2, 1, 0, 0 });
    }

    p.push_back({ nullptr, &queenContactCheck, queenContactCheck, 2, 1, 0, 0 });
    p.push_back({ nullptr, &kingDangerBase, kingDangerBase, 2, 1, 0, 0 });
    p.push_back({ nullptr, &kingringAttack, kingringAttack, 2, 1, 0, 0 });
    p.push_back({ nullptr, &kingpinnedPenalty, kingpinnedPenalty, 2, 1, 0, 0 });
    p.push_back({ nullptr, &kingweakPenalty, kingweakPenalty, 2, 1, 0, 0 });
    p.push_back({ nullptr, &kingShieldBonus, kingShieldBonus, 2, 1, 0, 0 });
    p.push_back({ nullptr, &noQueen, noQueen, 2, 1, 0, 0 });

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
        {
            if (j > 6)
                continue;
            p.push_back({ nullptr, &kingShield[i][j], kingShield[i][j], 2, 1, 0, 0 });
        }

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
        {
            if (j < 2 || j > 6)
                continue;
            p.push_back({ nullptr, &pawnStormBlocked[i][j], pawnStormBlocked[i][j], 2, 1, 0, 0 });
        }

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
        {
            if (j > 6)
                continue;
            p.push_back({ nullptr, &pawnStormFree[i][j], pawnStormFree[i][j], 2, 1, 0, 0 });
        }
}

void print_localTerms()
{
    cout << "=============================="<<endl;
    printf("\nint attackerWeights[7] = {");
    for (int i = 0; i < 7; i++)
    {
        printf("%d", attackerWeights[i]);
        if (i < 6)
            printf(", ");
    }
    printf("};\n\n");
    printf("int checkPenalty[7] = {");
    for (int i = 0; i < 7; i++)
    {
        printf("%d", checkPenalty[i]);
        if (i < 6)
            printf(", ");
    }
    printf("};\n\n");

    printf("int unsafeCheckPenalty[7] = {");
    for (int i = 0; i < 7; i++)
    {
        printf("%d", unsafeCheckPenalty[i]);
        if (i < 6)
            printf(", ");
    }
    printf("};\n\n");

    printf("int queenContactCheck = %d;\n", queenContactCheck);
    printf("int kingDangerBase = %d;\n", kingDangerBase);
    printf("int kingringAttack = %d;\n", kingringAttack);
    printf("int kingpinnedPenalty = %d;\n", kingpinnedPenalty);
    printf("int kingweakPenalty = %d;\n", kingweakPenalty);
    printf("int kingShieldBonus = %d;\n", kingShieldBonus);
    printf("int noQueen = %d;\n\n", noQueen);

    printf("\n");

    printf("int kingShield[4][8] = {");
    for (int i = 0; i < 4; i++)
    {
        printf("\n    {");
        for (int j = 0; j < 8; j++)
        {
            printf("%d", kingShield[i][j]);
            if (j < 7)
                printf(", ");
        }
        printf("}");
        if (i < 3)
            printf(",");
    }
    printf("\n};\n\n");

    printf("int pawnStormBlocked[4][8] = {");
    for (int i = 0; i < 4; i++)
    {
        printf("\n    {");
        for (int j = 0; j < 8; j++)
        {
            printf("%d", pawnStormBlocked[i][j]);
            if (j < 7)
                printf(", ");
        }
        printf("}");
        if (i < 3)
            printf(",");
    }
    printf("\n};\n\n");

    printf("int pawnStormFree[4][8] = {");
    for (int i = 0; i < 4; i++)
    {
        printf("\n    {");
        for (int j = 0; j < 8; j++)
        {
            printf("%d", pawnStormFree[i][j]);
            if (j < 7)
                printf(", ");
        }
        printf("}");
        if (i < 3)
            printf(",");
    }
    printf("\n};\n\n");
    cout << "=============================="<<endl;
}


void set_parameter(Parameter& param)
{
    switch (param.flag)
    {
    case 0:
        *param.address = S(param.value, eg_value(*param.address));
        break;
    case 1:
        *param.address = S(mg_value(*param.address), param.value);
        break;
    case 2:
        *param.constaddress = param.value;
        break;
    }

    init_values();

}

void localGuess(vector<Parameter>& params, double initial_best)
{
    cout << "Updating Gradient..." << endl;
    double bestError = initial_best;
    for (unsigned i = 0; i < params.size(); i++)
    {
        Parameter& param = params[i];
        int delta = max(10, abs(param.value / 5));
        int minVal = param.value - delta + param.valuedelta / 2;
        int maxVal = param.value + delta + param.valuedelta / 2;
        param.value = minVal;
        set_parameter(param);
        double min_error = localError();
        param.value = maxVal;
        set_parameter(param);
        double max_error = localError();
        param.errordelta = bestError;
        param.valuedelta = param.value;

        while (maxVal > minVal)
        {
            if (min_error < max_error)
            {
                if (minVal == maxVal - 1)
                {
                    param.value = minVal;
                    set_parameter(param);
                    bestError = min_error;
                    break;
                }
                maxVal = (maxVal + minVal) / 2;
                param.value = maxVal;
                set_parameter(param);
                max_error = localError();
            }
            else
            {
                if (minVal == maxVal - 1)
                {
                    param.value = maxVal;
                    set_parameter(param);
                    bestError = max_error;
                    break;
                }
                minVal = (maxVal + minVal) / 2;
                param.value = minVal;
                set_parameter(param);
                min_error = localError();
            }

            printf("Parameter %u / %zu : V / E : min %d / %f, max %d / %f \n", i + 1, params.size(), minVal, min_error, maxVal, max_error);
        }
        param.errordelta = abs(bestError - param.errordelta);
        param.valuedelta = param.value - param.valuedelta;
    }
}

void localTune()
{
    reset_threads(NTHREADS);
    srand(time(0));
    read_localdata();
    vector <Parameter> params;

    init_LocalTerms(params);

    double bestError = localError();
    double newError;
    cout << "Training error: " << bestError << endl;

    for (int epoch = 0; epoch < 2; epoch++)
    {
        for (unsigned i = 0; i < params.size(); i++)
        {
            params[i].stability = 1;
        }
        random_shuffle(params.begin(), params.end());
        localGuess(params, bestError); //get a good guess
        cout << "Beginning local search..." << endl;
        bool improving = true;
        bestError = localError();
        while (improving)
        {
            sort(params.begin(), params.end(), greater <Parameter> ());
            improving = false;

            for (unsigned i = 0; i < params.size(); i++)
            {
                if (params[i].stability >= 3)
                    continue;

                Parameter& param = params[i];
                param.errordelta = bestError;
                int delta = (param.valuedelta > 0) ? 1 : -1;
                param.value += delta;
                param.valuedelta += delta;
                set_parameter(param);

                newError = localError();

                if (newError < bestError)
                {
                    bestError = newError;
                    improving = true;
                    param.stability = 1;
                    param.errordelta = abs(bestError - param.errordelta);
                    printf("[Epoch %d] Parameter %u / %zu : V / E  %d / %f \n", epoch + 1, i + 1, params.size(), param.value, bestError);
                    continue;
                }

                delta *= -2;
                param.value += delta;
                param.valuedelta += delta;
                set_parameter(param);

                newError = localError();

                if (newError < bestError)
                {
                    bestError = newError;
                    improving = true;
                    param.stability = 1;
                    param.errordelta = abs(bestError - param.errordelta);
                    printf("[Epoch %d] Parameter %u / %zu : V / E  %d / %f\n", epoch + 1, i + 1, params.size(), param.value, bestError);
                    continue;
                }

                delta = -(delta / 2);
                param.value += delta;
                param.valuedelta += delta;
                set_parameter(param);

                param.stability++;
                param.errordelta = 0.0;
                printf("[Epoch %d] Parameter %u / %zu : V / E  %d / %f (No improvement)\n", epoch + 1, i + 1, params.size(), param.value, bestError);
            }
        }

        print_localTerms();
    }
}
#endif

