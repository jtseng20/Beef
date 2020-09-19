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



int startTime = 0;

int ideal_usage = 10000,
    max_usage = 10000,
    think_depth_limit = MAX_PLY;

bool is_movetime = false;
bool is_depth = false;
bool is_infinite = false;

volatile bool is_timeout = false,
              quit_application = false,
              is_searching = false,
              is_pondering = false;

timeInfo globalLimits = {0, 0, 0, 0, 0, 0, 0, 0};
volatile bool ANALYSISMODE = false;
Move bestMove, ponderMove;

struct PV
{
    uint8_t length;
    Move moves[MAX_PLY + 1];
};

PV main_pv;

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

void updateTime(bool failed_low, bool samePV, int initIdeal)
{
    if (failed_low)
    {
        ideal_usage = min(ideal_usage * 105 / 100, max_usage);
    }

    if (samePV)
    {
        ideal_usage = max(ideal_usage * 95 / 100, initIdeal / 2);
        max_usage = min(6 * ideal_usage, max_usage);
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
    if (get_materialEntry(*pos)->endgame_type == DRAW_ENDGAME)
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

void check_time(Position *p) {
    if (!is_main_thread(p)) {
        return;
    }

    if ((p->my_thread->nodes & 1023) == 1023) {
        if (time_passed() >= max_usage && !is_pondering && !is_depth && !is_infinite) {
            is_timeout = true;
        }
    }
}

int qSearch(SearchThread *thread, searchInfo *info, int depth, int alpha, const int beta, PV *pv)
{
    Position *pos = &thread->position;
    thread->nodes++;

    int ply = info->ply;
    bool is_pv = beta - alpha > 1;
    PV newPV = {0};

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

    MoveGen movegen = MoveGen(pos, QUIESCENCE_SEARCH, hashMove, 0, depth);
    Move bestMove = MOVE_NONE;
    int moveCount = 0;
    Move m;


    while((m = movegen.next_move(info, depth)) != MOVE_NONE)
    {
        moveCount++;

        if ((!in_check) && !pos->see(m, 0))
            continue;

        pos->do_move(m);
        thread->nodes++;
        info->chosenMove = m;
        int to = to_sq(m);
        PieceCode pc = pos->mailbox[to];
        info->counterMove_history = &thread->counterMove_history[pc][to];
        int score = -qSearch(thread, info+1, depth - 1, -beta, -alpha, &newPV);
        pos->undo_move(m);

        if (score > bestScore)
        {
            bestScore = score;
            if (score > alpha)
            {
                pv->length = newPV.length + 1;
                pv->moves[0] = m;
                memcpy(pv->moves + 1, newPV.moves, sizeof(Move)*newPV.length);

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

    if (moveCount == 0 && in_check)
        return VALUE_MATED + ply;
    uint8_t return_flag = (is_pv && bestMove) ? FLAG_EXACT : FLAG_ALPHA;
    storeEntry(tte, pos->key, bestMove, 0, score_to_tt(bestScore, ply), info->staticEval, return_flag);

    return bestScore;
}

int alphaBeta(SearchThread *thread, searchInfo *info, int depth, int alpha, int beta, PV *pv)
{
    if (depth < 1)
    {
        return qSearch(thread, info, 0, alpha, beta, pv);
    }

    int ply = info->ply;
    bool is_pv = beta - alpha > 1;

    bool isRoot = ply == 0;
    PV newPV = {0};
    pv->length = 0;

    Position *pos = &thread->position;
    bool in_check = (bool)(pos->checkBB);

    check_time(pos);
    if (!isRoot)
    {
        ///TIME CONTROL
        if (is_timeout)
        {
            return TIMEOUT;
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

    if (is_pv && ply > thread->seldepth)
        thread->seldepth = ply;

    info->chosenMove = MOVE_NONE;

    (info+1)->killers[0] = (info+1)->killers[1] = MOVE_NONE;
    (info+1)->ply = ply + 1;

    Move hashMove = MOVE_NONE;
    int hashScore = UNDEFINED;

    /// Hash table probe
    bool ttHit;
    TTEntry *tte = probeTT(pos->key, ttHit);
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
                    return hashScore;
                 }
        }
    }

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

    ///Razoring
    if (!in_check && !is_pv && depth < 3 && info->staticEval <= alpha - RazoringMarginByDepth[depth])
    {
        return qSearch(thread, info, 0, alpha, beta, pv);
    }

    bool improving = !in_check && ply > 1 && (info->staticEval >= (info-2)->staticEval || (info-2)->staticEval == UNDEFINED);

    ///Reverse Futility Pruning
    if (!in_check && !is_pv && depth < 9 && info->staticEval - 80 * depth >= (beta - improvementValue*improving) && pos->nonPawn[pos->activeSide])
    {
        return info->staticEval;
    }

    ///Null Move
    if ( !in_check && !is_pv && thread->doNMP && !is_null && info->staticEval >= (beta - improvementValue*improving) && depth > 2 && pos->nonPawn[pos->activeSide] &&
        (!ttHit || !(flag & FLAG_ALPHA) || hashScore >= beta))
    {
        int R = 4 + depth / 4;
        pos->do_null_move();
        info->chosenMove = MOVE_NULL;
        info->counterMove_history = &thread->counterMove_history[BLANK][0];
        int nullScore = -alphaBeta(thread, info+1, depth-R, -beta, -beta+1, &newPV);
        pos->undo_null_move();

        if (nullScore >= beta)
        {
            if (nullScore >= MATE_IN_MAX_PLY)
                nullScore = beta;

            return nullScore;
        }
    }

    MoveGen movegen = MoveGen(pos, NORMAL_SEARCH, hashMove, 0, depth);
    Move bestMove = MOVE_NONE;
    int bestScore = -VALUE_INF;

    Move quiets[64];
    int quiets_count = 0;
    int num_moves = 0;
    Move m;

    bool skipQuiets = false;

    while((m = movegen.next_move(info, skipQuiets)) != MOVE_NONE)
    {

        num_moves++;

        bool givesCheck = pos->givesCheck(m);
        bool isTactical = (pos->isTactical(m));
        int extension = 0;

        int16_t history, counter, followup;

        historyScores(pos, info, m, &history, &counter, &followup);

        ///Low-depth Pruning of Quiet Moves. As long as we're not mated, go ahead and prune bad moves
        if (!isRoot && pos->nonPawn[pos->activeSide] && bestScore > MATED_IN_MAX_PLY)
        {
            if (!givesCheck && !isTactical)
            {
                if (!isRoot && depth <= 8 && num_moves >= futility_move_counts[improving][depth])
                    skipQuiets = true;

                if (!isRoot && depth <= 5 && counter < 0 && followup < 0)
                    continue;
            }

            if (!pos->see(m, -PAWN_EG * depth)) // is a bad tactical move with very low SEE
                continue;
        }

        if (givesCheck && (pos->see(m, 0)))
            extension = 1;

        pos->do_move(m);
        thread->nodes++;
        info->chosenMove = m;
        int to = to_sq(m);
        PieceCode pc = pos->mailbox[to];
        info->counterMove_history = &thread->counterMove_history[pc][to];

        /// Late Move Reduction

        int newDepth = depth - 1 + extension;
        int score;

        if (is_pv && num_moves == 1)
        {
            score = -alphaBeta(thread, info+1, newDepth, -beta, -alpha, &newPV);
        }
        else
        {
            int reduction = 0;
            if (depth >= 3 && num_moves > 1 + isRoot)
            {
                reduction = lmr(improving, depth, num_moves);

                reduction -= 2*(is_pv);

                if (!isTactical)
                {
                    reduction ++;

                    if (m == info->killers[0] || m == info->killers[1] || m == movegen.counterMove)
                        reduction --;
                }

                int quietScore = history + counter + followup;
                reduction -= max(-2, min(quietScore / 8000, 2));

                reduction = min(newDepth - 1, max(reduction, 0));
            }

            score = -alphaBeta(thread, info+1, newDepth - reduction, -alpha-1, -alpha, &newPV);

            if (reduction > 0 && score > alpha)
                score = -alphaBeta(thread, info+1, newDepth, -alpha-1, -alpha, &newPV);

            if (is_pv && score > alpha && score < beta)
                score = -alphaBeta(thread, info+1, newDepth, -beta, -alpha, &newPV);
        }

        pos->undo_move(m);

        if (score > bestScore)
        {
            bestScore = score;
            bestMove = m;
            if (score > alpha)
            {
                alpha = score;
                pv->length = newPV.length + 1;
                pv->moves[0] = m;
                memcpy(pv->moves + 1, newPV.moves, sizeof(Move) * newPV.length);

                if (alpha >= beta)
                    break;
            }
        }

        if (m != bestMove && !isTactical && quiets_count < 64)
        {
            quiets[quiets_count++] = m;
        }
    }

    if (num_moves == 0)
    {
        bestScore = in_check ? VALUE_MATED + ply : 0;
    }
    else if (bestMove)
    {
        update_heuristics(pos, info, bestScore, beta, depth, bestMove, quiets, quiets_count);
    }

    storeEntry(tte, pos->key, bestMove, depth, score_to_tt(bestScore, ply), info->staticEval, (is_pv && bestMove) ? FLAG_EXACT : FLAG_ALPHA);

    return bestScore;
}

void printInfo(Position *pos, searchInfo *info, int depth, int score, int alpha, int beta)
{
    int time_taken = time_passed();
    bool printPV = true;
    U64 tb_hits = sum_tb_hits();
    std::cout << "info depth " << depth << " seldepth " << pos->my_thread->seldepth + 1 <<" multipv 1 ";
    std::cout << "tbhits " << tb_hits << " score ";

    if (score <= MATED_IN_MAX_PLY) {
        std::cout << "mate " << ((VALUE_MATED - score) / 2);
    } else if (score >= MATE_IN_MAX_PLY) {
        std::cout << "mate " << ((VALUE_MATE - score + 1) / 2);
    } else {
        std::cout << "cp " << score * 100 / PAWN_EG;
    }

    if (score <= alpha)
    {
        printPV = false;
        std::cout << " upperbound";
    }
    else if (score >= beta)
    {
        printPV = false;
        std::cout << " lowerbound";
    }

    std::cout << " hashfull " << hashfull();

    uint64_t nodes = sum_nodes();
    std::cout << " nodes " << nodes <<  " nps " << nodes*1000/(time_taken+1) << " time " << time_taken << " pv ";
    if (printPV) {
        for (int i = 0; i < main_pv.length; i++)
            cout << move_to_str(main_pv.moves[i]) << " ";
    } else {
        std::cout << move_to_str(main_pv.moves[0]);
    }
    std::cout << std::endl;
}

void *aspiration_thread(void *t)
{
    SearchThread *thread = (SearchThread *)t;
    Position *pos = &thread->position;
    searchInfo *info = &thread->ss[2];
    bool is_main = is_main_thread(pos);

    int previous = VALUE_MATED;
    int score = VALUE_MATED;
    int init_ideal_usage = ideal_usage;
    int depth = 0;
    Move lastPV = MOVE_NONE;

    while (++depth <= think_depth_limit)
    {
        thread->seldepth = 0;
        int aspiration = ASPIRATION_INIT;
        int alpha = VALUE_MATED;
        int beta = VALUE_MATE;

        if (depth >= 5)
        {
            alpha = max(previous - aspiration, int(VALUE_MATED));
            beta = min(previous + aspiration, int(VALUE_MATE));
        }

        bool failed_low = false;

        while (true)
        {
            score = alphaBeta(thread, info, depth, alpha, beta, &main_pv);

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
            }
            else if (score >= beta)
            {
                beta = min(score + aspiration, int(VALUE_MATE));
            }
            else
            {
                bestMove = main_pv.moves[0];
                ponderMove = main_pv.length > 1 ? main_pv.moves[1] : MOVE_NONE;
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

        printInfo(pos, info, depth, score, alpha, beta);

        updateTime(failed_low, lastPV == bestMove, init_ideal_usage);
        lastPV = bestMove;

        if (time_passed() > ideal_usage && !is_pondering && !is_depth && !is_infinite)
        {
            is_timeout = true;
            break;
        }
    }
    return NULL;
}

void* think (void *p)
{
    Position *pos = (Position*)p;
    getMyTimeLimit();
    start_search();

    startTime = getRealTime();

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

    std::thread *threads = new std::thread[num_threads];
    initialize_nodes();

    is_searching = true;

    for (int i = 0; i < num_threads; i++)
    {
        threads[i] = std::thread(aspiration_thread, get_thread(i));
    }

    for (int i = 0; i < num_threads; i++)
    {
        threads[i].join();
    }

    delete[] threads;

    while (is_pondering) {}

    cout << "info time " << time_passed() << endl;
    cout << "bestmove " << move_to_str(bestMove);

    if (to_sq(ponderMove) != from_sq(ponderMove)) {
        cout << " ponder " << move_to_str(ponderMove);
    }

    cout<<endl;
    fflush(stdout);
    if (quit_application)
        exit(EXIT_SUCCESS);

    is_searching = false;
    return NULL;
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
        std::cout << "\nPosition [" << (i + 1) << "|36]\n" << std::endl;
        Position *p = import_fen(benchmarks[i].c_str(), 0);
        think(p);
        nodes += main_thread.nodes;

        clear_tt();
    }

    int time_taken = getRealTime() - benchStart;

    std::cout << "\n------------------------\n";
    std::cout << "Time  : " << time_taken << std::endl;
    std::cout << "Nodes : " << nodes << std::endl;
    std::cout << "NPS   : " << nodes * 1000 / (time_taken + 1) << std::endl;
}
