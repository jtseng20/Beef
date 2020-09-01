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

/// Code adapted from Ethereal

unsigned TB_PROBE_DEPTH;
extern int TB_LARGEST;
extern volatile bool ANALYSISMODE;

static Move convertPyrrhicMove(Position *pos, unsigned result) {

    // Extract Pyrhic's move representation
    unsigned to    = TB_GET_TO(result);
    unsigned from  = TB_GET_FROM(result);
    unsigned ep    = TB_GET_EP(result);
    unsigned promo = TB_GET_PROMOTES(result);

    // Convert the move notation. Care that Pyrrhic's promotion flags are inverted
    if (ep == 0u && promo == 0u) return makeMove(from, to, NORMAL);
    else if (ep != 0u)           return makeMove(from, pos->epSquare, ENPASSANT);
    else /* if (promo != 0u) */  return makePromotionMove(from, to, PieceType(6 - promo), PROMOTION);
}

bool tablebasesProbeDTZ(Position *pos, Move *best, Move *ponder) {

    unsigned results[255];
    uint64_t white = pos->occupiedBB[WHITE];
    uint64_t black = pos->occupiedBB[BLACK];

    // We cannot probe when there are castling rights, or when
    // we have more pieces than our largest Tablebase has pieces
    if (   pos->castleRights
        || POPCOUNT(white | black) > TB_LARGEST)
        return false;


    unsigned result = tb_probe_root(
        white,  black,
        (pos->pieceBB[WKING] | pos->pieceBB[BKING]),  (pos->pieceBB[WQUEEN] | pos->pieceBB[BQUEEN]),
        (pos->pieceBB[WROOK] | pos->pieceBB[BROOK]),  (pos->pieceBB[WBISHOP] | pos->pieceBB[BBISHOP]),
        (pos->pieceBB[WKNIGHT] | pos->pieceBB[BKNIGHT]),  (pos->pieceBB[WPAWN] | pos->pieceBB[BPAWN]),
        pos->halfmoveClock, pos->epSquare,
        pos->activeSide == WHITE ? 1 : 0, results
    );

    // Probe failed, or we are already in a finished position.
    if (   result == TB_RESULT_FAILED
        || result == TB_RESULT_CHECKMATE
        || result == TB_RESULT_STALEMATE)
        return false;

    // Otherwise, set the best move to any which maintains the WDL
    else {
        *best = convertPyrrhicMove(pos, result);
        *ponder = MOVE_NONE;
    }

    return !ANALYSISMODE;
}

unsigned tablebasesProbeWDL(Position *pos, int depth, int height) {

    uint64_t white = pos->occupiedBB[WHITE];
    uint64_t black = pos->occupiedBB[BLACK];

    // Never take a Syzygy Probe in a Root node, in a node with Castling rights,
    // in a node which was not just zero'ed by a Pawn Move or Capture, or in a
    // node which has more pieces than our largest found Tablebase can handle

    if (   height == 0
        || pos->castleRights
        || pos->halfmoveClock
        || POPCOUNT(white | black) > TB_LARGEST)
        return TB_RESULT_FAILED;


    // We also will avoid probing beneath the provided TB_PROBE_DEPTH, except
    // for when our board has even fewer pieces than the largest Tablebase is
    // able to handle. Namely, when we have a 7man Tablebase, we will always
    // probe the 6man Tablebase if possible, irregardless of TB_PROBE_DEPTH

    if (   depth < (int) TB_PROBE_DEPTH
        && POPCOUNT(white | black) == TB_LARGEST)
        return TB_RESULT_FAILED;


    // Tap into Pyrrhic's API. Pyrrhic takes the board representation, followed
    // by the enpass square (0 if none set), and the turn. Pyrrhic defines WHITE
    // as 1, and BLACK as 0, which is the opposite of how Ethereal defines them

    return tb_probe_wdl(
        white,  black,
        (pos->pieceBB[WKING] | pos->pieceBB[BKING]),  (pos->pieceBB[WQUEEN] | pos->pieceBB[BQUEEN]),
        (pos->pieceBB[WROOK] | pos->pieceBB[BROOK]),  (pos->pieceBB[WBISHOP] | pos->pieceBB[BBISHOP]),
        (pos->pieceBB[WKNIGHT] | pos->pieceBB[BKNIGHT]),  (pos->pieceBB[WPAWN] | pos->pieceBB[BPAWN]),
        pos->epSquare,
        pos->activeSide == WHITE ? 1 : 0
    );
}
