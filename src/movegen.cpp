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

template <PieceType Pt> SMove* generatePieceMoves(const Position &pos, SMove *movelist, U64 occ, U64 to_squares, U64 from_mask)
{
    const PieceCode pc = make_piece(pos.activeSide, Pt); //convert piece type + side to piece code
    U64 fromSquares = pos.pieceBB[pc] & from_mask;
    U64 tobits = 0ULL;
    while (fromSquares)
    {
        int from = popLsb(&fromSquares);
        if (Pt == KNIGHT)
            tobits = PseudoAttacks[KNIGHT][from] & to_squares;
        if(Pt == BISHOP || Pt == QUEEN)
            tobits |= bishopAttacks(occ, from) & to_squares;
        if (Pt == ROOK || Pt == QUEEN)
            tobits |= rookAttacks(occ, from) & to_squares;
        if (Pt == KING)
            tobits = PseudoAttacks[KING][from] & to_squares & ~pos.tabooSquares();
        while (tobits)
        {
            int to = popLsb(&tobits);
            *(movelist++) = makeMove(from, to, NORMAL);
        }
    }
    return movelist;
}

inline bool Position::horizontalCheck(U64 occ, int kingSq) const
{
	int opponent = activeSide ^ SIDESWITCH;
	U64 rooksQueens = pieceBB[WROOK | opponent] | pieceBB[WQUEEN | opponent];
	return rooksQueens && (rookAttacks(occ, kingSq) & rooksQueens);
}

template <Color side, MoveType Mt> inline SMove* generatePawnMoves(const Position &pos, SMove *movelist, U64 from_mask, U64 to_mask)
{
    constexpr Color opponent = (side == WHITE) ? BLACK : WHITE;
    U64 occ = pos.occupiedBB[0] | pos.occupiedBB[1];
    constexpr PieceCode pc = (PieceCode)(WPAWN | side);
    constexpr U64  TRank7BB = (side == WHITE ? Rank7BB    : Rank2BB);
    constexpr U64  TRank3BB = (side == WHITE ? Rank3BB    : Rank6BB);
    constexpr Direction Up       = pawn_push(side);
    constexpr Direction UpRight  = (side == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction UpLeft   = (side == WHITE ? NORTH_WEST : SOUTH_EAST);
    U64 pawnsOn7    = pos.pieceBB[pc] &  TRank7BB & from_mask;
    U64 pawnsNotOn7 = pos.pieceBB[pc] & ~TRank7BB & from_mask;
    U64 enemies = pos.occupiedBB[opponent];
    U64 emptySquares = ~occ;

    int from, to;
    // The b1, b2, b3 scheme of separating the attacks is seen in Stockfish, for example, 
    // and encourages the compiler to parallelize the operations.

    if (Mt == QUIET_CHECK)
    {
        U64 target = PAWN_ATTACKSFROM[side][pos.kingpos[opponent]];
        U64 b1 = shift<Up>(pawnsNotOn7)   & emptySquares & target;
        U64 b2 = shift<Up>(b1 & TRank3BB) & emptySquares & target;
        while (b1)
        {
            to = popLsb(&b1);
            *(movelist++) = makeMove(to - Up, to, NORMAL);
        }

        while (b2)
        {
            to = popLsb(&b2);
            *(movelist++) = makeMove(to - Up - Up, to, NORMAL);
        }
        //break
        return movelist;
    }

    if (Mt & QUIET) // quiet non promotions
    {
        U64 b1 = shift<Up>(pawnsNotOn7)   & emptySquares & to_mask; //to_mask JUST for quiet discovered checks
        U64 b2 = shift<Up>(b1 & TRank3BB) & emptySquares & to_mask;

        while (b1)
        {
            to = popLsb(&b1);
            *(movelist++) = makeMove(to - Up, to, NORMAL);
        }

        while (b2)
        {
            to = popLsb(&b2);
            *(movelist++) = makeMove(to - Up - Up, to, NORMAL);
        }
    }

    if (Mt & CAPTURE) // capturing non promotions
    {
        U64 b1 = shift<UpRight>(pawnsNotOn7) & enemies;
        U64 b2 = shift<UpLeft >(pawnsNotOn7) & enemies;

        while (b1)
        {
            to = popLsb(&b1);
            *(movelist++) = makeMove(to - UpRight, to, NORMAL);
        }

        while (b2)
        {
            to = popLsb(&b2);
            *(movelist++) = makeMove(to - UpLeft, to, NORMAL);
        }

        if (pos.epSquare)
        {
            b1 = pawnsNotOn7 & PAWN_ATTACKS[opponent][pos.epSquare];
            while (b1)
            {
                from = popLsb(&b1);
				if (RANK(from) != RANK(pos.kingpos[side]) || !pos.horizontalCheck(occ ^ BITSET(from) ^ BITSET(pos.epSquare + ((side) ? 8 : -8)), pos.kingpos[side]))
                	*(movelist++) = makeMove(from, pos.epSquare, ENPASSANT);//addMove(from, epSquare, ENPASSANT, &m);
            }
        }

    }

    if (pawnsOn7 && Mt & PROMOTE)
    {
        U64 b1 = shift<UpRight>(pawnsOn7) & enemies;
        U64 b2 = shift<UpLeft >(pawnsOn7) & enemies;
        U64 b3 = shift<Up     >(pawnsOn7) & emptySquares;

        while (b1)
        {
            to = popLsb(&b1);
            *(movelist++) = makePromotionMove(to - UpRight, to, QUEEN, PROMOTION);
            *(movelist++) = makePromotionMove(to - UpRight, to, KNIGHT, PROMOTION);
            *(movelist++) = makePromotionMove(to - UpRight, to, ROOK, PROMOTION);
            *(movelist++) = makePromotionMove(to - UpRight, to, BISHOP, PROMOTION);
        }

        while (b2)
        {
            to = popLsb(&b2);
            *(movelist++) = makePromotionMove(to - UpLeft, to, QUEEN, PROMOTION);
            *(movelist++) = makePromotionMove(to - UpLeft, to, KNIGHT, PROMOTION);
            *(movelist++) = makePromotionMove(to - UpLeft, to, ROOK, PROMOTION);
            *(movelist++) = makePromotionMove(to - UpLeft, to, BISHOP, PROMOTION);
        }

        while (b3)
        {
            to = popLsb(&b3);
            *(movelist++) = makePromotionMove(to - Up, to, QUEEN, PROMOTION);
            *(movelist++) = makePromotionMove(to - Up, to, KNIGHT, PROMOTION);
            *(movelist++) = makePromotionMove(to - Up, to, ROOK, PROMOTION);
            *(movelist++) = makePromotionMove(to - Up, to, BISHOP, PROMOTION);
        }
    }
    return movelist;
}

template <Color side> inline SMove* generateCastles(const Position &pos, SMove *movelist)
{
    U64 occupied = pos.occupiedBB[0] | pos.occupiedBB[1];
    for (int index = side*2; index < side*2 + 2; index++)
    {
        if ((pos.castleRights & (WQCMASK << index)) == 0)
            continue;
        int kingFrom = pos.kingpos[side];
        int rookFrom = castleRookFrom[index];
        if (BETWEEN_MASKS[kingFrom][rookFrom] & occupied)
            continue;
        U64 attackTargets = castlekingwalk[index];
        bool attacked = false;
        while (!attacked && attackTargets)
        {
            int to = popLsb(&attackTargets);
            attacked = pos.isAttacked(to, side^SIDESWITCH);
        }
        if (attacked)
            continue;
        *movelist++ = makeMove(kingFrom, rookFrom, CASTLING);
    }
    return movelist;
}

template <Color side, MoveType Mt> SMove* generatePinnedMoves(const Position &pos, SMove *movelist) //gets the pinned moves and updates the pinned pieces for me
{
	constexpr Color opponent = (side == WHITE) ? BLACK : WHITE;
    int sq = pos.kingpos[side];
    U64 myPieces = pos.occupiedBB[side];
    U64 diagPinned = pos.blockersForKing[side][0] & myPieces;
    U64 straightPinned = pos.blockersForKing[side][1] & myPieces;
    int from, to;
    U64 pinned;
	U64 targetMask;
	U64 fromBB, toBB;
	U64 occ = pos.occupiedBB[0] | pos.occupiedBB[1];

	U64 to_squares = 0ULL;
	if (Mt & QUIET)
		to_squares |= ~occ;
	if (Mt & CAPTURE)
		to_squares |= pos.occupiedBB[opponent];

    while (diagPinned)
    {
        from = popLsb(&diagPinned);
        targetMask = RAY_MASKS[sq][from] & to_squares;
        pinned = BITSET(from);
        const PieceType pt = PieceType(pos.mailbox[from] >> 1);
        if (pt == BISHOP)
            movelist = generatePieceMoves <BISHOP> (pos, movelist, occ, targetMask, pinned);
        else if (pt == QUEEN)
            movelist = generatePieceMoves <QUEEN> (pos, movelist, occ, targetMask, pinned);
        else if (pt == PAWN)
        {
            if (RRANK(from, side) == 6)
            {
                //generate pawn promotion captures
                toBB = PAWN_ATTACKS[side][from] & pos.occupiedBB[opponent] & targetMask;
                while (toBB)
                {
                    to = popLsb(&toBB);
                    *(movelist++) = makePromotionMove(from, to, QUEEN, PROMOTION);
                    *(movelist++) = makePromotionMove(from, to, KNIGHT, PROMOTION);
                    *(movelist++) = makePromotionMove(from, to, ROOK, PROMOTION);
                    *(movelist++) = makePromotionMove(from, to, BISHOP, PROMOTION);
                }
            }
            else
            {
                if (BITSET(pos.epSquare) & targetMask)
                {
                    //generate EP
                    fromBB = pos.pieceBB[WPAWN | side] & PAWN_ATTACKS[opponent][pos.epSquare] & pinned;
                    while (fromBB)
                    {
                        from = popLsb(&fromBB);
                        *movelist++ = makeMove(from, pos.epSquare, ENPASSANT);
                    }
                }
                //generate pawn non-promotion captures
                toBB = PAWN_ATTACKS[side][from] & pos.occupiedBB[opponent] & targetMask;
                while (toBB)
                {
                    to = popLsb(&toBB);
                    *movelist++ = makeMove(from, to, NORMAL);
                }
            }
        }
    }

    while (straightPinned)
    {
        from = popLsb(&straightPinned);
        targetMask = RAY_MASKS[sq][from] & to_squares;
        pinned = BITSET(from);
        const PieceType pt = PieceType(pos.mailbox[from] >> 1);
        if (pt == ROOK)
            movelist = generatePieceMoves <ROOK> (pos, movelist, occ, targetMask, pinned);
        else if (pt == QUEEN)
            movelist = generatePieceMoves <QUEEN> (pos, movelist, occ, targetMask, pinned);
        else if (pt == PAWN && (FILE(sq) == FILE(from)))
        {
            //generate pawn pushes
            if (Mt & QUIET)
                movelist = generatePawnMoves <side, QUIET> (pos, movelist, pinned);
        }
    }

	return movelist;
}

void Position::updateBlockers()
{
    U64 bishops = pieceBB[WBISHOP] | pieceBB[BBISHOP] | pieceBB[WQUEEN] | pieceBB[BQUEEN];
    U64 rooks = pieceBB[WROOK] | pieceBB[BROOK] | pieceBB[WQUEEN] | pieceBB[BQUEEN];
    U64 occ = occupiedBB[0] | occupiedBB[1];
    for (int us = WHITE; us <= BLACK; us++)
    {
        int opponent = us ^ SIDESWITCH;
        int sq = kingpos[us];
        U64 pinnedPieces = 0ULL;
        U64 diagsnipers = PseudoAttacks[BISHOP][sq] & bishops & occupiedBB[opponent];
        U64 straightsnipers = PseudoAttacks[ROOK][sq] & rooks & occupiedBB[opponent];
        int sniper;
        U64 pinned;
        while (diagsnipers)
        {
            sniper = popLsb(&diagsnipers);
            pinned = BETWEEN_MASKS[sq][sniper] & occ;
            if (!MORETHANONE(pinned))
            {
                pinnedPieces |= pinned;
            }
        }
        blockersForKing[us][0] = pinnedPieces;
        pinnedPieces = 0ULL;
        while (straightsnipers)
        {
            sniper = popLsb(&straightsnipers);
            pinned = BETWEEN_MASKS[sq][sniper] & occ;
            if (!MORETHANONE(pinned))
            {
                pinnedPieces |= pinned;
            }
        }
        blockersForKing[us][1] = pinnedPieces;
    }
}

template <Color side, MoveType Mt> SMove* generate_pseudo_legal_moves(const Position &pos, SMove *movelist, U64 from_mask)
{
	U64 to_squares = 0ULL;
	U64 occ = pos.occupiedBB[0] | pos.occupiedBB[1];

	if (Mt & QUIET)
		to_squares |= ~occ;
	if (Mt & CAPTURE)
		to_squares |= pos.occupiedBB[pos.activeSide ^ SIDESWITCH];

	movelist = generatePawnMoves <side, Mt> (pos, movelist, from_mask);
	movelist = generatePieceMoves <KNIGHT> (pos, movelist, occ, to_squares, from_mask);
	movelist = generatePieceMoves <BISHOP> (pos, movelist, occ, to_squares, from_mask);
	movelist = generatePieceMoves <ROOK> (pos, movelist, occ, to_squares, from_mask);
	movelist = generatePieceMoves <QUEEN> (pos, movelist, occ, to_squares, from_mask);
	movelist = generatePieceMoves <KING> (pos, movelist, occ, to_squares, from_mask);
	if (Mt & QUIET)
		movelist = generateCastles <side> (pos, movelist);
	return movelist;
}

template <Color side> SMove* generate_evasions(const Position &pos, SMove *movelist)
{
	U64 occ = pos.occupiedBB[0] | pos.occupiedBB[1];
	U64 to_squares = ~pos.occupiedBB[side];
	movelist = generatePieceMoves <KING> (pos, movelist, occ, to_squares);
	if (ONEORZERO(pos.checkBB))
	{
		int checker = LSB(pos.checkBB);
		U64 targets = BETWEEN_MASKS[pos.kingpos[side]][checker];
		U64 noPin = ~(pos.blockersForKing[side][0] | pos.blockersForKing[side][1]);
		U64 fromBB = pos.attackersTo(checker, side) & noPin;
		//Try to capture the checker
		while (fromBB)
		{
			int from = popLsb(&fromBB);
			if (pos.mailbox[from] <= BPAWN && PROMOTERANK(checker)) // a little bit of a hack
			{
                *movelist++ = makePromotionMove(from, checker, QUEEN, PROMOTION);
                *movelist++ = makePromotionMove(from, checker, KNIGHT, PROMOTION);
                *movelist++ = makePromotionMove(from, checker, BISHOP, PROMOTION);
                *movelist++ = makePromotionMove(from, checker, ROOK, PROMOTION);
			}
			else
                *movelist++ = makeMove(from, checker, NORMAL);
		}
		//try to EP the checker
		if (pos.epSquare && pos.epSquare == (checker + (S2MSIGN(side)*8)))
		{
			fromBB = PAWN_ATTACKSFROM[side][pos.epSquare] & pos.pieceBB[WPAWN | side] & noPin;
			while (fromBB)
			{
				int from = popLsb(&fromBB);
				*movelist++ = makeMove(from, checker + S2MSIGN(side) * 8, ENPASSANT);
			}
		}

		//try to block the checker

		while (targets)
		{
			int to = popLsb(&targets);
			fromBB = pos.attackersTo(to, side, true) & noPin;
			while (fromBB)
			{
				int from = popLsb(&fromBB);
				if (pos.mailbox[from] <= BPAWN && PROMOTERANK(to)) // a little bit of a hack because from will not be blank
                {
                    *movelist++ = makePromotionMove(from, to, QUEEN, PROMOTION);
                    *movelist++ = makePromotionMove(from, to, KNIGHT, PROMOTION);
                    *movelist++ = makePromotionMove(from, to, BISHOP, PROMOTION);
                    *movelist++ = makePromotionMove(from, to, ROOK, PROMOTION);
                }
                else
                    *movelist++ = makeMove(from, to, NORMAL);
			}
		}
	}
	return movelist;
}

template <Color side> SMove * generate_quiet_checks(const Position &pos, SMove *movelist)
{
    constexpr Color opponent = (side == WHITE) ? BLACK : WHITE;
    int myKing = pos.kingpos[side];
    int kingSquare = pos.kingpos[~side];
    U64 occ = (pos.occupiedBB[0] | pos.occupiedBB[1]);

    U64 myPieces = pos.occupiedBB[side];
    U64 diagPinned = pos.blockersForKing[side][0] & myPieces;
    U64 straightPinned = pos.blockersForKing[side][1] & myPieces;

    U64 notPinned = ~(diagPinned | straightPinned);
    U64 emptySquares = ~occ;

    U64 knightSquares = PseudoAttacks[KNIGHT][kingSquare] & emptySquares;
    U64 bishopSquares = bishopAttacks(occ, kingSquare) & emptySquares;
    U64 rookSquares = rookAttacks(occ, kingSquare) & emptySquares;
    U64 queenSquares = bishopSquares | rookSquares;

    ///Quiet checks from non-pinned pieces

    movelist = generatePieceMoves <KNIGHT> (pos, movelist, occ, knightSquares, notPinned);
    movelist = generatePieceMoves <BISHOP> (pos, movelist, occ, bishopSquares, notPinned);
    movelist = generatePieceMoves <ROOK> (pos, movelist, occ, rookSquares, notPinned);
    movelist = generatePieceMoves <QUEEN> (pos, movelist, occ, queenSquares, notPinned);
    movelist = generatePawnMoves <side, QUIET_CHECK> (pos, movelist, notPinned);

    /// Quiet checks from pinned pieces

    int from;
	U64 targetMask;

    while (diagPinned)
    {
        from = popLsb(&diagPinned);
        targetMask = RAY_MASKS[myKing][from] & emptySquares;
        const PieceType pt = PieceType(pos.mailbox[from] >> 1);
        if (pt == BISHOP)
            movelist = generatePieceMoves <BISHOP> (pos, movelist, occ, targetMask & bishopSquares, BITSET(from));
        else if (pt == QUEEN)
            movelist = generatePieceMoves <QUEEN> (pos, movelist, occ, targetMask & queenSquares, BITSET(from));
    }

    while (straightPinned)
    {
        from = popLsb(&straightPinned);
        targetMask = RAY_MASKS[myKing][from] & emptySquares;
        const PieceType pt = PieceType(pos.mailbox[from] >> 1);
        if (pt == ROOK)
            movelist = generatePieceMoves <ROOK> (pos, movelist, occ, targetMask & rookSquares, BITSET(from));
        else if (pt == QUEEN)
            movelist = generatePieceMoves <QUEEN> (pos, movelist, occ, targetMask & queenSquares, BITSET(from));
        else if (pt == PAWN && (FILE(myKing) == FILE(from)))
        {
            //generate pawn pushes
            movelist = generatePawnMoves <side, QUIET_CHECK> (pos, movelist, BITSET(from));
        }
    }


   ///discovered checks

   U64 fossils = (pos.blockersForKing[opponent][0] | pos.blockersForKing[opponent][1]) & myPieces;
   U64 nonpinnedFossils = fossils & notPinned;
   U64 pinnedFossils = fossils ^ nonpinnedFossils;

   while (nonpinnedFossils)
   {
        from = popLsb(&nonpinnedFossils);
        const int pt = (pos.mailbox[from] >> 1);
        targetMask = ~RAY_MASKS[kingSquare][from] & emptySquares;

        switch (pt)
        {
        case KNIGHT:
            movelist = generatePieceMoves<KNIGHT>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case BISHOP:
            movelist = generatePieceMoves<BISHOP>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case ROOK:
            movelist = generatePieceMoves<ROOK>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case QUEEN:
            movelist = generatePieceMoves<QUEEN>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case KING:
            movelist = generatePieceMoves<KING>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case PAWN:
            movelist = generatePawnMoves <side, QUIET> (pos, movelist, BITSET(from), targetMask);
            break;
        }
   }

   while (pinnedFossils)
   {
        from = popLsb(&pinnedFossils);
        const int pt = (pos.mailbox[from] >> 1);
        //pieces that are pinned to both kings need to respect alignment with both kings
        targetMask = ~RAY_MASKS[kingSquare][from] & emptySquares & RAY_MASKS[myKing][from];

        switch (pt)
        {
        case KNIGHT:
            movelist = generatePieceMoves<KNIGHT>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case BISHOP:
            movelist = generatePieceMoves<BISHOP>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case ROOK:
            movelist = generatePieceMoves<ROOK>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case QUEEN:
            movelist = generatePieceMoves<QUEEN>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case KING:
            movelist = generatePieceMoves<KING>(pos, movelist, occ, targetMask, BITSET(from));
            break;
        case PAWN:
            movelist = generatePawnMoves <side, QUIET> (pos, movelist, BITSET(from), targetMask);
            break;
        }
   }

    return movelist;
}

template <Color side, MoveType Mt> SMove* generate_legal_moves(const Position &pos, SMove *movelist) // ASSUMING NOT EVASIONS
{

    movelist = generatePinnedMoves<side, Mt>(pos, movelist);
    U64 pinned = (pos.blockersForKing[side][0] | pos.blockersForKing[side][1]) & pos.occupiedBB[side];
    movelist = generate_pseudo_legal_moves<side, Mt>(pos, movelist, ~pinned);

    return movelist;
}

template <MoveType Mt> SMove* generate_all(const Position &pos, SMove *movelist)
{
    return (pos.activeSide) ? generate_legal_moves <BLACK, Mt> (pos, movelist) : generate_legal_moves <WHITE, Mt> (pos, movelist);
}

//Explicit instantiation
template SMove* generate_all<QUIET> (const Position &, SMove *);
template SMove* generate_all<CAPTURE> (const Position &, SMove *);
template SMove* generate_all<PROMOTE> (const Position &, SMove *);
template SMove* generate_all<TACTICAL> (const Position &, SMove *);

template <> SMove* generate_all<EVASION>(const Position &pos, SMove *movelist)
{
    return (pos.activeSide) ? generate_evasions <BLACK> (pos, movelist) : generate_evasions <WHITE> (pos, movelist);
}

template <> SMove* generate_all<QUIET_CHECK>(const Position &pos, SMove *movelist)
{
    return (pos.activeSide) ? generate_quiet_checks <BLACK> (pos, movelist) : generate_quiet_checks <WHITE> (pos, movelist);
}

template <> SMove* generate_all<ALL>(const Position &pos, SMove *movelist)
{
    if(pos.checkBB)
        return generate_all<EVASION>(pos, movelist);
    return (pos.activeSide) ? generate_legal_moves <BLACK, ALL> (pos, movelist) : generate_legal_moves <WHITE, ALL> (pos, movelist);
}

MoveGen::MoveGen(Position *p, SearchType type, Move hshm, int t, int d)
{
    pos = p;
    depth = d;
    threshold = t;

    if (p->checkBB)
    {
        state = EVASIONS_INIT;
        hashmove = MOVE_NONE;
    }
    else
    {
        if(type == NORMAL_SEARCH)
        {
            hashmove = (hshm != MOVE_NONE && pos->isPseudoLegal(hshm) && pos->isLegal(hshm)) ? hshm : MOVE_NONE;
            state = HASHMOVE_STATE;
        }
        else //it's qSearch or probcut
        {
            hashmove = (hshm != MOVE_NONE && pos->isPseudoLegal(hshm) && pos->isLegal(hshm) && pos->isTactical(hshm)) ? hshm : MOVE_NONE;
            state = type == QUIESCENCE_SEARCH ? QUIESCENCE_HASHMOVE : PROBCUT_HASHMOVE;
        }

        if (hashmove == MOVE_NONE)
        {
            state++;
        }
    }

    int prev_to = to_sq(p->moveStack[p->historyIndex]);
    counterMove = p->my_thread->counterMoveTable[p->mailbox[prev_to]][prev_to];
}

inline int quiet_score(Position *pos, searchInfo *info, Move m)
{
    int from = from_sq(m);
    int to = to_sq(m);
    PieceCode pc = pos->mailbox[from];

    return pos->my_thread->historyTable[pos->activeSide][from][to] +
    (*(info-1)->counterMove_history)[pc][to] +
    (*(info-2)->counterMove_history)[pc][to];
}

inline int mvvlva_score(Position *pos, Move m)
{
    return mvvlva[pos->mailbox[to_sq(m)]][pos->mailbox[from_sq(m)]];
}

void MoveGen::scoreMoves(searchInfo *info, ScoreType type)
{
    for (auto &m : *this)
    {
        if (type == SCORE_CAPTURE)
        {
            m.value = mvvlva_score(pos, m.code);
        }
        else if (type == SCORE_QUIET)
        {
            m.value = quiet_score(pos, info, m.code);
        }
        else //evasion
        {
            if (pos->isCapture(m.code))
                m.value = mvvlva_score(pos, m.code);
            else
               m.value = quiet_score(pos, info, m.code) - (1<<20);
        }
        if (type_of(m.code) == PROMOTION)
            m.value += mvvlva[make_piece(pos->activeSide, promotion_type(m.code))][pos->mailbox[from_sq(m.code)]];
    }
}

template <PickType type> Move MoveGen::select_move()
{
    if (type == BEST)
        swap(*curr, *max_element(curr, endMoves));

    return *curr++;
}

void insertion_sort(SMove *head, SMove *tail)
{
    for (SMove *i = head + 1; i < tail; i++)
    {
        SMove tmp = *i, *j;
        for (j = i; j != head && *(j-1) < tmp; --j)
        {
            *j = *(j - 1);
        }
        *j = tmp;
    }
}

Move MoveGen::next_move(searchInfo *info, bool skipQuiets)
{
    Move m;
    switch (state)
    {
        case HASHMOVE_STATE:
            ++state;
            return hashmove;
        case TACTICAL_INIT:
            curr = endBadCaptures = moveList;
            endMoves = generate_all<TACTICAL>(*pos, curr);
            scoreMoves(info, SCORE_CAPTURE);
            ++state;
            /* fallthrough */
        case TACTICAL_STATE:
            while (curr < endMoves)
            {
                m = select_move<BEST>();
                if (m == hashmove)
                    continue;

                if (pos->see(m, 0))
                    return m;

                *endBadCaptures++ = m;
            }
            ++state;
            m = info->killers[0];

            if (m != MOVE_NONE && m != hashmove && !pos->isTactical(m) && pos->isPseudoLegal(m) && pos->isLegal(m))
            {
                return m;
            }
            /* fallthrough */


        case KILLER_MOVE_2:
            ++state;
            m = info->killers[1];
            if (m != MOVE_NONE && m != hashmove && !pos->isTactical(m) && pos->isPseudoLegal(m) && pos->isLegal(m))
            {
                return m;
            }
            /* fallthrough */

        case COUNTER_MOVE:
            ++state;
            m = counterMove;
            if (m != MOVE_NONE &&
                m != hashmove &&
                m != info->killers[0] &&
                m != info->killers[1] &&
                !pos->isTactical(m) &&
                pos->isPseudoLegal(m) && pos->isLegal(m))
            {
                return m;
            }
            /* fallthrough */

        case QUIETS_INIT:
            if (!skipQuiets)
            {
                curr = endBadCaptures;
                endMoves = generate_all<QUIET>(*pos, curr);
                scoreMoves(info, SCORE_QUIET);
                insertion_sort(curr, endMoves);
            }
            ++state;
            /* fallthrough */
        case QUIET_STATE:
            if (!skipQuiets)
            {
                while (curr < endMoves)
                {
                    m = select_move<NEXT>();

                    if (m != hashmove &&
                        m != info->killers[0] &&
                        m != info->killers[1] &&
                        m != counterMove)
                        {
                            return m;
                        }
                }
            }
            ++state;
            curr = moveList;
            endMoves = endBadCaptures;
            /* fallthrough */

        case BAD_TACTICAL_STATE:
            while (curr < endMoves)
            {
                m = select_move<NEXT>();
                if (m != hashmove)
                    return m;
            }
            break;

        case EVASIONS_INIT:
            curr = moveList;
            endMoves = generate_all<EVASION>(*pos, curr);
            scoreMoves(info, SCORE_EVASION);
            ++state;
            /* fallthrough */

        case EVASIONS_STATE:
            while (curr < endMoves)
            {
                return select_move<BEST>();
            }
            break;

        case QUIESCENCE_HASHMOVE:
            ++state;
            return hashmove;

        case QUIESCENCE_CAPTURES_INIT:
            curr = endBadCaptures = moveList;

            endMoves = generate_all<TACTICAL>(*pos, curr);
            scoreMoves(info, SCORE_CAPTURE);
            ++state;
            /* fallthrough */

        case QUIESCENCE_CAPTURES:
            while (curr < endMoves)
            {
                m = select_move<BEST>();
                if (m != hashmove)
                {
                    return m;
                }
            }
            if (depth != 0)
                break;
            ++state;
            /* fallthrough */
        case QUIESCENCE_CHECKS_INIT:
            curr = moveList;
            endMoves = generate_all <QUIET_CHECK> (*pos, curr);
            ++state;
            /* fallthrough */
        case QUIESCENCE_CHECKS:
            while (curr < endMoves)
            {
                m = select_move<NEXT>();
                if (m != hashmove)
                    return m;
            }
            break;


        case PROBCUT_HASHMOVE:
            ++state;
            return hashmove;

        case PROBCUT_CAPTURES_INIT:
            curr = endBadCaptures = moveList;
            endMoves = generate_all<TACTICAL>(*pos, curr);
            scoreMoves(info, SCORE_CAPTURE);
            ++state;
            /* fallthrough */

        case PROBCUT_CAPTURES:
            while (curr < endMoves)
            {
                m = select_move<BEST>();
                if (m != hashmove && pos->see(m, threshold))
                    return m;
            }
            break;

    }
    return MOVE_NONE;
}
