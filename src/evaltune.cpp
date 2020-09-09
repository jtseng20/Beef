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
#define __quiet__ // using zurichess quiet; else lichess-quiet

struct tunerEntry
{
    string fen;
    double score;
};

vector <tunerEntry> tuningData;
unsigned int num_fens = 0;

#ifdef __quiet__
double K = 1.121291;
#else
double K = 0.226000;
#endif

void fen_split(string s, vector<string>& v)
{
#ifdef __quiet__
    size_t i = s.length() - 3;
    size_t stop;
    if (s[i] == '2')
        stop = i - 8;
    else
        stop = i - 4;
    v.push_back(s.substr(0, stop - 3));
    v.push_back(s.substr(stop + 1));
#else
    size_t i = s.length() - 1;
    size_t stop;
    if (s[i] == 'w')
        stop = i - 4;
    else
        stop = i - 5;

    v.push_back(s.substr(0, stop));
    v.push_back(s.substr(stop + 1));
#endif
}

void read_file_data() {
    ifstream fens;
#ifdef __quiet__
    fens.open("../quiet-labeled.epd");
#else
    fens.open("../lichess-quiet.epd");
#endif
    string line;

    while (getline(fens, line)) {
        vector<string> fen_info;
        fen_split(line, fen_info);

        string result_str = fen_info[1];
        double result;
#ifdef __quiet__
        if (result_str == "\"1-0\";") {
            result = 1.0;
        }
        else if (result_str == "\"0-1\";") {
            result = 0.0;
        }
        else if (result_str == "\"1/2-1/2\";") {
            result = 0.5;
        }
        else {
            result = -1.0;
            exit(1);
        }

#else
        if (result_str == "White") {
            result = 1.0;
        }
        else if (result_str == "Draw") {
            result = 0.0;
        }
        else if (result_str == "Black") {
            result = 0.5;
        }
        else {
            result = -1.0;
            exit(1);
        }
#endif

        tuningData.push_back({ fen_info[0], result });

        ++num_fens;
        if (num_fens % 1000000 == 0) {
            cout << "Reading line " << num_fens << endl;
        }
    }
    cout << "Total lines: " << num_fens << endl;
    fens.close();
}

double sigmoid(double s, double k) {
    return 1.0 / (1.0 + pow(10.0, -k * s / 400.0));
}

vector<double> diffs[MAX_THREADS];
vector<string> debugs[MAX_THREADS];

void single_error(int thread_id, double k) {
    for (unsigned i = thread_id; i < num_fens; i += num_threads) {
        tunerEntry* entry = &tuningData[i];

        Position* p = import_fen(entry->fen.c_str(), thread_id);
        if (p->checkBB) {
            continue;
        }

        SearchThread* thr = p->my_thread;
        searchInfo* info = &thr->ss[2];
        info->chosenMove = MOVE_NONE;
        info->staticEval = UNDEFINED;
        info->ply = 0;


        //int qi = qSearch(thr, info, -1, VALUE_MATED, VALUE_MATE) * S2MSIGN(p->activeSide);
        int qi = evaluate(thr->position) * S2MSIGN(p->activeSide);

        double sg = sigmoid(qi, k);
        diffs[thread_id].push_back(pow(entry->score - sg, 2.0));
    }
}

/// Using kahan sum if on GCC else normal sum
#ifdef __GNUC__
#pragma GCC push_options
#pragma GCC optimize ("O0")
double error_sum() {
    double sum, c, y, t;
    sum = 0.0;
    c = 0.0;
    for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
        for (unsigned i = 0; i < diffs[thread_id].size(); ++i) {
            y = diffs[thread_id][i] - c;
            t = sum + y;
            c = (t - sum) - y;
            sum = t;
        }
    }
    return sum;
}
#pragma GCC pop_options

#else
double error_sum()
{
    double sum = 0.0;
    for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
        for (unsigned i = 0; i < diffs[thread_id].size(); ++i) {
            sum += diffs[thread_id][i];
        }
    }
    return sum;
}
#endif

double find_error(double k) {

    std::thread threads[MAX_THREADS];
    for (int i = 0; i < num_threads; ++i) {
        diffs[i].clear();
        threads[i] = std::thread(single_error, i, k);
    }

    unsigned total_size = 0;
    for (int i = 0; i < num_threads; ++i) {
        threads[i].join();
        total_size += diffs[i].size();
    }
    return error_sum() / ((double)total_size);
}

void find_optimal_k()
{
    read_file_data();
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
            double newerror = find_error(guess);
            cout << guess << " " << newerror << endl;
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
    K = start;
}

void init_imbalance_weights(vector <Parameter>& p)
{
    p.push_back({ nullptr, &my_pieces[0][0], my_pieces[0][0], 2, 1, 0, 0 });

    p.push_back({ nullptr, &my_pieces[1][0], my_pieces[1][0], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[1][1], my_pieces[1][1], 2, 1, 0, 0 });

    p.push_back({ nullptr, &my_pieces[2][0], my_pieces[2][0], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[2][1], my_pieces[2][1], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[2][2], my_pieces[2][2], 2, 1, 0, 0 });

    p.push_back({ nullptr, &my_pieces[3][0], my_pieces[3][0], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[3][1], my_pieces[3][1], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[3][2], my_pieces[3][2], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[3][3], my_pieces[3][3], 2, 1, 0, 0 });

    p.push_back({ nullptr, &my_pieces[4][0], my_pieces[4][0], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[4][1], my_pieces[4][1], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[4][2], my_pieces[4][2], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[4][3], my_pieces[4][3], 2, 1, 0, 0 });
    p.push_back({ nullptr, &my_pieces[4][4], my_pieces[4][4], 2, 1, 0, 0 });


    p.push_back({ nullptr, &opponent_pieces[1][0], opponent_pieces[1][0], 2, 1, 0, 0  });

    p.push_back({ nullptr, &opponent_pieces[2][0], opponent_pieces[2][0], 2, 1, 0, 0  });
    p.push_back({ nullptr, &opponent_pieces[2][1], opponent_pieces[2][1], 2, 1, 0, 0  });

    p.push_back({ nullptr, &opponent_pieces[3][0], opponent_pieces[3][0], 2, 1, 0, 0  });
    p.push_back({ nullptr, &opponent_pieces[3][1], opponent_pieces[3][1], 2, 1, 0, 0  });
    p.push_back({ nullptr, &opponent_pieces[3][2], opponent_pieces[3][2], 2, 1, 0, 0  });

    p.push_back({ nullptr, &opponent_pieces[4][0], opponent_pieces[4][0], 2, 1, 0, 0  });
    p.push_back({ nullptr, &opponent_pieces[4][1], opponent_pieces[4][1], 2, 1, 0, 0  });
    p.push_back({ nullptr, &opponent_pieces[4][2], opponent_pieces[4][2], 2, 1, 0, 0  });
    p.push_back({ nullptr, &opponent_pieces[4][3], opponent_pieces[4][3], 2, 1, 0, 0  });

    p.push_back({ nullptr, &bishop_pair, bishop_pair, 2, 1, 0, 0  });
}

void init_kingTerms(vector <Parameter>& p)
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
    p.push_back({ nullptr, &kingflankAttack, kingflankAttack, 2, 1, 0, 0 });
    p.push_back({ nullptr, &kingringAttack, kingringAttack, 2, 1, 0, 0 });
    p.push_back({ nullptr, &kingpinnedPenalty, kingpinnedPenalty, 2, 1, 0, 0 });
    p.push_back({ nullptr, &kingweakPenalty, kingweakPenalty, 2, 1, 0, 0 });
    p.push_back({ nullptr, &pawnDistancePenalty, pawnDistancePenalty, 2, 1, 0, 0 });
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

    for (int j = 0; j < 64; j++)
    {
        p.push_back({ &piece_bonus[KING][j], nullptr, mg_value(piece_bonus[KING][j]), 0, 1, 0, 0 });
        p.push_back({ &piece_bonus[KING][j], nullptr, eg_value(piece_bonus[KING][j]), 1, 1, 0, 0 });
    }
}

void init_threatTerms(vector <Parameter>& p)
{
    for (int i = 0; i < 7; i++)
    {
        if (i < 1)
            continue;
        p.push_back({ &minorThreat[i], nullptr, mg_value(minorThreat[i]), 0, 1, 0, 0 });
        p.push_back({ &minorThreat[i], nullptr, eg_value(minorThreat[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 7; i++)
    {
        if (i < 1)
            continue;
        p.push_back({ &rookThreat[i], nullptr, mg_value(rookThreat[i]), 0, 1, 0, 0 });
        p.push_back({ &rookThreat[i], nullptr, eg_value(rookThreat[i]), 1, 1, 0, 0 });
    }

    p.push_back({ &kingThreat, nullptr, mg_value(kingThreat), 0, 1, 0, 0 });
    p.push_back({ &kingThreat, nullptr, eg_value(kingThreat), 1, 1, 0, 0 });

    p.push_back({ &kingMultipleThreat, nullptr, mg_value(kingMultipleThreat), 0, 1, 0, 0 });
    p.push_back({ &kingMultipleThreat, nullptr, eg_value(kingMultipleThreat), 1, 1, 0, 0 });

    p.push_back({ &pawnPushThreat, nullptr, mg_value(pawnPushThreat), 0, 1, 0, 0 });
    p.push_back({ &pawnPushThreat, nullptr, eg_value(pawnPushThreat), 1, 1, 0, 0 });

    p.push_back({ &safePawnThreat, nullptr, mg_value(safePawnThreat), 0, 1, 0, 0 });
    p.push_back({ &safePawnThreat, nullptr, eg_value(safePawnThreat), 1, 1, 0, 0 });

    p.push_back({ &hangingPiece, nullptr, mg_value(hangingPiece), 0, 1, 0, 0 });
    p.push_back({ &hangingPiece, nullptr, eg_value(hangingPiece), 1, 1, 0, 0 });
}

void init_pieceWeights(vector<Parameter>& p)
{
    p.push_back({ nullptr, &PAWN_EG, PAWN_EG, 2, 1, 0, 0 });
    p.push_back({ nullptr, &KNIGHT_MG, KNIGHT_MG, 2, 1, 0, 0 });
    p.push_back({ nullptr, &KNIGHT_EG, KNIGHT_EG, 2, 1, 0, 0 });
    p.push_back({ nullptr, &BISHOP_MG, BISHOP_MG, 2, 1, 0, 0 });
    p.push_back({ nullptr, &BISHOP_EG, BISHOP_EG, 2, 1, 0, 0 });
    p.push_back({ nullptr, &ROOK_MG, ROOK_MG, 2, 1, 0, 0 });
    p.push_back({ nullptr, &ROOK_EG, ROOK_EG, 2, 1, 0, 0 });
    p.push_back({ nullptr, &QUEEN_MG, QUEEN_MG, 2, 1, 0, 0 });
    p.push_back({ nullptr, &QUEEN_EG, QUEEN_EG, 2, 1, 0, 0 });
}

void init_passedTerms(vector<Parameter>& p)
{
    for (int i = 0; i < 8; i++)
    {
        if (i < 2 || i > 6)
            continue;
        p.push_back({ &passedRankBonus[i], nullptr, mg_value(passedRankBonus[i]), 0, 1, 0, 0 });
        p.push_back({ &passedRankBonus[i], nullptr, eg_value(passedRankBonus[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 8; j++)
        {
            if (j < 2 || j > 6)
                continue;
            p.push_back({ &passedUnsafeBonus[i][j], nullptr, mg_value(passedUnsafeBonus[i][j]), 0, 1, 0, 0 });
            p.push_back({ &passedUnsafeBonus[i][j], nullptr, eg_value(passedUnsafeBonus[i][j]), 1, 1, 0, 0 });
        }

    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 8; j++)
        {
            if (j < 2 || j > 6)
                continue;
            p.push_back({ &passedBlockedBonus[i][j], nullptr, mg_value(passedBlockedBonus[i][j]), 0, 1, 0, 0 });
            p.push_back({ &passedBlockedBonus[i][j], nullptr, eg_value(passedBlockedBonus[i][j]), 1, 1, 0, 0 });
        }

    for (int i = 1; i < 7; i++)
    {
        p.push_back({ &passedFriendlyDistance[i], nullptr, mg_value(passedFriendlyDistance[i]), 0, 1, 0, 0 });
        p.push_back({ &passedFriendlyDistance[i], nullptr, eg_value(passedFriendlyDistance[i]), 1, 1, 0, 0 });
    }

    for (int i = 1; i < 7; i++)
    {
        p.push_back({ &passedEnemyDistance[i], nullptr, mg_value(passedEnemyDistance[i]), 0, 1, 0, 0 });
        p.push_back({ &passedEnemyDistance[i], nullptr, eg_value(passedEnemyDistance[i]), 1, 1, 0, 0 });
    }

    //p.push_back({ &unstoppablePasser, nullptr, mg_value(unstoppablePasser), 0, 1, 0 });
    //p.push_back({ &unstoppablePasser, nullptr, eg_value(unstoppablePasser), 1, 1, 0 });

    p.push_back({ &tarraschRule_enemy, nullptr, mg_value(tarraschRule_enemy), 0, 1, 0, 0 });
    p.push_back({ &tarraschRule_enemy, nullptr, eg_value(tarraschRule_enemy), 1, 1, 0, 0 });

    for (int i = 0; i < 8; i++)
    {
        if (i < 3 || i > 6)
            continue;
        p.push_back({ &tarraschRule_friendly[i], nullptr, mg_value(tarraschRule_friendly[i]), 0, 1, 0, 0 });
        p.push_back({ &tarraschRule_friendly[i], nullptr, eg_value(tarraschRule_friendly[i]), 1, 1, 0, 0 });
    }
}

void init_pawnTerms(vector<Parameter>& p)
{
    for (int i = 0; i < 2; i++)
    {
        p.push_back({ &isolated_penalty[i], nullptr, mg_value(isolated_penalty[i]), 0, 1, 0, 0 });
        p.push_back({ &isolated_penalty[i], nullptr, eg_value(isolated_penalty[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 2; i++)
    {
        p.push_back({ &isolated_penaltyAH[i], nullptr, mg_value(isolated_penaltyAH[i]), 0, 1, 0, 0 });
        p.push_back({ &isolated_penaltyAH[i], nullptr, eg_value(isolated_penaltyAH[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 2; i++)
    {
        p.push_back({ &isolated_doubled_penalty[i], nullptr, mg_value(isolated_doubled_penalty[i]), 0, 1, 0, 0 });
        p.push_back({ &isolated_doubled_penalty[i], nullptr, eg_value(isolated_doubled_penalty[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 2; i++)
    {
        p.push_back({ &isolated_doubled_penaltyAH[i], nullptr, mg_value(isolated_doubled_penaltyAH[i]), 0, 1, 0, 0 });
        p.push_back({ &isolated_doubled_penaltyAH[i], nullptr, eg_value(isolated_doubled_penaltyAH[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 2; i++)
    {
        p.push_back({ &doubled_penalty[i], nullptr, mg_value(doubled_penalty[i]), 0, 1, 0, 0 });
        p.push_back({ &doubled_penalty[i], nullptr, eg_value(doubled_penalty[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 2; i++)
    {
        p.push_back({ &doubled_penalty_undefended[i], nullptr, mg_value(doubled_penalty_undefended[i]), 0, 1, 0, 0 });
        p.push_back({ &doubled_penalty_undefended[i], nullptr, eg_value(doubled_penalty_undefended[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 2; i++)
    {
        p.push_back({ &backward_penalty[i], nullptr, mg_value(backward_penalty[i]), 0, 1, 0, 0 });
        p.push_back({ &backward_penalty[i], nullptr, eg_value(backward_penalty[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 8; k++)
            {
                if ((k < 1) || (k < 2 && j == 0) || (k > 6) || (i == 1 && k > 5))
                    continue;
                p.push_back({ &connected_bonus[i][j][k], nullptr, mg_value(connected_bonus[i][j][k]), 0, 1, 0, 0 });
                p.push_back({ &connected_bonus[i][j][k], nullptr, eg_value(connected_bonus[i][j][k]), 1, 1, 0, 0 });
            }
    }

    for (int j = 0; j < 64; j++)
    {
        if (j < 8 || j > 55)
            continue;
        p.push_back({ &piece_bonus[PAWN][j], nullptr, mg_value(piece_bonus[PAWN][j]), 0, 1, 0, 0 });
        p.push_back({ &piece_bonus[PAWN][j], nullptr, eg_value(piece_bonus[PAWN][j]), 1, 1, 0, 0 });
    }
}

void init_mobilityTerms(vector<Parameter>& p)
{
    for (int i = 0; i < 9; i++)
    {
        p.push_back({ &knightMobilityBonus[i], nullptr, mg_value(knightMobilityBonus[i]), 0, 1, 0, 0 });
        p.push_back({ &knightMobilityBonus[i], nullptr, eg_value(knightMobilityBonus[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 14; i++)
    {
        p.push_back({ &bishopMobilityBonus[i], nullptr, mg_value(bishopMobilityBonus[i]), 0, 1, 0, 0 });
        p.push_back({ &bishopMobilityBonus[i], nullptr, eg_value(bishopMobilityBonus[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 15; i++)
    {
        p.push_back({ &rookMobilityBonus[i], nullptr, mg_value(rookMobilityBonus[i]), 0, 1, 0, 0 });
        p.push_back({ &rookMobilityBonus[i], nullptr, eg_value(rookMobilityBonus[i]), 1, 1, 0, 0 });
    }

    for (int i = 0; i < 28; i++)
    {
        p.push_back({ &queenMobilityBonus[i], nullptr, mg_value(queenMobilityBonus[i]), 0, 1, 0, 0 });
        p.push_back({ &queenMobilityBonus[i], nullptr, eg_value(queenMobilityBonus[i]), 1, 1, 0, 0 });
    }
}

void init_minorTerms(vector<Parameter>& p)
{
    p.push_back({ &bishopPawns, nullptr, mg_value(bishopPawns), 0, 1, 0, 0 });
    p.push_back({ &bishopPawns, nullptr, eg_value(bishopPawns), 1, 1, 0, 0 });

    p.push_back({ &kingProtector, nullptr, mg_value(kingProtector), 0, 1, 0, 0 });
    p.push_back({ &kingProtector, nullptr, eg_value(kingProtector), 1, 1, 0, 0 });

    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
        {
            p.push_back({ &outpostBonus[i][j], nullptr, mg_value(outpostBonus[i][j]), 0, 1, 0, 0 });
            p.push_back({ &outpostBonus[i][j], nullptr, eg_value(outpostBonus[i][j]), 1, 1, 0, 0 });
        }


    for (int i = 0; i < 2; i++)
    {
        p.push_back({ &reachableOutpost[i], nullptr, mg_value(reachableOutpost[i]), 1, 0, 0 });
        p.push_back({ &reachableOutpost[i], nullptr, eg_value(reachableOutpost[i]), 1, 1, 0, 0 });
    }

    p.push_back({ &bishopOpposerBonus, nullptr, mg_value(bishopOpposerBonus), 0, 1, 0, 0 });
    p.push_back({ &bishopOpposerBonus, nullptr, eg_value(bishopOpposerBonus), 1, 1, 0, 0 });

    p.push_back({ &trappedBishopPenalty, nullptr, mg_value(trappedBishopPenalty), 0, 1, 0, 0 });
    p.push_back({ &trappedBishopPenalty, nullptr, eg_value(trappedBishopPenalty), 1, 1, 0, 0 });

    p.push_back({ &veryTrappedBishopPenalty, nullptr, mg_value(veryTrappedBishopPenalty), 0, 1, 0, 0 });
    p.push_back({ &veryTrappedBishopPenalty, nullptr, eg_value(veryTrappedBishopPenalty), 1, 1, 0, 0 });

    for (int j = 0; j < 64; j++)
    {
        p.push_back({ &piece_bonus[KNIGHT][j], nullptr, mg_value(piece_bonus[KNIGHT][j]), 0, 1, 0, 0 });
        p.push_back({ &piece_bonus[KNIGHT][j], nullptr, eg_value(piece_bonus[KNIGHT][j]), 1, 1, 0, 0 });
    }

    for (int j = 0; j < 64; j++)
    {
        p.push_back({ &piece_bonus[BISHOP][j], nullptr, mg_value(piece_bonus[BISHOP][j]), 0, 1, 0, 0 });
        p.push_back({ &piece_bonus[BISHOP][j], nullptr, eg_value(piece_bonus[BISHOP][j]), 1, 1, 0, 0 });
    }
}

void init_rookTerms(vector<Parameter>& p)
{
    p.push_back({ &defendedRookFile, nullptr, mg_value(defendedRookFile), 0, 1, 0, 0 });
    p.push_back({ &defendedRookFile, nullptr, eg_value(defendedRookFile), 1, 1, 0, 0 });

    p.push_back({ &rank7Rook, nullptr, mg_value(rank7Rook), 0, 1, 0, 0 });
    p.push_back({ &rank7Rook, nullptr, eg_value(rank7Rook), 1, 1, 0, 0 });

    for (int i = 0; i < 2; i++)
    {
        p.push_back({ &rookFile[i], nullptr, mg_value(rookFile[i]), 0, 1, 0, 0 });
        p.push_back({ &rookFile[i], nullptr, eg_value(rookFile[i]), 1, 1, 0, 0 });
    }

    p.push_back({ &battery, nullptr, mg_value(battery), 0, 1, 0, 0 });
    p.push_back({ &battery, nullptr, eg_value(battery), 1, 1, 0, 0 });

    for (int j = 0; j < 64; j++)
    {
        p.push_back({ &piece_bonus[ROOK][j], nullptr, mg_value(piece_bonus[ROOK][j]), 0, 1, 0, 0 });
        p.push_back({ &piece_bonus[ROOK][j], nullptr, eg_value(piece_bonus[ROOK][j]), 1, 1, 0, 0 });
    }
}

void init_queenTerms(vector<Parameter>& p)
{
    for (int j = 0; j < 64; j++)
    {
        p.push_back({ &piece_bonus[QUEEN][j], nullptr, mg_value(piece_bonus[QUEEN][j]), 0, 1, 0, 0 });
        p.push_back({ &piece_bonus[QUEEN][j], nullptr, eg_value(piece_bonus[QUEEN][j]), 1, 1, 0, 0 });
    }
}


void printParams()
{
    printf("Score piece_bonus[7][64] = {");
    for (int i = 0; i < 7; i++)
    {
        printf("\n{");
        for (int j = 0; j < 64; j++)
        {
            if (j % 8 == 0)
                printf("\n    ");
            printf("S(%d,%d)", mg_value(piece_bonus[i][j]), eg_value(piece_bonus[i][j]));
            if (j < 63)
                printf(", ");
        }
        printf("\n}");
        if (i < 6)
            printf(",");
    }
    printf("\n};\n\n");
    printf("Score isolated_penalty[2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("S(%d,%d)", mg_value(isolated_penalty[i]), eg_value(isolated_penalty[i]));
        if (i < 1)
            printf(", ");
    }
    printf("};\n");
    printf("Score isolated_penaltyAH[2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("S(%d,%d)", mg_value(isolated_penaltyAH[i]), eg_value(isolated_penaltyAH[i]));
        if (i < 1)
            printf(", ");
    }
    printf("};\n\n");
    printf("Score doubled_penalty[2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("S(%d,%d)", mg_value(doubled_penalty[i]), eg_value(doubled_penalty[i]));
        if (i < 1)
            printf(", ");
    }
    printf("};\n");
    printf("Score doubled_penalty_undefended[2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("S(%d,%d)", mg_value(doubled_penalty_undefended[i]), eg_value(doubled_penalty_undefended[i]));
        if (i < 1)
            printf(", ");
    }
    printf("};\n\n");
    printf("Score isolated_doubled_penalty[2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("S(%d,%d)", mg_value(isolated_doubled_penalty[i]), eg_value(isolated_doubled_penalty[i]));
        if (i < 1)
            printf(", ");
    }
    printf("};\n");
    printf("Score isolated_doubled_penaltyAH[2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("S(%d,%d)", mg_value(isolated_doubled_penaltyAH[i]), eg_value(isolated_doubled_penaltyAH[i]));
        if (i < 1)
            printf(", ");
    }
    printf("};\n\n");
    printf("Score backward_penalty[2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("S(%d,%d)", mg_value(backward_penalty[i]), eg_value(backward_penalty[i]));
        if (i < 1)
            printf(", ");
    }
    printf("};\n\n");
    printf("Score connected_bonus[2][2][8] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("\n{");
        for (int j = 0; j < 2; j++)
        {
            printf("\n    {");
            for (int k = 0; k < 8; k++)
            {
                printf("S(%d,%d)", mg_value(connected_bonus[i][j][k]), eg_value(connected_bonus[i][j][k]));
                if (k < 7)
                    printf(", ");
            }
            printf("}");
            if (j < 1)
                printf(",");
        }
        printf("\n}");
        if (i < 1)
            printf(",");
    }
    printf("\n};\n\n");
    printf("Score passedRankBonus[8] = {");
    for (int i = 0; i < 8; i++)
    {
        printf("S(%d,%d)", mg_value(passedRankBonus[i]), eg_value(passedRankBonus[i]));
        if (i < 7)
            printf(", ");
    }
    printf("};\n\n");
    printf("Score passedUnsafeBonus[2][8] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("\n    {");
        for (int j = 0; j < 8; j++)
        {
            printf("S(%d,%d)", mg_value(passedUnsafeBonus[i][j]), eg_value(passedUnsafeBonus[i][j]));
            if (j < 7)
                printf(", ");
        }
        printf("}");
        if (i < 1)
            printf(",");
    }
    printf("\n};\n\n");
    printf("Score passedBlockedBonus[2][8] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("\n    {");
        for (int j = 0; j < 8; j++)
        {
            printf("S(%d,%d)", mg_value(passedBlockedBonus[i][j]), eg_value(passedBlockedBonus[i][j]));
            if (j < 7)
                printf(", ");
        }
        printf("}");
        if (i < 1)
            printf(",");
    }
    printf("\n};\n\n");

    //printf("Score unstoppablePasser = S(%d,%d);\n\n", mg_value(unstoppablePasser), eg_value(unstoppablePasser));

    printf("Score knightMobilityBonus[9] = {");
    for (int i = 0; i < 9; i++)
    {
        if (i % 7 == 0)
            printf("\n    ");
        printf("S(%d,%d)", mg_value(knightMobilityBonus[i]), eg_value(knightMobilityBonus[i]));
        if (i < 8)
            printf(", ");
    }
    printf("};\n\n");
    printf("Score bishopMobilityBonus[14] = {");
    for (int i = 0; i < 14; i++)
    {
        if (i % 7 == 0)
            printf("\n    ");
        printf("S(%d,%d)", mg_value(bishopMobilityBonus[i]), eg_value(bishopMobilityBonus[i]));
        if (i < 13)
            printf(", ");
    }
    printf("};\n\n");
    printf("Score rookMobilityBonus[15] = {");
    for (int i = 0; i < 15; i++)
    {
        if (i % 7 == 0)
            printf("\n    ");
        printf("S(%d,%d)", mg_value(rookMobilityBonus[i]), eg_value(rookMobilityBonus[i]));
        if (i < 14)
            printf(", ");
    }
    printf("};\n\n");
    printf("Score queenMobilityBonus[28] = {");
    for (int i = 0; i < 28; i++)
    {
        if (i % 7 == 0)
            printf("\n    ");
        printf("S(%d,%d)", mg_value(queenMobilityBonus[i]), eg_value(queenMobilityBonus[i]));
        if (i < 27)
            printf(", ");
    }
    printf("};\n\n");

    printf("int PAWN_MG = %d;\n", PAWN_MG);
    printf("int PAWN_EG = %d;\n", PAWN_EG);
    printf("int KNIGHT_MG = %d;\n", KNIGHT_MG);
    printf("int KNIGHT_EG = %d;\n", KNIGHT_EG);
    printf("int BISHOP_MG = %d;\n", BISHOP_MG);
    printf("int BISHOP_EG = %d;\n", BISHOP_EG);
    printf("int ROOK_MG = %d;\n", ROOK_MG);
    printf("int ROOK_EG = %d;\n", ROOK_EG);
    printf("int QUEEN_MG = %d;\n", QUEEN_MG);
    printf("int QUEEN_EG = %d;\n", QUEEN_EG);
    printf("int KING_MG = %d;\n", KING_MG);
    printf("int KING_EG = %d;\n", KING_EG);

    printf("\n");

    printf("int attackerWeights[7] = {");
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
    printf("int kingflankAttack = %d;\n", kingflankAttack);
    printf("int kingringAttack = %d;\n", kingringAttack);
    printf("int kingpinnedPenalty = %d;\n", kingpinnedPenalty);
    printf("int kingweakPenalty = %d;\n", kingweakPenalty);
    printf("int pawnDistancePenalty = %d;\n", pawnDistancePenalty);
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

    printf("Score bishopPawns = S(%d,%d);\n", mg_value(bishopPawns), eg_value(bishopPawns));

    printf("Score rookFile[2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("S(%d,%d)", mg_value(rookFile[i]), eg_value(rookFile[i]));
        if (i < 1)
            printf(", ");
    }
    printf("};\n\n");

    printf("Score battery = S(%d,%d);\n", mg_value(battery), eg_value(battery));
    printf("Score kingProtector = S(%d,%d);\n", mg_value(kingProtector), eg_value(kingProtector));
    printf("Score outpostBonus[2][2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("{");
        for (int j = 0; j < 2; j++)
        {
            printf("S(%d,%d)", mg_value(outpostBonus[i][j]), eg_value(outpostBonus[i][j]));
            if (j < 1)
                printf(", ");
        }
        printf("}");
        if (i < 1)
            printf(", ");
    }
    printf("};\n");

    printf("Score reachableOutpost[2] = {");
    for (int i = 0; i < 2; i++)
    {
        printf("S(%d,%d)", mg_value(reachableOutpost[i]), eg_value(reachableOutpost[i]));
        if (i < 1)
            printf(", ");
    }
    printf("};\n");

    printf("Score minorThreat[7] = {");
    for (int i = 0; i < 7; i++)
    {
        printf("S(%d,%d)", mg_value(minorThreat[i]), eg_value(minorThreat[i]));
        if (i < 6)
            printf(", ");
    }
    printf("};\n\n");

    printf("Score rookThreat[7] = {");
    for (int i = 0; i < 7; i++)
    {
        printf("S(%d,%d)", mg_value(rookThreat[i]), eg_value(rookThreat[i]));
        if (i < 6)
            printf(", ");
    }
    printf("};\n\n");

    printf("Score kingThreat = S(%d,%d);\n", mg_value(kingThreat), eg_value(kingThreat));
    printf("Score kingMultipleThreat = S(%d,%d);\n", mg_value(kingMultipleThreat), eg_value(kingMultipleThreat));
    printf("Score pawnPushThreat = S(%d,%d);\n", mg_value(pawnPushThreat), eg_value(pawnPushThreat));
    printf("Score safePawnThreat = S(%d,%d);\n", mg_value(safePawnThreat), eg_value(safePawnThreat));
    printf("Score hangingPiece = S(%d,%d);\n\n", mg_value(hangingPiece), eg_value(hangingPiece));

    printf("Score bishopOpposerBonus = S(%d,%d);\n", mg_value(bishopOpposerBonus), eg_value(bishopOpposerBonus));
    printf("Score trappedBishopPenalty = S(%d,%d);\n", mg_value(trappedBishopPenalty), eg_value(trappedBishopPenalty));
    printf("Score veryTrappedBishopPenalty = S(%d,%d);\n\n", mg_value(veryTrappedBishopPenalty), eg_value(veryTrappedBishopPenalty));
    printf("Score defendedRookFile = S(%d,%d);\n\n", mg_value(defendedRookFile), eg_value(defendedRookFile));
    printf("Score rank7Rook = S(%d,%d);\n\n", mg_value(rank7Rook), eg_value(rank7Rook));

    printf("Score passedFriendlyDistance[8] = {");
    for (int i = 0; i < 8; i++)
    {
        printf("S(%d,%d)", mg_value(passedFriendlyDistance[i]), eg_value(passedFriendlyDistance[i]));
        if (i < 7)
            printf(", ");
    }
    printf("};\n\n");

    printf("Score passedEnemyDistance[8] = {");
    for (int i = 0; i < 8; i++)
    {
        printf("S(%d,%d)", mg_value(passedEnemyDistance[i]), eg_value(passedEnemyDistance[i]));
        if (i < 7)
            printf(", ");
    }
    printf("};\n\n");

    printf("Score tarraschRule_friendly[8] = {");
    for (int i = 0; i < 8; i++)
    {
        printf("S(%d,%d)", mg_value(tarraschRule_friendly[i]), eg_value(tarraschRule_friendly[i]));
        if (i < 7)
            printf(", ");
    }
    printf("};\n\n");

    printf("Score tarraschRule_enemy = S(%d,%d);\n\n\n", mg_value(tarraschRule_enemy), eg_value(tarraschRule_enemy));

    printf("int my_pieces[5][5] = {");
    for (int i = 0; i < 5; i++)
    {
        printf("\n    {");
        for (int j = 0; j < 5; j++)
        {
            if (j <= i)
                printf("%4d", my_pieces[i][j]);
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

    printf("int opponent_pieces[5][5] = {");
    for (int i = 0; i < 5; i++)
    {
        printf("\n    {");
        for (int j = 0; j < 5; j++)
        {
            if (j <= i)
                printf("%4d", opponent_pieces[i][j]);
            else
                printf("     ");

            if (j < i)
                printf(",");
        }
        printf("}");
        if (i < 4)
            printf(", ");
    }
    printf("\n};\n\n\n");

    printf("int bishop_pair = %d;\n\n", bishop_pair);
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

void testThings()
{
    printParams();
}

void initAll(vector <Parameter>& params)
{
    for (int i = 0; i < 2; i++)
    {
        params.push_back({ &reachableOutpost[i], nullptr, mg_value(reachableOutpost[i]), 1, 0, 0 });
        params.push_back({ &reachableOutpost[i], nullptr, eg_value(reachableOutpost[i]), 1, 1, 0, 0 });
    }
#if 0
    init_mobilityTerms(params);
    init_pieceWeights(params);
    init_imbalance_weights(params);
    init_passedTerms(params);
    init_kingTerms(params);
    init_threatTerms(params);
    init_pawnTerms(params);
    init_minorTerms(params);
    init_rookTerms(params);
    init_queenTerms(params);
#endif
}

void tuneParams(vector<Parameter>& params, double initial_best)
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
        double min_error = find_error(K);
        param.value = maxVal;
        set_parameter(param);
        double max_error = find_error(K);
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
                max_error = find_error(K);
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
                min_error = find_error(K);
            }

            printf("Parameter %u / %zu : V / E : min %d / %f, max %d / %f \n", i + 1, params.size(), minVal, min_error, maxVal, max_error);
        }
        param.errordelta = abs(bestError - param.errordelta);
        param.valuedelta = param.value - param.valuedelta;
    }
}

void tune()
{
    srand(time(0));
    read_file_data();
    vector <Parameter> params;

    initAll(params);

    double bestError = find_error(K);
    double newError;
    cout << "Training error: " << bestError << endl;

    for (int epoch = 0; epoch < 2; epoch++)
    {
        for (unsigned i = 0; i < params.size(); i++)
        {
            params[i].stability = 1;
        }
        random_shuffle(params.begin(), params.end());
        tuneParams(params, bestError); //get a good guess
        cout << "Beginning local search..." << endl;
        bool improving = true;
        bestError = find_error(K);
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

                newError = find_error(K);

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

                newError = find_error(K);

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

        printParams();
    }
}
