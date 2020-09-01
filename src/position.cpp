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

void Position::readFEN(const char* fen)
{
    string temp;
    vector<string> parts = SplitString(fen);
    for (int i = 0; i < 14; i++)
    {
        pieceBB[i] = 0ULL;
        pieceCount[i] = 0;
    }
    for (int i = 0; i < 64; i++)
        mailbox[i] = BLANK;
    for (int i = 0; i < 2; i++)
        occupiedBB[i] = 0ULL;

    temp = parts[0];
    int rank = 7;
    int file = 0; // starting states for FEN
    nonPawn[0] = nonPawn[1] = 0;
    psqt_score = S(0, 0);
    halfmoveClock = 0;
    fullmoveClock = 0;

    for (size_t i = 0; i < temp.length(); i++)
    {
        PieceCode p;
        int num = 1;
        int index = INDEX(rank, file);
        char c = temp[i];
        switch (c)
        {
        case 'k':
            p = BKING;
            kingpos[1] = index;
            break;
        case 'q':
            p = BQUEEN;
            break;
        case 'r':
            p = BROOK;
            break;
        case 'b':
            p = BBISHOP;
            break;
        case 'n':
            p = BKNIGHT;
            break;
        case 'p':
            p = BPAWN;
            break;
        case 'K':
            p = WKING;
            kingpos[0] = index;
            break;
        case 'Q':
            p = WQUEEN;
            break;
        case 'R':
            p = WROOK;
            break;
        case 'B':
            p = WBISHOP;
            break;
        case 'N':
            p = WKNIGHT;
            break;
        case 'P':
            p = WPAWN;
            break;
        case '/':
            rank--;
            num = 0;
            file = 0;
            break;
        default:
            num = 0;
            file += (c - '0');
            break;
        }
        if (num)
        {
            set_piece_at(index, p);
            file++;
        }
    }

    castleRights = 0;
    activeSide = WHITE;
    if (parts[1] == "b")
        activeSide = BLACK;

    temp = parts[2];
    const string castles = "QKqk";
    for (size_t i = 0; i < temp.length(); i++)
    {
        char c = temp[i];
        size_t castleindex;
        if ((castleindex = castles.find(c)) != string::npos)
            castleRights |= WQCMASK << castleindex;
    }

    epSquare = 0;
    temp = parts[3];
    if (temp.length() == 2)
        epSquare = AlgebraicToIndex(temp);

    if (parts.size() > 4)
        halfmoveClock = stoi(parts[4]);

    if (parts.size() > 5)
        fullmoveClock = stoi(parts[5]);

    key = zb.getHash(this);
    pawnhash = zb.getPawnHash(this);
    materialhash = 0ULL;

    for (int i = WPAWN; i <= BKING; i++)
        for (int j = 0; j < pieceCount[i]; j++)
            materialhash ^= zb.pieceKeys[(j << 4) | i];

    historyIndex = 0;
    capturedPiece = BLANK;
    checkBB = attackersTo(kingpos[activeSide], activeSide ^ SIDESWITCH);
    updateBlockers();
}

ostream& operator<<(ostream& os, const Position& pos) {
    os << "\n +---+---+---+---+---+---+---+---+\n";
    const string Piece2Char   ("  PpNnBbRrQqKk");

    for (int r = 7; r >= 0; --r)
    {
        for (int f = 0; f <= 7; ++f)
        {
            char c = Piece2Char[pos.mailbox[INDEX(r, f)]];
            os << " | " << c;
        }

        os << " |\n +---+---+---+---+---+---+---+---+\n";
    }
    os << "\nKey: " << pos.key << "\nPawn Key: " << pos.pawnhash << "\nMaterial Key: " << pos.materialhash << endl;

    return os;
}

bool Position::isAttacked(int sq, int side) const //isAttacked by side
{
    U64 occ = occupiedBB[0] | occupiedBB[1];
    return PAWN_ATTACKSFROM[side][sq] & pieceBB[WPAWN | side]
        || PseudoAttacks[KNIGHT][sq] & pieceBB[WKNIGHT | side]
        || PseudoAttacks[KING][sq] & pieceBB[WKING | side]
        || bishopAttacks(occ, sq) & (pieceBB[WBISHOP | side] | pieceBB[WQUEEN | side])
        || rookAttacks(occ, sq) & (pieceBB[WROOK | side] | pieceBB[WQUEEN | side]);
}

U64 Position::attackersTo(int sq, int side, bool free) const // attackers from side
{
    U64 occ = occupiedBB[0] | occupiedBB[1];
    return ((free ? (PAWN_PUSHESFROM[side][sq] | (PAWN_2PUSHESFROM[side][sq] & PAWNPUSH(side ^ SIDESWITCH, ~occ))) : (PAWN_ATTACKSFROM[side][sq])) & pieceBB[WPAWN | side])
        | (PseudoAttacks[KNIGHT][sq] & pieceBB[WKNIGHT | side])
        | (bishopAttacks(occ, sq) & (pieceBB[WBISHOP | side] | pieceBB[WQUEEN | side]))
        | (rookAttacks(occ, sq) & (pieceBB[WROOK | side] | pieceBB[WQUEEN | side]));
}

U64 Position::attackers_to(int sq, int side, U64 occ) const // attackers from side w/ custom occ
{
    return (PAWN_ATTACKSFROM[side][sq] & pieceBB[WPAWN | side])
        | (PseudoAttacks[KNIGHT][sq] & pieceBB[WKNIGHT | side])
        | (bishopAttacks(occ, sq) & (pieceBB[WBISHOP | side] | pieceBB[WQUEEN | side]))
        | (rookAttacks(occ, sq) & (pieceBB[WROOK | side] | pieceBB[WQUEEN | side]));
}

U64 Position::all_attackers_to(int sq, U64 occ) const // attackers from side w/ custom occ
{
    return (PAWN_ATTACKSFROM[WHITE][sq] & (pieceBB[WPAWN])) | (PAWN_ATTACKSFROM[BLACK][sq] & (pieceBB[BPAWN]))
        | (PseudoAttacks[KNIGHT][sq] & (pieceBB[WKNIGHT] | pieceBB[BKNIGHT]))
        | (bishopAttacks(occ, sq) & (pieceBB[WBISHOP] | pieceBB[WQUEEN] | pieceBB[BBISHOP] | pieceBB[BQUEEN]))
        | (rookAttacks(occ, sq) & (pieceBB[WROOK] | pieceBB[WQUEEN] | pieceBB[BROOK] | pieceBB[BQUEEN]));
}

U64 Position::getAttackSet(int sq, U64 occ) const
{
    int pt = mailbox[sq] >> 1;
    switch (pt)
    {
    case KNIGHT:
        return PseudoAttacks[KNIGHT][sq];
    case KING:
        return PseudoAttacks[KING][sq];
    case BISHOP:
        return bishopAttacks(occ, sq);
    case ROOK:
        return rookAttacks(occ, sq);
    case QUEEN:
        return bishopAttacks(occ, sq) | rookAttacks(occ, sq);
    }
    return 0;
}

U64 Position::tabooSquares() const //gets all squares attacked by opponent (including xray through king)
{
    int side = activeSide;
    int opponent = side ^ SIDESWITCH;
    U64 occ = (occupiedBB[0] | occupiedBB[1]) ^ BITSET(kingpos[side]); // no king
    int from;

    U64 out = PAWNATTACKS(opponent, pieceBB[WPAWN | opponent]);
    U64 knights = pieceBB[(WKNIGHT | opponent)];

    while (knights)
    {
        from = popLsb(&knights);
        out |= PseudoAttacks[KNIGHT][from];
    }

    out |= PseudoAttacks[KING][kingpos[opponent]];

    U64 bishopsQueens = (pieceBB[WBISHOP | opponent] | pieceBB[WQUEEN | opponent]);
    while (bishopsQueens)
    {
        from = popLsb(&bishopsQueens);
        out |= bishopAttacks(occ, from);
    }
    U64 rooksQueens = (pieceBB[WROOK | opponent] | pieceBB[WQUEEN | opponent]);
    while (rooksQueens)
    {
        from = popLsb(&rooksQueens);
        out |= rookAttacks(occ, from);
    }

    return out;
}

void Position::set_piece_at(int sq, PieceCode pc)
{
    int side = pc & 0x01;
    PieceCode current = mailbox[sq];
    if (current)
        remove_piece_at(sq, current);
    mailbox[sq] = pc;
    pieceBB[pc] |= SQUARE_MASKS[sq];
    occupiedBB[side] |= SQUARE_MASKS[sq];
    key ^= zb.pieceKeys[(sq << 4) | pc];
    psqt_score += psq.psqt[pc][sq];
    nonPawn[side] += (nonPawnValue[pc]);
    pieceCount[pc]++;
}

void Position::remove_piece_at(int sq, PieceCode pc)
{
    int side = pc & 0x01;
    mailbox[sq] = BLANK;
    pieceBB[pc] ^= SQUARE_MASKS[sq];
    occupiedBB[side] ^= SQUARE_MASKS[sq];
    key ^= zb.pieceKeys[(sq << 4) | pc];
    psqt_score -= psq.psqt[pc][sq];
    nonPawn[side] -= (nonPawnValue[pc]);
    pieceCount[pc]--;
}

void Position::move_piece(int from, int to, PieceCode pc)
{
    remove_piece_at(from, pc);
    set_piece_at(to, pc);
}

void Position::init()
{
    init_boards();
    init_threads();
    init_values();
}

void Position::do_null_move()
{
    memcpy(&historyStack[historyIndex++], &key, sizeof(stateHistory));
    moveStack[historyIndex] = MOVE_NULL;
    halfmoveClock++;

    int eptnew = 0;
    key ^= zb.epSquares[epSquare];
    epSquare = eptnew;
    key ^= zb.epSquares[epSquare];
    activeSide = ~activeSide;
    key ^= zb.activeSide;
}

void Position::undo_null_move()
{
    historyIndex--;
    memcpy(&key, &historyStack[historyIndex], sizeof(stateHistory));
    activeSide = ~activeSide;
}

void Position::do_move(Move m)
{
    memcpy(&historyStack[historyIndex++], &key, sizeof(stateHistory));
    moveStack[historyIndex] = m;
    halfmoveClock++;
    SpecialType type = type_of(m);
    capturedPiece = BLANK;
    int eptnew = 0;
    uint8_t oldCastle = castleRights;
    Color side = activeSide;
    Color opponent = ~side;

    if (type == CASTLING)
    {
        int kingFrom = from_sq(m);
        int rookFrom = to_sq(m);
        int castleType = 2 * side + ((rookFrom > kingFrom) ? 1 : 0);
        int kingTo = castleKingTo[castleType];
        int rookTo = castleRookTo[castleType];

        PieceCode kingpc = (PieceCode)(WKING | side);
        PieceCode rookpc = (PieceCode)(WROOK | side);

        kingpos[side] = kingTo;
        move_piece(kingFrom, kingTo, kingpc);
        move_piece(rookFrom, rookTo, rookpc);
        pawnhash ^= zb.pieceKeys[(kingFrom << 4) | kingpc] ^ zb.pieceKeys[(kingTo << 4) | kingpc];

        castleRights &= (side ? ~(BQCMASK | BKCMASK) : ~(WQCMASK | WKCMASK)); //clear castling rights
    }
    else
    {
        int from = from_sq(m);
        int to = to_sq(m);
        PieceCode pc = mailbox[from];
        PieceType pt = PieceType(pc >> 1);
        capturedPiece = (type == ENPASSANT) ? make_piece(opponent, PAWN) : mailbox[to];

        if (type == PROMOTION)
        {
            PieceCode promote = make_piece(side, promotion_type(m));
            set_piece_at(to, promote);
            remove_piece_at(from, pc);
            pawnhash ^= zb.pieceKeys[(from << 4) | pc];
            materialhash ^= zb.pieceKeys[(pieceCount[pc] << 4) | pc] ^ zb.pieceKeys[((pieceCount[promote] - 1) << 4) | promote];
        }
        else
        {
            move_piece(from, to, pc);

            if (pt == PAWN)
            {
                if ((to ^ from) == 16 && (epthelper[to] & pieceBB[WPAWN | (opponent)])) // double push w/ possible EP
                    eptnew = (to + from) / 2;

                pawnhash ^= zb.pieceKeys[(from << 4) | pc] ^ zb.pieceKeys[(to << 4) | pc];
                halfmoveClock = 0;
            }

            else if (pt == KING)
            {
                kingpos[side] = to;
                pawnhash ^= zb.pieceKeys[(from << 4) | pc] ^ zb.pieceKeys[(to << 4) | pc];
            }
        }

        if (capturedPiece != BLANK)
        {
            if (capturedPiece >> 1 == PAWN)
            {
                if (type == ENPASSANT)
                {
                    int capturesquare = (from & 0x38) | (to & 0x07);
                    remove_piece_at(capturesquare, capturedPiece);
                    pawnhash ^= zb.pieceKeys[(capturesquare << 4) | capturedPiece]; //remove the captured pawn from pawnhash
                    pawnhash ^= zb.pieceKeys[(from << 4) | pc] ^ zb.pieceKeys[(to << 4) | pc]; // move the moving pawn
                }
                else
                    pawnhash ^= zb.pieceKeys[(to << 4) | capturedPiece];
            }
            halfmoveClock = 0;
            materialhash ^= zb.pieceKeys[(pieceCount[capturedPiece] << 4) | capturedPiece];
        }
        castleRights &= (castlerights[from] & castlerights[to]);
    }

    PREFETCH(&my_thread->pawntable[pawnhash & PAWN_HASH_SIZE_MASK]);

    activeSide = ~activeSide;
    if (!activeSide)
        fullmoveClock++;

    key ^= zb.activeSide;
    key ^= zb.epSquares[epSquare];
    epSquare = eptnew;
    key ^= zb.epSquares[epSquare];
    key ^= zb.castle[oldCastle] ^ zb.castle[castleRights];

    PREFETCH(&TT.table[key & TT.size_mask]);

    checkBB = attackersTo(kingpos[activeSide], activeSide ^ SIDESWITCH);
    updateBlockers();
}

void Position::undo_move(Move m)
{
    SpecialType type = type_of(m);
    activeSide = ~activeSide;
    Color side = activeSide;
    Color opponent = ~side;

    if (type == CASTLING)
    {
        int kingFrom = from_sq(m);
        int rookFrom = to_sq(m);
        int castleType = 2 * side + ((rookFrom > kingFrom) ? 1 : 0);
        int kingTo = castleKingTo[castleType];
        int rookTo = castleRookTo[castleType];
        PieceCode kingpc = (PieceCode)(WKING | side);
        PieceCode rookpc = (PieceCode)(WROOK | side);

        move_piece(kingTo, kingFrom, kingpc);
        move_piece(rookTo, rookFrom, rookpc);
        kingpos[side] = kingFrom;
    }
    else
    {
        int from = from_sq(m);
        int to = to_sq(m);
        PieceCode pc = mailbox[to];
        PieceType pt = PieceType(pc >> 1);
        PieceCode capture = capturedPiece;

        if (type == PROMOTION)
        {
            remove_piece_at(to, (PieceCode)((promotion_type(m) << 1) | side));
            set_piece_at(from, (PieceCode)(WPAWN | side));
        }
        else
        {
            move_piece(to, from, pc);
            if (pt == KING)
                kingpos[side] = from;
        }

        if (capture != BLANK)
        {
            if (type == ENPASSANT)
            {
                int capturesquare = (from & 0x38) | (to & 0x07);
                set_piece_at(capturesquare, make_piece(opponent, PAWN));
            }
            else
            {
                set_piece_at(to, capture);
            }
        }
    }
    historyIndex--;
    memcpy(&key, &historyStack[historyIndex], sizeof(stateHistory));
}

bool Position::isCapture(Move m) const
{
    return mailbox[to_sq(m)];
}

bool Position::isTactical(Move m) const
{
    return mailbox[to_sq(m)] || type_of(m) == PROMOTION;
}

bool Position::isPseudoLegal(const Move m) const
{
    Color side = activeSide;
    int from = from_sq(m);
    int to = to_sq(m);
    PieceCode pc = mailbox[from_sq(m)];

    if (type_of(m) != NORMAL)
        return MoveList<ALL>(*this).contains(m);

    if (promotion_type(m) - KNIGHT != BLANKTYPE)
        return false;

    if (pc < WPAWN || (pc & SIDESWITCH) != side)
        return false;

    if (occupiedBB[side] & BITSET(to))
        return false;

    if ((pc >> 1) == PAWN)
    {
        if ((Rank8BB | Rank1BB) & BITSET(to))
            return false;

        if (!(PAWN_ATTACKS[side][from] & occupiedBB[~side] & BITSET(to)) // Not a capture
            && !((from + pawn_push(side) == to) && !mailbox[to])       // Not a single push
            && !((from + 2 * pawn_push(side) == to)              // Not a double push
                && (RRANK(from, side) == 1)
                && !mailbox[to]
                && !mailbox[(to - pawn_push(side))]))
            return false;
    }
    else if (!(getAttackSet(from, (occupiedBB[0] | occupiedBB[1])) & BITSET(to)))
        return false;

    if (checkBB)
    {
        if ((pc >> 1) != KING)
        {
            if (!ONEORZERO(checkBB))
                return false;

            if (!(((BETWEEN_MASKS[LSB(checkBB)][kingpos[side]]) | checkBB) & BITSET(to)))
                return false;
        }

        else if (attackers_to(to, ~side, ((occupiedBB[0] | occupiedBB[1]) ^ BITSET(from))))
            return false;
    }

    return true;
}

bool Position::isLegal(const Move m) const
{
    Color side = activeSide;
    int from = from_sq(m);
    int to = to_sq(m);
    int kingSq = kingpos[side];

    if (type_of(m) == ENPASSANT)
    {
        int captured = to - pawn_push(side);
        U64 occ = (occupiedBB[0] | occupiedBB[1]) ^ BITSET(from) ^ BITSET(to) ^ BITSET(captured);
        U64 bishops = pieceBB[make_piece(~side, BISHOP)] | pieceBB[make_piece(~side, QUEEN)];
        U64 rooks = pieceBB[make_piece(~side, ROOK)] | pieceBB[make_piece(~side, QUEEN)];

        return !(rookAttacks(occ, kingSq) & rooks) && !(bishopAttacks(occ, kingSq) & bishops);
    }

    if (type_of(m) == CASTLING)
    {
        int castleType = 2 * side + ((to > from) ? 1 : 0);
        to = castleKingTo[castleType];
        U64 attackTargets = castlekingwalk[castleType];
        while (attackTargets)
        {
            if (isAttacked(popLsb(&attackTargets), ~side))
                return false;
        }
    }

    if (mailbox[from] >= WKING)
    {
        return !(isAttacked(to, ~side));
    }


    return !(BITSET(from) & (blockersForKing[side][0] | blockersForKing[side][1]))
        || (BITSET(from) & RAY_MASKS[to][kingSq]);
}

bool Position::pawnOn7th() const
{
    U64 pawns = pieceBB[WPAWN | activeSide];
    return pawns & ((activeSide) ? Rank2BB : Rank7BB);
}

bool Position::advanced_pawn_push(Move m) const
{
    PieceCode pc = mailbox[from_sq(m)];
    int to = to_sq(m);
    return (pc <= BPAWN &&
            (RRANK(to, activeSide) == 6 ||
             (RRANK(to, activeSide) == 5 && mailbox[PAWNPUSHINDEX(activeSide, to)] == BLANK)
             )
            );
}

bool Position::givesCheck(Move m) //assumes move is legal
{
    Color opponent = ~activeSide;
    int opponentKing_square = kingpos[opponent];
    U64 opponentKing = BITSET(opponentKing_square);
    SpecialType mtype = type_of(m);

    int from = from_sq(m);
    int to = to_sq(m);
    PieceCode pc = mailbox[from];
    PieceType type = PieceType(pc >> 1);
    U64 occ = occupiedBB[0] | occupiedBB[1];

    switch (type)
    {
    case PAWN:
        if (PAWN_ATTACKS[activeSide][to] & opponentKing)
            return true;
        if (!(opponentKing & RAY_MASKS[from][to]) && (blockersForKing[opponent][0] | blockersForKing[opponent][1]) & BITSET(from))
            return true;
        if (mtype == PROMOTION)
        {
            PieceType promotion = promotion_type(m);
            switch (promotion)
            {
            case QUEEN:
                return (opponentKing & (rookAttacks(occ, to) | bishopAttacks(occ, to)));
            case ROOK:
                return (opponentKing & (rookAttacks(occ, to)));
            case BISHOP:
                return (opponentKing & (bishopAttacks(occ, to)));
            case KNIGHT:
                return (opponentKing & (PseudoAttacks[KNIGHT][to]));
            default:
                return false;
            }
        }
        if (mtype == ENPASSANT)
        {
            do_move(m);
            if (checkBB)
            {
                undo_move(m);
                return true;
            }
            undo_move(m);
        }
        break;

    case KING:
        if (!(opponentKing & RAY_MASKS[from][to]) && (blockersForKing[opponent][0] | blockersForKing[opponent][1]) & BITSET(from))
            return true;

        if (mtype == CASTLING)
        {
            do_move(m);
            if (checkBB)
            {
                undo_move(m);
                return true;
            }
            undo_move(m);
        }
        break;

    case QUEEN:
        if (opponentKing & (rookAttacks(occ, to) | bishopAttacks(occ, to)))
            return true;
        if (!(opponentKing & RAY_MASKS[from][to]) && (blockersForKing[opponent][0] | blockersForKing[opponent][1]) & BITSET(from))
            return true;

        break;

    case ROOK:
        if (opponentKing & (rookAttacks(occ, to)))
            return true;
        if (!(opponentKing & RAY_MASKS[from][to]) && (blockersForKing[opponent][0] | blockersForKing[opponent][1]) & BITSET(from))
            return true;

        break;

    case BISHOP:
        if (opponentKing & (bishopAttacks(occ, to)))
            return true;
        if (!(opponentKing & RAY_MASKS[from][to]) && (blockersForKing[opponent][0] | blockersForKing[opponent][1]) & BITSET(from))
            return true;

        break;

    case KNIGHT:
        if (opponentKing & PseudoAttacks[KNIGHT][to])
            return true;
        if (!(opponentKing & RAY_MASKS[from][to]) && (blockersForKing[opponent][0] | blockersForKing[opponent][1]) & BITSET(from))
            return true;

        break;

    default:
        return false;
    }
    return false;
}


int Position::smallestAttacker(U64 attackers, Color color) const
{
    for (int piece = WPAWN + color; piece <= BKING; piece += 2)
    {
        U64 intersection = pieceBB[piece] & attackers;
        if (intersection)
            return LSB(intersection);
    }
    return 0;
}


bool Position::see(Move m, int threshold) const
{
    int from = from_sq(m);
    int to = to_sq(m);
    int value = (pieceValues[MG][mailbox[to]] + ((type_of(m) == PROMOTION) ? (pieceValues[MG][promotion_type(m) << 1] - PAWN_MG) : 0)) - threshold;

    //cout << value<<endl;
    if (value < 0)
        return false;
    int nextPiece = (type_of(m) == PROMOTION) ? promotion_type(m) << 1 : mailbox[from];

    value -= pieceValues[MG][nextPiece];

    //cout << value<<endl;
    if (value >= 0)
        return true;

    U64 occ = ((occupiedBB[0] | occupiedBB[1]) ^ BITSET(from)) | BITSET(to);
    U64 rooks = (pieceBB[WROOK] | pieceBB[BROOK] | pieceBB[WQUEEN] | pieceBB[BQUEEN]);
    U64 bishops = (pieceBB[WBISHOP] | pieceBB[BBISHOP] | pieceBB[WQUEEN] | pieceBB[BQUEEN]);

    U64 attackers = all_attackers_to(to, occ) & occ;
    Color activeSide_temp = ~activeSide;

    while (true)
    {
        U64 my_attackers = attackers & occupiedBB[activeSide_temp];
        if (!my_attackers)
            break;

        int sq = smallestAttacker(my_attackers, activeSide_temp);
        //cout <<"smallest attacker on "<<sq<<endl;
        occ ^= BITSET(sq);
        int attackerType = mailbox[sq] >> 1;
        if (attackerType == PAWN || attackerType == BISHOP || attackerType == QUEEN)
        {
            attackers |= bishopAttacks(occ, to) & bishops;
        }

        if (attackerType == ROOK || attackerType == QUEEN)
        {
            attackers |= rookAttacks(occ, to) & rooks;
        }

        attackers &= occ;
        //printBits(attackers);

        activeSide_temp = ~activeSide_temp;
        value = -value - 1 - pieceValues[MG][mailbox[sq]];
        //cout << value<<endl;

        if (value >= 0)
            break;
    }

    return activeSide_temp ^ activeSide;
}

bool Position::testRepetition() const
{
    int minIndex = max(historyIndex - halfmoveClock, 0);
    for (int i = historyIndex - 4; i >= minIndex; i -= 2)
    {
        if (key == historyStack[i].key)
        {
            return true;
        }
    }
    return false;
}

zobrist zb;
PSQT psq;
SearchThread main_thread;
SearchThread* search_threads;

Position* start_position()
{
    Position* p = &main_thread.position;
    p->readFEN(STARTFEN);
    p->my_thread = &main_thread;
    return p;
}

Position* import_fen(const char* fen, int thread_id)
{
    SearchThread* t = get_thread(thread_id);
    Position* p = &t->position;
    p->readFEN(fen);
    p->my_thread = t;
    return p;
}
