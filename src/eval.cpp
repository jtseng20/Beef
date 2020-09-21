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

Score trace_scores[TERM_NB][2];
tunerTrace Trace;

constexpr int lazyThreshold = 1300;

int Position::scaleFactor() const
{
    if (ONLYONE(pieceBB[WBISHOP]) && ONLYONE(pieceBB[BBISHOP]) && ONLYONE((pieceBB[WBISHOP] | pieceBB[BBISHOP]) & DarkSquares))
    {
        if (nonPawn[0] == BISHOP_MG && nonPawn[1] == BISHOP_MG)
            return SCALE_OCB;
        else
            return SCALE_OCB_PIECES;
    }
    Color strongerSide = (eg_value(psqt_score) > 0) ? WHITE : BLACK;
    if (!pieceBB[make_piece(strongerSide, PAWN)] && nonPawn[strongerSide] <= nonPawn[~strongerSide] + BISHOP_MG)
    {
        return nonPawn[strongerSide] < ROOK_MG ? SCALE_NOPAWNS : SCALE_HARDTOWIN;
    }
    if (ONLYONE(pieceBB[make_piece(strongerSide, PAWN)]) && nonPawn[strongerSide] <= nonPawn[~strongerSide] + BISHOP_MG)
    {
        return SCALE_ONEPAWN;
    }
    return SCALE_NORMAL;
}


template <Tracing T> void Eval<T>::pre_eval()
{

    //prepare threat squares and get king attackers information
    double_targets[0] = double_targets[1] = 0ULL;
    king_attackers_count[0] = king_attackers_count[1] = king_attacks_count[0] = king_attacks_count[1] = king_attackers_weight[0] = king_attackers_weight[1] = 0;
    kingRings[0] = kingRing[pos.kingpos[0]];
    kingRings[1] = kingRing[pos.kingpos[1]];

    U64 real_occ = pos.occupiedBB[0] | pos.occupiedBB[1];

    attackedSquares[WPAWN] = PAWNATTACKS(WHITE, pos.pieceBB[WPAWN]);
    attackedSquares[BPAWN] = PAWNATTACKS(BLACK, pos.pieceBB[BPAWN]);
    attackedSquares[WKING] = PseudoAttacks[KING][pos.kingpos[WHITE]];
    attackedSquares[BKING] = PseudoAttacks[KING][pos.kingpos[BLACK]];

    //double pawn targets + pawn/king targets
    double_targets[WHITE] = ((pos.pieceBB[WPAWN] & ~FileABB) << 7) & ((pos.pieceBB[WPAWN] & ~FileHBB) << 9);
    double_targets[BLACK] = ((pos.pieceBB[BPAWN] & ~FileHBB) >> 7) & ((pos.pieceBB[BPAWN] & ~FileABB) >> 9);
    double_targets[WHITE] |= attackedSquares[WKING] & attackedSquares[WPAWN];
    double_targets[BLACK] |= attackedSquares[BKING] & attackedSquares[BPAWN];

    attackedSquares[WHITE] = attackedSquares[WKING] | attackedSquares[WPAWN];
    attackedSquares[BLACK] = attackedSquares[BKING] | attackedSquares[BPAWN];

    //prepare mobility area
    U64 low_ranks = Rank2BB | Rank3BB;
    U64 blocked_pawns = pos.pieceBB[WPAWN] & (PAWNPUSH(BLACK, real_occ) | low_ranks);
    mobility_area[WHITE] = ~(blocked_pawns | pos.pieceBB[WKING] | attackedSquares[BPAWN]);

    low_ranks = Rank6BB | Rank7BB;
    blocked_pawns = pos.pieceBB[BPAWN] & (PAWNPUSH(WHITE, real_occ) | low_ranks);
    mobility_area[BLACK] = ~(blocked_pawns | pos.pieceBB[BKING] | attackedSquares[WPAWN]);
}

template <Tracing T> template <Color side> int Eval<T>::pawn_shelter_score(int sq)
{
    int out = 0;
    int middle_file = max(1, min(6, FILE(sq)));
    U64 myPawns = pos.pieceBB[make_piece(side, PAWN)];
    U64 opponentPawns = pos.pieceBB[make_piece(~side, PAWN)];

    for (int file = middle_file - 1; file <= middle_file + 1; file++)
    {
        U64 pawns = myPawns & FileBB[file];
        int defendingRank = pawns ? RRANK((side ? MSB(pawns) : LSB(pawns)), side) : 0;
        pawns = opponentPawns & FileBB[file];
        int stormingRank = pawns ? RRANK((side ? MSB(pawns) : LSB(pawns)), side) : 0;

        int f = min(file, 7 - file);
        out += kingShield[f][defendingRank];
        bool blocked = (defendingRank != 0) && defendingRank == stormingRank - 1;
        out -= (blocked) ? pawnStormBlocked[f][stormingRank] : pawnStormFree[f][stormingRank];
        #if TUNERTRACE and TUNESAFETY
        Trace.kingShield[side][f][defendingRank]++;
        if (blocked)
            Trace.pawnStormBlocked[side][f][stormingRank]--;
        else
            Trace.pawnStormFree[side][f][stormingRank]--;
        #endif // TUNERTRACE
    }

    return out;
}

template <Tracing T> template <Color side> void Eval<T>::pawn_shelter_castling()
{
    pawntte->pawnShelter[side] = pawn_shelter_score<side>(pos.kingpos[side]);
    if (pos.castleRights & KINGSIDE_CASTLE_MASKS[side])
        pawntte->pawnShelter[side] = max(pawntte->pawnShelter[side], pawn_shelter_score<side>(castleKingTo[2 * side + KINGSIDE]));

    if (pos.castleRights & QUEENSIDE_CASTLE_MASKS[side])
        pawntte->pawnShelter[side] = max(pawntte->pawnShelter[side], pawn_shelter_score<side>(castleKingTo[2 * side + QUEENSIDE]));
}

template <Tracing T> void Eval<T>::evaluate_pawns()
{
#ifndef __TUNE__
    if (pawntte->pawn_hash != pos.pawnhash) //entry not found, update everything
    {
        pawntte->pawn_hash = pos.pawnhash;
        pawntte->kingpos[WHITE] = pos.kingpos[WHITE];
        pawntte->kingpos[BLACK] = pos.kingpos[BLACK];
        pawntte->castling = pos.castleRights;

        evaluate_pawn_structure<WHITE>();
        evaluate_pawn_structure<BLACK>();
        pawn_shelter_castling<WHITE>();
        pawn_shelter_castling<BLACK>();
    }
    else //entry found, have to update king position/castling etc.
    {
        if (pos.kingpos[WHITE] != pawntte->kingpos[WHITE]
            || (pos.castleRights & WHITECASTLE) != (pawntte->castling & WHITECASTLE))
            pawn_shelter_castling<WHITE>();

        if (pos.kingpos[BLACK] != pawntte->kingpos[BLACK]
            || (pos.castleRights & BLACKCASTLE) != (pawntte->castling & BLACKCASTLE))
            pawn_shelter_castling<BLACK>();

        pawntte->kingpos[WHITE] = pos.kingpos[WHITE];
        pawntte->kingpos[BLACK] = pos.kingpos[BLACK];
        pawntte->castling = pos.castleRights;
    }
#else // When tuning, always update pawn hash
    pawntte->pawn_hash = pos.pawnhash;
    pawntte->kingpos[WHITE] = pos.kingpos[WHITE];
    pawntte->kingpos[BLACK] = pos.kingpos[BLACK];
    pawntte->castling = pos.castleRights;

    evaluate_pawn_structure<WHITE>();
    evaluate_pawn_structure<BLACK>();
    pawn_shelter_castling<WHITE>();
    pawn_shelter_castling<BLACK>();
#endif
}

template <Tracing T> template <Color side> void Eval<T>::evaluate_pawn_structure() //get pawn score and find the passed pawns
{
    Score pawnStructure = S(0, 0);
    Color us = (side) ? BLACK : WHITE;
    Color them = ~us;
    U64 myPawns = pos.pieceBB[make_piece(us, PAWN)];
    U64 theirPawns = pos.pieceBB[make_piece(them, PAWN)];
    int sq, fwd1, fwd2;
    bool opposed, isolated, doubled, backward;
    U64 phalanx, supported, forward_threats;
    pawntte->passedPawns[us] = 0ULL;
    pawntte->attackSpans[us] = 0ULL;
    pawntte->semiopenFiles[us] = 0xFF;
    while (myPawns)
    {
        sq = popLsb(&myPawns);
        pawntte->attackSpans[us] |= (passedPawnMasks[us][sq] ^ pawnBlockerMasks[us][sq]);

        fwd1 = PAWNPUSHINDEX(us, sq);
        fwd2 = PAWNPUSHINDEX(us, fwd1);
        opposed = (pawnBlockerMasks[us][sq] & theirPawns) != 0ULL;
        isolated = (neighborMasks[sq] & myPawns) == 0ULL;
        doubled = (pawnBlockerMasks[us][sq] & myPawns) != 0ULL;
        forward_threats = (PAWN_ATTACKS[us][fwd1] & theirPawns);
        backward = !(passedPawnMasks[them][sq] & myPawns) && forward_threats;
        phalanx = phalanxMasks[sq] & myPawns;
        supported = PAWN_ATTACKS[them][sq] & myPawns;

        #if TUNERTRACE
        Trace.piece_bonus[side][PAWN][FLIP_SQUARE(side, sq)]++;
        Trace.piece_values[side][PAWN]++;
        #endif // TUNERTRACE

        if (isolated)
        {
            pawnStructure -= (BITSET(sq) & FileAH) ? isolated_penaltyAH[opposed] : isolated_penalty[opposed];
            #if TUNERTRACE
            if (BITSET(sq) & FileAH)
                Trace.isolated_penaltyAH[side][opposed]--;
            else
                Trace.isolated_penalty[side][opposed]--;
            #endif // TUNERTRACE
        }

        if (backward)
        {
            pawnStructure -= backward_penalty[opposed];
            #if TUNERTRACE
            Trace.backward_penalty[side][opposed]--;
            #endif // TUNERTRACE
        }

        if (doubled)
        {
            pawnStructure -= (BITSET(sq) & attackedSquares[make_piece(side, PAWN)]) ? doubled_penalty[opposed] : doubled_penalty_undefended[opposed];
            #if TUNERTRACE
            if (BITSET(sq) & attackedSquares[make_piece(side, PAWN)])
                Trace.doubled_penalty[side][opposed]--;
            else
                Trace.doubled_penalty_undefended[side][opposed]--;
            #endif // TUNERTRACE
        }

        if (isolated && doubled)
        {
            pawnStructure -= (BITSET(sq) & FileAH) ? isolated_doubled_penaltyAH[opposed] : isolated_doubled_penalty[opposed];
            #if TUNERTRACE
            if (BITSET(sq) & FileAH)
                Trace.isolated_doubled_penaltyAH[side][opposed]--;
            else
                Trace.isolated_doubled_penalty[side][opposed]--;
            #endif // TUNERTRACE
        }

        if (phalanx | supported)
        {
            pawnStructure += connected_bonus[opposed][bool(phalanx)][RRANK(sq, us)];
            #if TUNERTRACE
            Trace.connected_bonus[side][opposed][bool(phalanx)][RRANK(sq,us)]++;
            #endif // TUNERTRACE
        }


        if (!opposed &&
            (!(passedPawnMasks[us][fwd1] & theirPawns)
                || ((passedPawnMasks[us][fwd2] & theirPawns)
                    && (POPCOUNT(phalanx) >= POPCOUNT(forward_threats)))
                ))
            pawntte->passedPawns[us] |= BITSET(sq);
        pawntte->semiopenFiles[us] &= ~(1 << FILE(sq));
    }

    pawntte->scores[us] = pawnStructure;
}

template <Tracing T> template <Color side> Score Eval<T>::evaluate_passers()
{
    constexpr Color opponent = ~side;
    constexpr int Up = side == WHITE ? NORTH : SOUTH;
    Score out = S(0, 0);
    U64 passers = pawntte->passedPawns[side];
    U64 myRooks = pos.pieceBB[make_piece(side, ROOK)];
    U64 opponentRooks = pos.pieceBB[make_piece(opponent, ROOK)];
    int sq, r;
    bool blocked, unsafe;
    while (passers)
    {
        sq = popLsb(&passers);
        r = RRANK(sq, side);
        out += passedRankBonus[r];
        blocked = SQUARE_MASKS[PAWNPUSHINDEX(side, sq)] & (pos.occupiedBB[0] | pos.occupiedBB[1]);
        unsafe = SQUARE_MASKS[PAWNPUSHINDEX(side, sq)] & attackedSquares[opponent];
        out += passedUnsafeBonus[unsafe][r];
        out += passedBlockedBonus[blocked][r];

        // King distance from passed pawn
        out += passedFriendlyDistance[r] * squareDistance[pos.kingpos[side]][sq + Up];
        out += passedEnemyDistance[r] * squareDistance[pos.kingpos[opponent]][sq + Up];

        #if TUNERTRACE
        Trace.passedRankBonus[side][r]++;
        Trace.passedUnsafeBonus[side][unsafe][r]++;
        Trace.passedBlockedBonus[side][blocked][r]++;
        Trace.passedFriendlyDistance[side][r] += squareDistance[pos.kingpos[side]][sq + Up];
        Trace.passedEnemyDistance[side][r] += squareDistance[pos.kingpos[opponent]][sq + Up];
        #endif // TUNERTRACE
        // Rooks behind passed pawn

        if (r >= 3)
        {
            if (myRooks & pawnBlockerMasks[opponent][sq])
            {
                out += tarraschRule_friendly[r];
                #if TUNERTRACE
                Trace.tarraschRule_friendly[side][r]++;
                #endif // TUNERTRACE
            }

            if (opponentRooks & pawnBlockerMasks[opponent][sq])
            {
                out -= tarraschRule_enemy;
                #if TUNERTRACE
                Trace.tarraschRule_enemy[side]--;
                #endif // TUNERTRACE
            }

        }
    }

    if (T)
    {
        trace_scores[PASSERS][side] = out;
    }
    return out;
}

template <Tracing T> template <Color side> Score Eval<T>::king_safety() const
{
    //king danger for white, e.g. = attacks on white king - safety features for white king
    // attacks = flank attacks/double attacks + safe checks + king ring attacks + pawn storm
    // safety features = pawn shelter score + flank defense
    Color opponent = (side == WHITE) ? BLACK : WHITE;
    int outMG = 0;
    int outEG = 0;
    int kingSquare = pos.kingpos[side];
    int pawn_shelter = pawntte->pawnShelter[side];
    outMG += pawn_shelter;

    if (king_attackers_count[side] > (1 - pos.pieceCount[make_piece(opponent, QUEEN)]))
    {
        U64 weak = attackedSquares[opponent]
            & ~double_targets[side]
            & (~attackedSquares[side] | attackedSquares[make_piece(side, KING)] | attackedSquares[make_piece(side, QUEEN)]);

        //the higher this number, the worse for [side]
        int king_danger = kingDangerBase
            - pawn_shelter * kingShieldBonus / 10
            - !pos.pieceCount[make_piece(opponent, QUEEN)] * noQueen
            + king_attackers_count[side] * king_attackers_weight[side]
            + king_attacks_count[side] * kingringAttack
            + bool(pos.blockersForKing[side]) * kingpinnedPenalty
            + POPCOUNT(weak & kingRings[side]) * kingweakPenalty;

        #if TUNERTRACE and TUNESAFETY
        Trace.kingDangerBase[side]++;
        Trace.kingShieldBonus[side] -= pawn_shelter/10;
        Trace.noQueen[side] -= !pos.pieceCount[make_piece(opponent, QUEEN)];
        for (int i = 0; i < 7; i++)
        {
            Trace.attackerWeights[side][i] *= king_attackers_count[side];
        }
        Trace.kingringAttack[side] += king_attacks_count[side];
        Trace.kingpinnedPenalty[side] += bool(pos.blockersForKing[side]);
        Trace.kingweakPenalty[side] += POPCOUNT(weak & kingRings[side]);
        #endif // TUNERTRACE

        U64 safe = ~pos.occupiedBB[opponent] & (~attackedSquares[side] | (weak & double_targets[opponent]));

        U64 occ = pos.occupiedBB[0] | pos.occupiedBB[1];
        U64 rookSquares = rookAttacks(occ, kingSquare);
        U64 bishopSquares = bishopAttacks(occ, kingSquare);
        U64 knightSquares = PseudoAttacks[KNIGHT][kingSquare];

        U64 queenChecks, rookChecks, bishopChecks, knightChecks;

        rookChecks = ((rookSquares)&attackedSquares[make_piece(opponent, ROOK)]);
        if (rookChecks)
        {
            king_danger += (rookChecks & safe) ? checkPenalty[ROOK] : unsafeCheckPenalty[ROOK];
            #if TUNERTRACE and TUNESAFETY
            if (rookChecks & safe)
                Trace.checkPenalty[side][ROOK]++;
            else
                Trace.unsafeCheckPenalty[side][ROOK]++;
            #endif // TUNERTRACE
        }

        queenChecks = ((rookSquares | bishopSquares)
            & attackedSquares[make_piece(opponent, QUEEN)]
            & ~attackedSquares[make_piece(side, QUEEN)]
            & ~rookChecks);

        if (queenChecks)
        {
            if (queenChecks & ~attackedSquares[make_piece(side, KING)])
            {
                king_danger += (queenChecks & safe) ? checkPenalty[QUEEN] : unsafeCheckPenalty[QUEEN];
                #if TUNERTRACE and TUNESAFETY
                if (queenChecks & safe)
                    Trace.checkPenalty[side][QUEEN]++;
                else
                    Trace.unsafeCheckPenalty[side][QUEEN]++;
                #endif // TUNERTRACE
            }

            if (queenChecks & attackedSquares[make_piece(side, KING)] & double_targets[opponent] & weak)
            {
                king_danger += queenContactCheck;
                #if TUNERTRACE and TUNESAFETY
                Trace.queenContactCheck[side]++;
                #endif // TUNERTRACE
            }
        }

        bishopChecks = ((bishopSquares)
            &attackedSquares[make_piece(opponent, BISHOP)]
            & ~queenChecks);

        if (bishopChecks)
        {
            king_danger += (bishopChecks & safe) ? checkPenalty[BISHOP] : unsafeCheckPenalty[BISHOP];
            #if TUNERTRACE and TUNESAFETY
            if (bishopChecks & safe)
                Trace.checkPenalty[side][BISHOP]++;
            else
                Trace.unsafeCheckPenalty[side][BISHOP]++;
            #endif // TUNERTRACE
        }

        knightChecks = ((knightSquares)
            &attackedSquares[make_piece(opponent, KNIGHT)]);

        if (knightChecks)
        {
            king_danger += (knightChecks & safe) ? checkPenalty[KNIGHT] : unsafeCheckPenalty[KNIGHT];
            #if TUNERTRACE and TUNESAFETY
            if (knightChecks & safe)
                Trace.checkPenalty[side][KNIGHT]++;
            else
                Trace.unsafeCheckPenalty[side][KNIGHT]++;
            #endif // TUNERTRACE
        }

        if (king_danger > 0)
        {
            outMG -= king_danger * king_danger / 4096;
            outEG -= king_danger / 20;
        }
    }

    Score out = S(outMG, outEG);

    if (pos.pieceBB[make_piece(side, PAWN)])
    {
        int distance = 0;
        while (!(distanceRings[kingSquare][distance++] & pos.pieceBB[make_piece(side, PAWN)])) {}
        out -= pawnDistancePenalty * distance;
        #if TUNERTRACE
        Trace.pawnDistancePenalty[side] -= distance;
        #endif // TUNERTRACE
    }

    U64 flankAttacks = attackedSquares[opponent] & flank_ranks[side] & flank_files[FILE(kingSquare)];
    U64 double_flank_attacks = flankAttacks & double_targets[opponent];
    out -= kingflankAttack * (POPCOUNT(flankAttacks) + POPCOUNT(double_flank_attacks));
    #if TUNERTRACE
    Trace.kingflankAttack[side] -= (POPCOUNT(flankAttacks) + POPCOUNT(double_flank_attacks));
    Trace.piece_bonus[side][KING][FLIP_SQUARE(side, kingSquare)]++;
    #endif // TUNERTRACE

    if (T)
    {
        trace_scores[KING_SAFETY][side] = out;
    }

    return out;
}

template <Tracing T> template <Color side, PieceType type> Score Eval<T>::evaluate_piece()
{
    Score out = S(0, 0);
    constexpr PieceCode pc = make_piece(side, type);
    U64 pieces = pos.pieceBB[pc];
    int sq;
    constexpr U64 outpostRanks = outpost_ranks[side];
    constexpr Color opponent = ~side;
    int kingSquare = pos.kingpos[side];
    U64 attacks, kingAttacks;

    while (pieces)
    {
        sq = popLsb(&pieces);
        attacks = (type == BISHOP) ? pos.getAttackSet(sq, (pos.occupiedBB[0] | pos.occupiedBB[1]) ^ pos.pieceBB[WQUEEN] ^ pos.pieceBB[BQUEEN])
            : (type == ROOK) ? pos.getAttackSet(sq, (pos.occupiedBB[0] | pos.occupiedBB[1]) ^ pos.pieceBB[WQUEEN] ^ pos.pieceBB[BQUEEN] ^ pos.pieceBB[pc])
            : pos.getAttackSet(sq, (pos.occupiedBB[0] | pos.occupiedBB[1]));
        if ((pos.blockersForKing[side][0] | pos.blockersForKing[side][1]) & BITSET(sq))
            attacks &= RAY_MASKS[sq][pos.kingpos[side]];

        kingAttacks = attacks & kingRings[opponent];

        if (kingAttacks)
        {
            king_attackers_count[opponent] ++;
            king_attackers_weight[opponent] += attackerWeights[pc >> 1];
            king_attacks_count[opponent] += POPCOUNT(kingAttacks);
            #if TUNERTRACE and TUNESAFETY
            Trace.attackerWeights[side][pc >> 1]++;
            #endif // TUNERTRACE
        }

        double_targets[side] |= attackedSquares[side] & attacks;
        attackedSquares[side] |= attackedSquares[pc] |= attacks;

        if (type == BISHOP || type == KNIGHT)
        {
            U64 outpostSquares = outpostRanks & ~pawntte->attackSpans[opponent];

            if (outpostSquares & BITSET(sq))
            {
                out += outpostBonus[bool(attackedSquares[make_piece(side, PAWN)] & BITSET(sq))][type == KNIGHT];
                #if TUNERTRACE
                Trace.outpostBonus[side][bool(attackedSquares[make_piece(side, PAWN)] & BITSET(sq))][type == KNIGHT]++;
                #endif // TUNERTRACE
            }
            else if (outpostSquares & attacks & ~pos.occupiedBB[side])
            {
                out += reachableOutpost[type == KNIGHT];
                #if TUNERTRACE
                Trace.reachableOutpost[side][type == KNIGHT]++;
                #endif // TUNERTRACE
            }

            if (type == BISHOP)
            {
                int same_color_pawns = POPCOUNT(pos.pieceBB[make_piece(side, PAWN)] & colorMasks[sq]);
                out -= bishopPawns * same_color_pawns;

                U64 pawns = pos.pieceBB[WPAWN] | pos.pieceBB[BPAWN];
                if (pawns & trappedBishop[side][sq])
                {
                    out -= (pawns & veryTrappedBishop[sq]) ? veryTrappedBishopPenalty : trappedBishopPenalty;
                    #if TUNERTRACE
                    if (pawns & veryTrappedBishop[sq])
                        Trace.veryTrappedBishopPenalty[side]++;
                    else
                        Trace.trappedBishopPenalty[side]++;
                    #endif // TUNERTRACE
                }

                // TODO (drstrange767#1#): try 2x for knights on rim, try removing leftRight, etc
                if (pos.pieceBB[make_piece(opponent, KNIGHT)] & (knightOpposingBishop[side][sq]))
                {
                    out += bishopOpposerBonus;
                    #if TUNERTRACE
                    Trace.bishopOpposerBonus[side]++;
                    #endif // TUNERTRACE
                }

                mobility[side] += bishopMobilityBonus[POPCOUNT(attacks & (mobility_area[side]))];
                #if TUNERTRACE
                Trace.bishopMobilityBonus[side][POPCOUNT(attacks & (mobility_area[side]))]++;
                Trace.piece_bonus[side][BISHOP][FLIP_SQUARE(side, sq)]++;
                Trace.piece_values[side][BISHOP]++;
                #endif // TUNERTRACE
            }
            else // KNIGHT
            {
                mobility[side] += knightMobilityBonus[POPCOUNT(attacks & (mobility_area[side]))];
                #if TUNERTRACE
                Trace.knightMobilityBonus[side][POPCOUNT(attacks & (mobility_area[side]))]++;
                Trace.piece_bonus[side][KNIGHT][FLIP_SQUARE(side, sq)]++;
                Trace.piece_values[side][KNIGHT]++;
                #endif // TUNERTRACE
            }

            out -= kingProtector * squareDistance[sq][kingSquare];
            #if TUNERTRACE
            Trace.kingProtector[side] -= squareDistance[sq][kingSquare];
            #endif // TUNERTRACE
        }

        if (type == ROOK)
        {
            if (pawntte->semiopenFiles[side] & (1 << FILE(sq)))
            {
                if (pawntte->semiopenFiles[opponent] & (1 << FILE(sq))) // open file
                {
                    out += rookFile[1];
                    #if TUNERTRACE
                    Trace.rookFile[side][1]++;
                    #endif // TUNERTRACE
                }

                else // semiopen defended / not defended
                {
                    out += (attackedSquares[make_piece(opponent, PAWN)] & pos.pieceBB[make_piece(opponent, PAWN)] & FileBB[FILE(sq)]) ? defendedRookFile : rookFile[0];
                    #if TUNERTRACE
                    if (attackedSquares[make_piece(opponent, PAWN)] & pos.pieceBB[make_piece(opponent, PAWN)] & FileBB[FILE(sq)])
                        Trace.defendedRookFile[side]++;
                    else
                        Trace.rookFile[side][0]++;
                    #endif // TUNERTRACE
                }
            }

            if (FileBB[FILE(sq)] & (pos.pieceBB[WQUEEN] | pos.pieceBB[BQUEEN]))
            {
                out += battery;
                #if TUNERTRACE
                Trace.battery[side]++;
                #endif // TUNERTRACE
            }


            if (RRANK(sq, side) == 6 && RRANK(pos.kingpos[opponent], side) == 7)
            {
                out += rank7Rook;
                #if TUNERTRACE
                Trace.rank7Rook[side]++;
                #endif // TUNERTRACE
            }


            mobility[side] += rookMobilityBonus[POPCOUNT(attacks & (mobility_area[side]))];
            #if TUNERTRACE
            Trace.rookMobilityBonus[side][POPCOUNT(attacks & (mobility_area[side]))]++;
            Trace.piece_bonus[side][ROOK][FLIP_SQUARE(side, sq)]++;
            Trace.piece_values[side][ROOK]++;
            #endif // TUNERTRACE
        }

        if (type == QUEEN)
        {
            mobility[side] += queenMobilityBonus[POPCOUNT(attacks & (mobility_area[side]))];
            #if TUNERTRACE
            Trace.queenMobilityBonus[side][POPCOUNT(attacks & (mobility_area[side]))]++;
            Trace.piece_bonus[side][QUEEN][FLIP_SQUARE(side, sq)]++;
            Trace.piece_values[side][QUEEN]++;
            #endif // TUNERTRACE
        }
    }

    if (T)
    {
        trace_scores[type][side] = out;
    }

    return out;
}

template <Tracing T> template <Color side> Score Eval<T>::evaluate_threats() const
{
    Color opponent = ~side;
    Score out = S(0, 0);
    U64 nonPawns = pos.occupiedBB[opponent] ^ pos.pieceBB[make_piece(opponent, PAWN)];
    U64 supported = (double_targets[opponent] | attackedSquares[make_piece(opponent, PAWN)]) & ~double_targets[side];
    U64 weak = attackedSquares[side] & ~supported & pos.occupiedBB[opponent];
    U64 occ = pos.occupiedBB[0] | pos.occupiedBB[1];
    U64 RRank3 = (side) ? Rank6BB : Rank3BB;

    U64 attacked;
    int sq;
    if (nonPawns | weak)
    {
        attacked = (nonPawns | weak) & (attackedSquares[make_piece(side, KNIGHT)] | attackedSquares[make_piece(side, BISHOP)]);
        while (attacked)
        {
            sq = popLsb(&attacked);
            out += minorThreat[pos.mailbox[sq] >> 1];
            #if TUNERTRACE
            Trace.minorThreat[side][pos.mailbox[sq] >> 1]++;
            #endif // TUNERTRACE
        }

        attacked = (pos.pieceBB[make_piece(opponent, QUEEN)] | weak) & (attackedSquares[make_piece(side, ROOK)]);
        while (attacked)
        {
            sq = popLsb(&attacked);
            out += rookThreat[pos.mailbox[sq] >> 1];
            #if TUNERTRACE
            Trace.rookThreat[side][pos.mailbox[sq] >> 1]++;
            #endif // TUNERTRACE
        }

        attacked = (weak & attackedSquares[make_piece(side, KING)]);
        if (attacked)
        {
            out += MORETHANONE(attacked) ? kingMultipleThreat : kingThreat;
            #if TUNERTRACE
            if (MORETHANONE(attacked))
                Trace.kingMultipleThreat[side]++;
            else
                Trace.kingThreat[side]++;
            #endif // TUNERTRACE
        }

        out += hangingPiece * POPCOUNT(weak & (~attackedSquares[opponent] | (nonPawns & double_targets[side])));
        #if TUNERTRACE
        Trace.hangingPiece[side] += POPCOUNT(weak & (~attackedSquares[opponent] | (nonPawns & double_targets[side])));
        #endif // TUNERTRACE
    }

    U64 safe = ~attackedSquares[opponent] | attackedSquares[side];
    U64 safePawns = safe & pos.pieceBB[make_piece(side, PAWN)];
    attacked = PAWNATTACKS(side, safePawns) & nonPawns;
    out += safePawnThreat * POPCOUNT(attacked);
    #if TUNERTRACE
    Trace.safePawnThreat[side] += POPCOUNT(attacked);
    #endif // TUNERTRACE

    U64 pawnPushes = PAWNPUSH(side, pos.pieceBB[make_piece(side, PAWN)]) & ~occ;
    pawnPushes |= PAWNPUSH(side, pawnPushes & RRank3) & ~occ;

    pawnPushes &= ~attackedSquares[opponent] & safe;
    out += pawnPushThreat * POPCOUNT(PAWNATTACKS(side, pawnPushes) & nonPawns);
    #if TUNERTRACE
    Trace.pawnPushThreat[side] += POPCOUNT(PAWNATTACKS(side, pawnPushes) & nonPawns);
    #endif // TUNERTRACE

    if (T)
    {
        trace_scores[THREAT][side] = out;
    }

    return out;
}

int imbalance(const Position& pos, Color side)
{
    // Quadratic material imbalance by Tord Romstad
    int bonus = 0;
    for (int pt1 = PAWN; pt1 < KING; ++pt1) {
        if (!pos.pieceCount[make_piece(side, PieceType(pt1))]) {
            continue;
        }

        for (int pt2 = PAWN; pt2 <= pt1; ++pt2) {
            bonus += pos.pieceCount[make_piece(side, PieceType(pt1))] * (my_pieces[pt1 - 1][pt2 - 1] * pos.pieceCount[make_piece(side, PieceType(pt2))] +
                opponent_pieces[pt1 - 1][pt2 - 1] * pos.pieceCount[make_piece(~side, PieceType(pt2))]);
            #if TUNERTRACE
            Trace.my_pieces[side][pt1 - 1][pt2 - 1] += (pos.pieceCount[make_piece(side, PieceType(pt1))]*pos.pieceCount[make_piece(side, PieceType(pt2))]) / 16;
            Trace.opponent_pieces[side][pt1 - 1][pt2 - 1] += (pos.pieceCount[make_piece(side, PieceType(pt1))]*pos.pieceCount[make_piece(~side, PieceType(pt2))]) / 16;
            #endif // TUNERTRACE
        }
    }

    return bonus;
}

U64 materialKey(const Position& pos)
{
    U64 materialhash = 0ULL;

    for (int i = WPAWN; i <= BKING; i++)
        for (int j = 0; j < pos.pieceCount[i]; j++)
            materialhash ^= zb.pieceKeys[(j << 4) | i];

    return materialhash;
}

materialhashEntry* probeMaterial(const Position& pos)
{
    materialhashEntry* material = get_materialEntry(pos);

#ifndef __TUNE__
    if (material->key == pos.materialhash)
    {
        return material;
    }
#endif

    material->key = pos.materialhash;
    material->phase = ((24 - (pos.pieceCount[WBISHOP] + pos.pieceCount[BBISHOP] + pos.pieceCount[WKNIGHT] + pos.pieceCount[BKNIGHT])
        - 2 * (pos.pieceCount[WROOK] + pos.pieceCount[BROOK])
        - 4 * (pos.pieceCount[WQUEEN] + pos.pieceCount[BQUEEN])) * 255 + 12) / 24;

    int value = (imbalance(pos, WHITE) - imbalance(pos, BLACK)) / 16;

    // Bishop pair
    if (pos.pieceCount[WBISHOP] > 1) {
        value += bishop_pair;
        #if TUNERTRACE
        Trace.bishop_pair[WHITE]++;
        #endif // TUNERTRACE
    }
    if (pos.pieceCount[BBISHOP] > 1) {
        value -= bishop_pair;
        #if TUNERTRACE
        Trace.bishop_pair[BLACK]++;
        #endif // TUNERTRACE
    }

    material->score = S(value, value);

    // Endgames

    int white_minor = pos.pieceCount[WBISHOP] + pos.pieceCount[WKNIGHT];
    int white_major = pos.pieceCount[WROOK] + pos.pieceCount[WQUEEN];
    int black_minor = pos.pieceCount[BBISHOP] + pos.pieceCount[BKNIGHT];
    int black_major = pos.pieceCount[BROOK] + pos.pieceCount[BQUEEN];
    int all_minor = white_minor + black_minor;
    int all_major = white_major + black_major;
    bool no_pawns = pos.pieceCount[WPAWN] == 0 && pos.pieceCount[BPAWN] == 0;

    material->isDrawn = false;

    if (no_pawns && all_minor + all_major == 0) {
        material->isDrawn = true;
    }
    else if (no_pawns && all_major == 0 && white_minor < 2 && black_minor < 2) {
        material->isDrawn = true;
    }
    else if (no_pawns && all_major == 0 && all_minor == 2 && (pos.pieceCount[WKNIGHT] == 2 || pos.pieceCount[BKNIGHT] == 2)) {
        material->isDrawn = true;
    }

    material->hasSpecialEndgame = false;
    material->evaluation = nullptr;

    /// Special endgame for KBNvK
    if (pos.materialhash == 0xa088eeb4f991b4ea || pos.materialhash == 0x52f8aa4b980286be)
    {
        material->hasSpecialEndgame = true;
        material->evaluation = &KBNvK;
    }

    return material;
}

template <Tracing T> int Eval<T>::value()
{
    materialhashEntry* material = probeMaterial(pos);

    if (material->hasSpecialEndgame)
    {
        return material->evaluation(pos);
    }

    if (material->isDrawn)
    {
        return 1 - (pos.my_thread->nodes & 2);
    }

    pawntte = get_pawntte(pos);
    evaluate_pawns();

    Score out = material->score + pos.psqt_score;

    out += pawntte->scores[WHITE] - pawntte->scores[BLACK];
#ifndef __TUNE__
    int lazyValue = (mg_value(out) + eg_value(out)) / 2;

    /// Early return if value is high/low enough

    if (abs(lazyValue) >= lazyThreshold + (pos.nonPawn[0] + pos.nonPawn[1]) / 64)
        goto return_flag;
#endif

    pre_eval();

    out += evaluate_piece<WHITE, KNIGHT>() - evaluate_piece<BLACK, KNIGHT>() +
        evaluate_piece<WHITE, BISHOP>() - evaluate_piece<BLACK, BISHOP>() +
        evaluate_piece<WHITE, ROOK>() - evaluate_piece<BLACK, ROOK>() +
        evaluate_piece<WHITE, QUEEN>() - evaluate_piece<BLACK, QUEEN>();

    out += evaluate_passers<WHITE>() - evaluate_passers<BLACK>() +
        mobility[WHITE] - mobility[BLACK] +
        evaluate_threats<WHITE>() - evaluate_threats<BLACK>() +
        king_safety<WHITE>() - king_safety<BLACK>(); // add more components maybe?
#ifndef __TUNE__
    return_flag :
#endif

    int phase = material->phase;

    #if TUNERTRACE
    Trace.originalScore = out; // always positive for white
    Trace.scale = pos.scaleFactor() / (double)SCALE_NORMAL;
    #endif // TUNERTRACE

    int v = ((mg_value(out) * (256 - phase) + eg_value(out) * phase * pos.scaleFactor() / SCALE_NORMAL) / 256);

    if (T)
    {
        trace_scores[PAWNS][WHITE] = pawntte->scores[WHITE];
        trace_scores[PAWNS][BLACK] = pawntte->scores[BLACK];
        trace_scores[MOBILITY][WHITE] = mobility[WHITE];
        trace_scores[MOBILITY][BLACK] = mobility[BLACK];
        trace_scores[MATERIAL][0] = pos.psqt_score;
        trace_scores[IMBALANCE][0] = material->score;
        trace_scores[PHASE][0] = Score(phase);
        trace_scores[SCALE][0] = Score(pos.scaleFactor());
        trace_scores[TEMPO][0] = Score(tempo);
        trace_scores[TOTAL][0] = out;
    }

    v = ((v)*S2MSIGN(pos.activeSide) + tempo);

    return v;
}

//a wrapper function to look clean :P
int evaluate(const Position& pos)
{
    return Eval<NO_TRACE>(pos).value();
}

double to_cp(int v) { return double(v) / PAWN_EG; }

std::ostream& operator<<(std::ostream& os, Score s) {
    os << std::setw(5) << to_cp(mg_value(s)) << " "
        << std::setw(5) << to_cp(eg_value(s));
    return os;
}

std::ostream& operator<<(std::ostream& os, evalTerms t) {

    if (t == MATERIAL || t == IMBALANCE || t == TOTAL)
    {
        os << " ----  ----" << " | " << " ----  ----";
        os << " | " << trace_scores[t][WHITE] << "\n";
    }
    else if (t == PHASE || t == SCALE || t == TEMPO)
    {
        os << " ----  ----" << " | " << " ----  ----";
        os << " | " << std::setw(5) << mg_value(trace_scores[t][WHITE]) << ((t == PHASE) ? " / 256" : (t == SCALE) ? " /  32" : "") << "\n";
    }
    else
    {
        os << trace_scores[t][WHITE] << " | " << trace_scores[t][BLACK];
        os << " | " << trace_scores[t][WHITE] - trace_scores[t][BLACK] << "\n";
    }

    return os;
}

string trace(const Position& pos) {

    if (pos.checkBB)
        return "Total evaluation: none (in check)";

    memset(trace_scores, 0, sizeof(trace_scores));

    int v = Eval<DO_TRACE>(pos).value();

    v = pos.activeSide == WHITE ? v : -v; // Trace scores are from white's point of view

    stringstream ss;
    ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2)
        << "     Term    |    White    |    Black    |    Total   \n"
        << "             |   MG    EG  |   MG    EG  |   MG    EG \n"
        << " ------------+-------------+-------------+------------\n"
        << "    Material | " << evalTerms(MATERIAL)
        << "   Imbalance | " << evalTerms(IMBALANCE)
        << "     Knights | " << evalTerms(KNIGHTS)
        << "     Bishops | " << evalTerms(BISHOPS)
        << "       Rooks | " << evalTerms(ROOKS)
        << "       Pawns | " << evalTerms(PAWNS)
        << "    Mobility | " << evalTerms(MOBILITY)
        << "      Threat | " << evalTerms(THREAT)
        << " King safety | " << evalTerms(KING_SAFETY)
        << "      Passed | " << evalTerms(PASSERS)
        << " ------------+-------------+-------------+------------\n"
        << "       Phase | " << evalTerms(PHASE)
        << "       Scale | " << evalTerms(SCALE)
        << "       Tempo | " << evalTerms(TEMPO)
        << "       Total | " << evalTerms(TOTAL);

    ss << "\nTotal evaluation: " << to_cp(v) << " (white side)\n";

    return ss.str();
}

int KBNvK(const Position& pos)
{
    Color strongerSide = (pos.psqt_score > 0) ? WHITE : BLACK;
    bool darkBishop = (pos.pieceBB[WBISHOP + strongerSide] & DarkSquares);
    int sk = pos.kingpos[strongerSide] ^ (darkBishop ? 0 : 56);
    int wk = pos.kingpos[~strongerSide] ^ (darkBishop ? 0 : 56);
    int kingDistance = 7 - squareDistance[wk][sk];
    int cornerDistance = abs(7 - RANK(wk) - FILE(wk));

    return (S2MSIGN(pos.activeSide) * (WON_ENDGAME + 420 * cornerDistance + 20 * kingDistance));
}
