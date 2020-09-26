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
#include "pyrrhic/tbprobe.h"
#define STACKTRACE 0


// A shameless copy of Laser's parameters; it remains to be seen if these are any good
static const int SkipSize[16] = { 1, 1, 1, 2, 2, 2, 1, 3, 2, 2, 1, 3, 3, 2, 2, 1 };
static const int SkipDepths[16] = { 1, 2, 2, 4, 4, 3, 2, 5, 4, 3, 2, 6, 5, 4, 3, 2 };

int startTime = 0;
int globalState = 0;

int ideal_usage = 10000,
    max_usage = 10000,
    think_depth_limit = MAX_PLY;

constexpr int TIMER_GRANULARITY = 2047; // 2^n - 1

bool is_movetime = false;
bool is_depth = false;
bool is_infinite = false;

volatile bool is_timeout = false,
              is_pondering = false;

timeInfo globalLimits = {0, 0, 0, 0, 0, 0, 0, 0};
volatile bool ANALYSISMODE = false;
Move main_pv[MAX_PLY + 1];
int pvLength = 0;
Move ponderMove;

inline void getMyTimeLimit()
{
    is_movetime = globalLimits.timelimited;
    is_depth = globalLimits.depthlimited;
    is_infinite = globalLimits.infinite;
    is_timeout = false;
    think_depth_limit = globalLimits.depthlimited ? globalLimits.depthlimit : MAX_PLY;

    if (think_depth_limit == MAX_PLY)
    {
        timeTuple t = calculate_time();
        ideal_usage = t.optimum_time;
        max_usage = t.maximum_time;
    }
    else
    {
        ideal_usage = 10000;
        max_usage = 10000;
    }

}

void updateTime(bool failed_low, bool samePV, int init_time)
{
    if (failed_low)
    {
        ideal_usage *= 105 / 100;
    }
    if (samePV)
    {
        ideal_usage = max(95 * ideal_usage / 100 , init_time / 2);
        max_usage = min(ideal_usage * 6, max_usage);
    }
}

inline void historyScores(Position *pos, searchInfo *info, Move m, int16_t *history, int16_t *counterMoveHistory, int16_t *followUpHistory)
{
    int from = from_sq(m);
    int to = to_sq(m);
    PieceCode pc = pos->mailbox[from];

    *history = pos->my_thread->historyTable[pos->activeSide][from][to];
    *counterMoveHistory = (*(info-1)->counterMove_history)[pc][to];
    *followUpHistory = (*(info-2)->counterMove_history)[pc][to];
}


// TODO (drstrange767#1#): tune coefficients (0 vs nothing next)
int16_t historyBonus (int depth)
{
    return depth > 15 ? 0 : 32*depth*depth;
}

void add_history_bonus(int16_t *history, int16_t bonus)
{
    *history += bonus - (*history) * abs(bonus) / 16384; //cap at 16384, keeping scores well within 16 bits
}

inline bool isValid(Move m)
{
    return (from_sq(m) != to_sq(m));
}

void update_countermove_histories(searchInfo *info, PieceCode pc, int to, int16_t bonus)
{
    for (int i = 1; i<=2; i++)
    {
        if (isValid((info - i)->chosenMove))
        {
            add_history_bonus(&(*(info-i)->counterMove_history)[pc][to], bonus);
        }
    }
}

void save_killer(Position *pos, searchInfo *info, Move m, int16_t bonus)
{
    SearchThread *thread = pos->my_thread;

    if (m != info->killers[0])
    {
        info->killers[1] = info->killers[0];
        info->killers[0] = m;
    }

    Color side = pos->activeSide;
    int from = from_sq(m);
    int to = to_sq(m);
    PieceCode pc = pos->mailbox[from];

    /// Update butterfly table history
    add_history_bonus(&thread->historyTable[side][from][to], bonus);
    /// Update counter/followup history tables
    update_countermove_histories(info, pc, to, bonus);

    /// Update countermove
    if (isValid((info-1)->chosenMove))
    {
        int prevTo = to_sq((info-1)->chosenMove);
        thread->counterMoveTable[pos->mailbox[prevTo]][prevTo] = m;
    }
}

/// When a quiet move gets a beta cutoff, its histories are boosted
void update_heuristics(Position *pos, searchInfo *info, int bestScore, int beta, int depth, Move m, Move *quiets, int quiets_count)
{
// TODO (drstrange767#1#): try bigBonus value = depth == 15 ? bonus : historyBonus(depth + 1)
    int16_t bigBonus = historyBonus(depth + 1);
    int16_t bonus = bestScore > beta + PAWN_MG ? bigBonus : historyBonus(depth);

    if (!pos->isTactical(m)) /// If the best move is a quiet one, then reward it
    {
        save_killer(pos, info, m, bonus);

        /// All other quiet moves get a penalty
        for (int i = 0; i < quiets_count; i++)
        {
            int from = from_sq(quiets[i]);
            int to = to_sq(quiets[i]);
            add_history_bonus(&pos->my_thread->historyTable[pos->activeSide][from][to], -bonus);
            update_countermove_histories(info, pos->mailbox[from], to, -bonus);
        }
    }
}

// Check if the position is a forced draw; check if the position is a repetition
bool isDraw(Position *pos)
{
    int halfmoveClock = pos->halfmoveClock;
    pos->gameCycle = false;
    if (halfmoveClock > 3)
    {
        if (halfmoveClock > 99)
        {
            return true;
        }

        int repetitions = 0;
        int minIndex = max(pos->historyIndex - halfmoveClock, 0);
        int rootheight = pos->my_thread->rootheight;

        for (int i = pos->historyIndex - 4; i >= minIndex; i -= 2)
        {
            if (pos->key == pos->historyStack[i].key)
            {
                pos->gameCycle = true;
                /// If a repetition position is found *within* the search depth
                /// (as opposed to an old position from earlier in the game), declare draw
                /// without counting to 3 b/c opponent can't just choose a different move.

                if (i >= rootheight)
                {
                    return true;
                }

                repetitions++;

                if (repetitions == 2)
                {
                    return true;
                }
            }
        }
    }
// TODO (drstrange767#1#): verify material entry or just do a straight check ... or both???
    if (get_materialEntry(*pos)->isDrawn)
    {
        return true;
    }
    return false;
}

int qSearchDelta(const Position *pos)
{
    const int delta = pos->pawnOn7th() ? QUEEN_MG : PAWN_MG;
    Color enemy = ~pos->activeSide;
    return delta + ((pos->pieceBB[WQUEEN | enemy]) ? QUEEN_MG
                    : (pos->pieceBB[WROOK | enemy]) ? ROOK_MG
                    : (pos->pieceBB[WBISHOP | enemy]) ? BISHOP_MG
                    : (pos->pieceBB[WKNIGHT | enemy]) ? KNIGHT_MG
                                                        : PAWN_MG);
}

void check_time(SearchThread *thread) {
    if ((thread->nodes & TIMER_GRANULARITY) == TIMER_GRANULARITY)
    {
        if (time_passed() >= max_usage && !is_pondering && !is_depth && !is_infinite)
        {
            is_timeout = true;
        }
    }
}

int qSearch(SearchThread *thread, searchInfo *info, int depth, int alpha, const int beta)
{
    if (STACKTRACE) globalState = 0;
    Position *pos = &thread->position;
    thread->nodes++;

    int ply = info->ply;
    bool is_pv = beta - alpha > 1;

    if (is_pv)
        info->pvLen = 0;

    if (thread->thread_id == 0)
        check_time(thread);
    ///TIME CONTROL
    if (is_timeout)
    {
        longjmp(thread->jbuffer, 1);
    }

    if (isDraw(pos))
        return 1 - (thread->nodes & 2);

    bool in_check = (bool)(pos->checkBB);
    if (ply >= MAX_PLY)
    {
        return !in_check ? evaluate(*pos) : 0;
    }

    Move hashMove = MOVE_NONE;
    int hashScore = UNDEFINED;
    bool ttHit;
    TTEntry *tte = probeTT(pos->key, ttHit);
    uint8_t flag = tte_flag(tte);

    info->chosenMove = MOVE_NONE;
    (info+1)->ply = ply + 1;

    if (STACKTRACE) globalState = 1;

    if (ttHit)
    {
        hashMove = Move(tte->movecode);
        hashScore = tt_to_score(tte->value, ply);
        if (!is_pv &&
            (flag == FLAG_EXACT ||
             (flag == FLAG_BETA && hashScore >= beta) ||
             (flag == FLAG_ALPHA && hashScore <= alpha)))
             {
                 return hashScore;
             }
    }
    int bestScore;

    if (STACKTRACE) globalState = 2;

    if (!in_check) // stand pat
    {
        bool is_null = (info-1)->chosenMove == MOVE_NULL;
        if (ttHit && tte->static_eval != UNDEFINED)
            info->staticEval = bestScore = tte->static_eval;
        else if (is_null)
            info->staticEval = bestScore = tempo * 2 - (info-1)->staticEval;
        else
            info->staticEval = bestScore = evaluate(*pos);

        if (bestScore >= beta)
        {
            return bestScore;
        }


        //Delta pruning
        if (bestScore + qSearchDelta(pos) < alpha)
            return bestScore;

        if (is_pv && bestScore > alpha)
            alpha = bestScore;
    }
    else
    {
        bestScore = -VALUE_INF;
        info->staticEval = UNDEFINED;
    }

    if (STACKTRACE) globalState = 3;

    MoveGen movegen = MoveGen(pos, QUIESCENCE_SEARCH, hashMove, 0, depth);
    Move bestMove = MOVE_NONE;
    int moveCount = 0;
    Move m;


    while((m = movegen.next_move(info, depth)) != MOVE_NONE)
    {
        moveCount++;

        if ((!in_check) && !pos->see(m, 0))
            continue;

        int16_t history, counter, followup;

        historyScores(pos, info, m, &history, &counter, &followup);

        bool isTactical = (pos->isTactical(m));

        if (STACKTRACE) globalState = 4;

        if (moveCount > 1 && !isTactical && counter < 0 && followup < 0)
            continue;

        pos->do_move(m);
        thread->nodes++;
        info->chosenMove = m;
        int to = to_sq(m);
        PieceCode pc = pos->mailbox[to];
        info->counterMove_history = &thread->counterMove_history[pc][to];
        int score = -qSearch(thread, info+1, depth - 1, -beta, -alpha);
        pos->undo_move(m);

        if (score > bestScore)
        {
            bestScore = score;
            if (score > alpha)
            {
                if (is_pv && is_main_thread(pos))
                {
                    if (STACKTRACE) globalState = 5;
                    info->pv[0] = m;
                    info->pvLen = (info+1)->pvLen + 1;
                    memcpy(info->pv + 1, (info + 1)->pv, sizeof(Move)*(info+1)->pvLen);
                }

                bestMove = m;
                if (is_pv && score < beta)
                {
                    alpha = score;
                }
                else //Beta cutoff
                {
                    storeEntry(tte, pos->key, m, 0, score_to_tt(score, ply), info->staticEval, FLAG_BETA);
                    return score;
                }
            }
        }
    }

    if (STACKTRACE) globalState = 6;

    if (moveCount == 0 && in_check)
        return VALUE_MATED + ply;
    uint8_t return_flag = (is_pv && bestMove) ? FLAG_EXACT : FLAG_ALPHA;
    storeEntry(tte, pos->key, bestMove, 0, score_to_tt(bestScore, ply), info->staticEval, return_flag);

    return bestScore;
}

int alphaBeta(SearchThread *thread, searchInfo *info, int depth, int alpha, int beta)
{
    if (STACKTRACE) globalState = 7;
    if (depth < 1)
    {
        return qSearch(thread, info, 0, alpha, beta);
    }

    int ply = info->ply;
    bool is_pv = beta - alpha > 1;
    if (is_pv)
    {
        info->pvLen = 0;
    }

    bool isRoot = ply == 0;

    Position *pos = &thread->position;
    bool in_check = (bool)(pos->checkBB);

    if (thread->thread_id == 0)
        check_time(thread);
    if (!isRoot)
    {
        ///TIME CONTROL
        if (is_timeout)
        {
            longjmp(thread->jbuffer, 1);
        }

        if (ply >= MAX_PLY)
        {
            return (in_check) ? 0 : evaluate(*pos);
        }

        if (isDraw(pos))
        {
            return 1 - (thread->nodes & 2);
        }

        alpha = max(VALUE_MATED + ply, alpha);
        beta = min(VALUE_MATE - ply, beta);
        if (alpha >= beta)
        {
            return alpha;
        }
    }

    if (STACKTRACE) globalState = 8;

    if (is_pv && ply > thread->seldepth)
        thread->seldepth = ply;

    info->chosenMove = MOVE_NONE;
    Move excluded_move = info->excludedMove;
    U64 newHash = pos->key ^ U64(excluded_move << 16);

    (info+1)->killers[0] = (info+1)->killers[1] = MOVE_NONE;
    (info+1)->ply = ply + 1;

    Move hashMove = MOVE_NONE;
    int hashScore = UNDEFINED;

    /// Hash table probe
    bool ttHit;
    TTEntry *tte = probeTT(newHash, ttHit);
    uint8_t flag = tte_flag(tte);

    if (ttHit)
    {
        hashMove = tte->movecode;
        if (tte->depth >= depth)
        {
            hashScore = tt_to_score(tte->value, ply);
            if (!is_pv &&
                hashScore != UNDEFINED &&
                (flag == FLAG_EXACT ||
                 (flag == FLAG_BETA && hashScore >= beta) ||
                 (flag == FLAG_ALPHA && hashScore <= alpha)))
                 {
                    if (hashMove && !pos->isTactical(hashMove))
                    {
                        if (hashScore >= beta)
                        {
                            save_killer(pos, info, hashMove, historyBonus(depth));
                        }
                        else
                        {
                            int16_t penalty = -historyBonus(depth);
                            add_history_bonus(&thread->historyTable[pos->activeSide][from_sq(hashMove)][to_sq(hashMove)], penalty);
                            update_countermove_histories(info, pos->mailbox[from_sq(hashMove)], to_sq(hashMove), penalty);
                        }
                    }

                    return hashScore;
                 }
        }
    }

    if (STACKTRACE) globalState = 9;

    ///TB Probe
    unsigned TBResult = tablebasesProbeWDL(pos, depth, ply);
    if (TBResult != TB_RESULT_FAILED) {

        thread->tb_hits++;

        int tb_value = TBResult == TB_LOSS ? -TB_MATE + ply
              : TBResult == TB_WIN  ?  TB_MATE - ply : 0;

        uint8_t ttBound = TBResult == TB_LOSS ? FLAG_BETA
                : TBResult == TB_WIN  ? FLAG_ALPHA : FLAG_EXACT;

        if (    ttBound == FLAG_EXACT
            || (ttBound == FLAG_BETA && tb_value >= beta)
            || (ttBound == FLAG_ALPHA && tb_value <= alpha)) {

            storeEntry(tte, pos->key, MOVE_NONE, depth, score_to_tt(tb_value, ply), UNDEFINED, ttBound);
            return tb_value;
        }
    }

    /// Get a static eval

    bool is_null = (info-1)->chosenMove == MOVE_NULL;

    if (STACKTRACE) globalState = 10;

    if (!in_check)
    {
        if (ttHit && tte->static_eval != UNDEFINED)
        {
            info->staticEval = tte->static_eval;
        }
        else if (is_null)
        {
            info->staticEval = tempo * 2 - (info-1)->staticEval;
        }
        else
        {
            info->staticEval = evaluate(*pos);
        }

        if (pos->gameCycle)
            info->staticEval *= max(0, 100 - pos->halfmoveClock) / 100;
    }
    else
    {
        info->staticEval = UNDEFINED;
    }
// TODO (drstrange767#1#): tune razoring + futility margins / depths + add multiple razor levels

    if (STACKTRACE) globalState = 11;

    ///Razoring
    if (!in_check && !is_pv && depth < 3 && info->staticEval <= alpha - RazoringMarginByDepth[depth])
    {
        return qSearch(thread, info, 0, alpha, beta);
    }

    bool improving = !in_check && ply > 1 && (info->staticEval >= (info-2)->staticEval || (info-2)->staticEval == UNDEFINED);

    if (STACKTRACE) globalState = 12;

    ///Reverse Futility Pruning
    if (!in_check && !is_pv && depth < 9 && info->staticEval - 80 * depth >= (beta - improvementValue*improving) && pos->nonPawn[pos->activeSide])
    {
        return info->staticEval;
    }

    if (STACKTRACE) globalState = 13;

    ///Null Move
    if ( !in_check && !is_pv && thread->doNMP && !is_null &&
        excluded_move == MOVE_NONE && info->staticEval >= (beta - improvementValue*improving) && depth > 2 && pos->nonPawn[pos->activeSide] &&
        (!ttHit || !(flag & FLAG_ALPHA) || hashScore >= beta))
    {
// TODO (drstrange767#1#): tune R

        int R = 3 + depth / 4 + min((info->staticEval - beta) / PAWN_MG, 3);
        pos->do_null_move();
        info->chosenMove = MOVE_NULL;
        info->counterMove_history = &thread->counterMove_history[BLANK][0];
        int nullScore = -alphaBeta(thread, info+1, depth-R, -beta, -beta+1);
        pos->undo_null_move();

        if (nullScore >= beta)
        {
            if (nullScore >= MATE_IN_MAX_PLY)
                nullScore = beta;

// TODO (drstrange767#1#): tune depth
            return nullScore;
        }
    }

    if (STACKTRACE) globalState = 14;

    ///Probcut
    ///////////////////////////////////////////////////SECTION UNDER REVIEW//////////////////////////////////////////////////////////
    Move m;
    if (!in_check && !is_pv && depth > 4 && abs(beta) < MATE_IN_MAX_PLY && info->staticEval + qSearchDelta(pos) >= beta + ProbCutMargin)
    {
        int rbeta = min(beta + ProbCutMargin, int(VALUE_MATE));
        MoveGen movegen = MoveGen(pos, PROBCUT_SEARCH, hashMove, rbeta - info->staticEval, depth);
        while((m = movegen.next_move(info, depth)) != MOVE_NONE)
        {
            if (m != excluded_move)
            {
                pos->do_move(m);
                info->chosenMove = m;
                int to = to_sq(m);
                PieceCode pc = pos->mailbox[to];
                info->counterMove_history = &thread->counterMove_history[pc][to];

                int value = -qSearch(thread, info+1, 0, -rbeta, -rbeta + 1);

                if (value >= rbeta)
                    value = -alphaBeta(thread, info+1, depth - 4, -rbeta, -rbeta+1);

                pos->undo_move(m);

                if (value >= rbeta)
                    return value;
            }
        }
    }
    ///////////////////////////////////////////////////SECTION UNDER REVIEW//////////////////////////////////////////////////////////

    MoveGen movegen = MoveGen(pos, NORMAL_SEARCH, hashMove, 0, depth);
    Move bestMove = MOVE_NONE;
    int bestScore = -VALUE_INF;

    Move quiets[64];
    int quiets_count = 0;
    int num_moves = 0;

    bool skipQuiets = false;

    if (STACKTRACE) globalState = 15;

    info->hadSingularExtension = false;

    while((m = movegen.next_move(info, skipQuiets)) != MOVE_NONE)
    {
        if (m == excluded_move)
            continue;

        num_moves++;

        bool givesCheck = pos->givesCheck(m);
        bool isTactical = (pos->isTactical(m));
        int extension = 0;

        int16_t history, counter, followup;

        historyScores(pos, info, m, &history, &counter, &followup);

        ///////////////////////////////////////////////////SECTION UNDER REVIEW//////////////////////////////////////////////////////////

        ///Low-depth Pruning of Quiet Moves. As long as we're not mated, go ahead and prune bad moves
        if (!isRoot && pos->nonPawn[pos->activeSide] && bestScore > MATED_IN_MAX_PLY)
        {
            if (!givesCheck && !isTactical)
            {
                if (!isRoot && depth <= 8 && num_moves >= futility_move_counts[improving][depth])
                    skipQuiets = true;

                if (!isRoot && depth <= 8 && !in_check && info->staticEval + PAWN_MG * depth <= alpha &&
                    history + counter + followup < futility_history_limit[improving])
                    skipQuiets = true;

                if (!isRoot && depth <= 5 && counter < 0 && followup < 0)
                    continue;

                if (depth < 9 && !pos->see(m, -10*depth*depth))
                    continue;
            }
            else if (movegen.state > TACTICAL_STATE && !pos->see(m, -PAWN_EG * depth)) // is a bad tactical move with very low SEE
                continue;
        }

        if (STACKTRACE) globalState = 16;

        ///Singular extension search
        if (depth >= singularDepth && m == hashMove && !isRoot && excluded_move == MOVE_NONE
            && abs(hashScore) < WON_ENDGAME && (flag & FLAG_BETA) && tte->depth >= depth - 2 && !pos->gameCycle) // Don't singularly extend if we've been here before (what's the point?)
        {
            int singularBeta = hashScore - 2 * depth;
            int halfDepth = depth / 2;
            info->excludedMove = m;
            int singularValue = alphaBeta(thread, info, halfDepth, singularBeta - 1, singularBeta);
            info->excludedMove = MOVE_NONE;

            if (singularValue < singularBeta)
            {
                extension = 1;
                info->hadSingularExtension = true;
            }

            else if (singularBeta >= beta)
                return singularBeta;

        }
        ///////////////////////////////////////////////////SECTION UNDER REVIEW//////////////////////////////////////////////////////////
        ///Extensions
// TODO (drstrange767#1#): if (depth < 6?) + discoveredCheck // INSIST ON PAWN PUSH EXTENSION

        else
        {
            if (givesCheck && (pos->see(m, 0)))
                extension = 1;
            else if (depth < 3 && pos->advanced_pawn_push(m))
                extension = 1;
        }
// TODO (drstrange767#1#): if (gameCycle && depth < 5 || is_pv) extend ??

        pos->do_move(m);
        if (isRoot && time_passed() > 3000)
            cout << "info depth " << depth << " currmove " << move_to_str(m) << " currmovenumber " << num_moves << endl;
        thread->nodes++;
        info->chosenMove = m;
        int to = to_sq(m);
        PieceCode pc = pos->mailbox[to];
        info->counterMove_history = &thread->counterMove_history[pc][to];

        /// Late Move Reduction

        int newDepth = depth - 1 + extension;
        int score;


        ///////////////////////////////////////////////////SECTION UNDER REVIEW//////////////////////////////////////////////////////////

        if (STACKTRACE) globalState = 17;
        if (is_pv && num_moves == 1)
        {
            score = -alphaBeta(thread, info+1, newDepth, -beta, -alpha);
        }
        else
        {
            if (STACKTRACE) globalState = 18;
            int reduction = 0;
            if (depth >= 3 && num_moves > 1 + isRoot && (!isTactical || pieceValues[EG][pos->capturedPiece] + info->staticEval <= alpha))
            {
                reduction = lmr(improving, depth, num_moves);

                reduction -= 2*(is_pv);

                if (!isTactical)
                {
                    if (m == info->killers[0] || m == info->killers[1] || m == movegen.counterMove)
                        reduction --;
                    if (hashMove && pos->isCapture(hashMove))
                        reduction += 1 + info->hadSingularExtension;
                }

                int quietScore = history + counter + followup;
                reduction -= max(-2, min(quietScore / 8000, 2));

                reduction = min(newDepth - 1, max(reduction, 0));
            }

            score = -alphaBeta(thread, info+1, newDepth - reduction, -alpha-1, -alpha);

            if (reduction > 0 && score > alpha)
                score = -alphaBeta(thread, info+1, newDepth, -alpha-1, -alpha);

            if (is_pv && score > alpha && score < beta)
                score = -alphaBeta(thread, info+1, newDepth, -beta, -alpha);
        }

        ///////////////////////////////////////////////////SECTION UNDER REVIEW//////////////////////////////////////////////////////////

        pos->undo_move(m);

        if (score > bestScore)
        {
            bestScore = score;
            if (score > alpha)
            {
                if (is_pv && is_main_thread(pos))
                {
                    if (STACKTRACE) globalState = 19;
                    info->pv[0] = m;
                    info->pvLen = (info+1)->pvLen + 1;
                    memcpy(info->pv + 1, (info + 1)->pv, sizeof(Move)*(info+1)->pvLen);
                }
                bestMove = m;
                if (is_pv && score < beta)
                {
                    alpha = score;
                }
                else
                {
                    break;
                }
            }
        }

        if (m != bestMove && !isTactical && quiets_count < 64)
        {
            quiets[quiets_count++] = m;
        }
    }

    if (STACKTRACE) globalState = 20;

    if (num_moves == 0)
    {
        bestScore = excluded_move != MOVE_NONE ? alpha : in_check ? VALUE_MATED + ply : 0;
    }
    else if (bestMove)
    {
        update_heuristics(pos, info, bestScore, beta, depth, bestMove, quiets, quiets_count);
    }

    if (excluded_move == MOVE_NONE)
    {
        storeEntry(tte, pos->key, bestMove, depth, score_to_tt(bestScore, ply), info->staticEval, (is_pv && bestMove) ? FLAG_EXACT : FLAG_ALPHA);
    }

    return bestScore;
}

void printInfo(Position *pos, searchInfo *info, int depth, int score, int alpha, int beta)
{
    int time_taken = time_passed();
    bool printPV = score > alpha && score < beta;
    U64 tb_hits = sum_tb_hits();
    U64 nodes = sum_nodes();
    const char *bound = score >= beta ? " lowerbound" : score <= alpha ? " upperbound" : "";
    const char *scoreType = abs(score) >= MATE_IN_MAX_PLY ? "mate" : "cp";
    score = score <= MATED_IN_MAX_PLY ? ((VALUE_MATED - score) / 2) : score >= MATE_IN_MAX_PLY ? ((VALUE_MATE - score + 1) / 2) : score * 100 / PAWN_EG;

    printf("info depth %d seldepth %d multipv 1 score %s %d%s time %d nodes %zu nps %zu tbhits %zu hashfull %d pv ",
           depth, pos->my_thread->seldepth+1, scoreType, score, bound, time_taken, nodes, nodes*1000/(time_taken+1), tb_hits, hashfull());

    if (printPV) {
        for (int i = 0; i < info->pvLen; i++)
            cout << move_to_str(info->pv[i]) << " ";
    } else {
        cout << move_to_str(main_pv[0]);
    }
    cout << endl;
}

void *aspiration_thread(void *t)
{
    SearchThread *thread = (SearchThread *)t;
    Position *pos = &thread->position;
    searchInfo *info = &thread->ss[2];
    bool is_main = is_main_thread(pos);

    Move lastPV = MOVE_NONE;

    int previous = VALUE_MATED;
    int score = VALUE_MATED;
    int init_ideal_usage = ideal_usage;
    int depth = 0;
    int actualSearchDepth = 0;

    while (++depth <= think_depth_limit)
    {
        thread->seldepth = 0;
        int aspiration = ASPIRATION_INIT;
        int alpha = VALUE_MATED;
        int beta = VALUE_MATE;
        actualSearchDepth = depth;

        if (actualSearchDepth >= 5)
        {
            alpha = max(previous - aspiration, int(VALUE_MATED));
            beta = min(previous + aspiration, int(VALUE_MATE));
        }

        bool failed_low = false;

// TODO (drstrange767#1#): test removing this

        if ( thread->thread_id != 0 )
        {
            int cycle = thread->thread_id % 16;
            if ((actualSearchDepth + cycle) % SkipDepths[cycle] == 0)
                actualSearchDepth += SkipSize[cycle];
        }
        if (setjmp(thread->jbuffer)) break;

        while (true)
        {
            score = alphaBeta(thread, info, actualSearchDepth, alpha, beta);

            if (is_timeout)
            {
                break;
            }

            if (is_main && (score <= alpha || score >= beta) && depth > 12)
            {
                printInfo(pos, info, depth, score, alpha, beta);
            }

            if (score <= alpha)
            {
                //beta = (alpha + beta) / 2;
                alpha = max(score - aspiration, int(VALUE_MATED));
                failed_low = true;
                actualSearchDepth = depth;
            }
            else if (score >= beta)
            {
                beta = min(score + aspiration, int(VALUE_MATE));
                actualSearchDepth--;
            }
            else
            {
                if (is_main)
                {
                    pvLength = info->pvLen;
                    memcpy(main_pv, info->pv, sizeof(Move)*pvLength);
                    printInfo(pos, info, depth, score, alpha, beta);
                }
                break;
            }

            aspiration +=  aspiration == ASPIRATION_INIT ? aspiration * 2 / 3 : aspiration / 2;
        }

        previous = score;

        if (is_timeout)
        {
            break;
        }

        if (!is_main)
        {
            continue;
        }

        if (time_passed() > ideal_usage && !is_pondering && !is_depth && !is_infinite)
        {
            is_timeout = true;
            break;
        }

        if ( depth >= 6 && !is_movetime)
        {
            updateTime(failed_low, lastPV == main_pv[0], init_ideal_usage);
        }

        lastPV = main_pv[0];
    }
    return NULL;
}

void* think (void *p)
{
    Position *pos = (Position*)p;
    getMyTimeLimit();
    start_search();

    startTime = getRealTime();
    pvLength = 0;
    if (STACKTRACE) globalState = -1;

    /// Probe book and TB
    Move probeMove = book.probe(*pos);
    if (probeMove != MOVE_NONE)
    {
        cout << "info time " << time_passed() << endl;
        cout << "info book move is " << move_to_str(probeMove) << endl;
        cout << "bestmove " << move_to_str(probeMove) << endl;
        return NULL;
    }

    if (tablebasesProbeDTZ(pos, &probeMove, &ponderMove))
    {
        cout << "info time " << time_passed() << endl;
        cout << "info TB move is " << move_to_str(probeMove) << endl;
        cout << "bestmove " << move_to_str(probeMove) << endl;
        return NULL;
    }

    thread *threads = new thread[num_threads];
    initialize_nodes();

    for (int i = 0; i < num_threads; i++)
    {
        threads[i] = thread(aspiration_thread, get_thread(i));
    }

    for (int i = 0; i < num_threads; i++)
    {
        threads[i].join();
    }

    delete[] threads;

    while (is_pondering) {}

    #if STACKTRACE
    if (pvLength == 0)
    {
        cout << "DISASTER at global state " << globalState << endl;
    }
    #endif

    ponderMove = pvLength > 1 ? main_pv[1] : MOVE_NONE;

    cout << "info time " << time_passed() << endl;
    cout << "bestmove " << move_to_str(main_pv[0]);

    if ((to_sq(ponderMove) != from_sq(ponderMove)))
    {
        cout << " ponder " << move_to_str(ponderMove);
    }

    cout<<endl;
    fflush(stdout);
    return NULL;
}
