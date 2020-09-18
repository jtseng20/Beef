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

const Score knightMobilityBonus[9] = {
    S(-71, -23), S(-70, -31), S(-50, 8), S(-40, 15),
    S(-30, 22), S(-23, 32), S(-14, 33), S(-4, 35),
    S(2, 22)
};

const Score bishopMobilityBonus[14] = {
    S(-75, -49), S(-43, -50), S(-25, -37), S(-17, -9),
    S(-2, -4), S(7, 3), S(13, 10), S(16, 10),
    S(21, 13), S(22, 20), S(32, 9), S(53, 11),
    S(53, 20), S(68, 14)
};

const Score rookMobilityBonus[15] = {
    S(-145, -167), S(-120, -101), S(-95, -83), S(-93, -64),
    S(-87, -61), S(-92, -34), S(-86, -37), S(-82, -32),
    S(-77, -29), S(-73, -22), S(-73, -18), S(-71, -11),
    S(-72, -2), S(-60, -4), S(-53, -9)
};

const Score queenMobilityBonus[28] = {
    S(1477, -1234), S(17, 108), S(1, 114), S(8, 7),
    S(19, -6), S(28, -23), S(29, 25), S(33, 38),
    S(34, 71), S(39, 75), S(42, 86), S(44, 93),
    S(45, 105), S(50, 103), S(52, 114), S(54, 115),
    S(52, 123), S(55, 125), S(51, 129), S(67, 113),
    S(66, 105), S(108, 76), S(113, 54), S(98, 47),
    S(142, 8), S(176, 44), S(213, -48), S(264, 16)
};

const int PAWN_MG = 107;
const int PAWN_EG = 196;
const int KNIGHT_MG = 306;
const int KNIGHT_EG = 582;
const int BISHOP_MG = 359;
const int BISHOP_EG = 689;
const int ROOK_MG = 677;
const int ROOK_EG = 1170;
const int QUEEN_MG = 1329;
const int QUEEN_EG = 1924;
const int KING_MG = 0;
const int KING_EG = 0;

const Score piece_bonus[7][64] = {
{
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0),
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0),
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0),
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0),
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0),
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0),
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0),
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0)
},
{
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0),
    S(-31,26), S(-7,14), S(-28,14), S(-10,10), S(-14,22), S(-16,12), S(3,4), S(-39,-19),
    S(-28,16), S(-7,6), S(-12,-6), S(-2,-11), S(12,-4), S(-8,-5), S(6,-8), S(-49,-9),
    S(-32,33), S(-11,23), S(-7,-8), S(-1,-13), S(6,-23), S(2,-14), S(2,0), S(-40,1),
    S(-19,54), S(10,33), S(0,9), S(27,-29), S(37,-21), S(16,-13), S(9,17), S(-15,18),
    S(-22,94), S(10,71), S(16,36), S(24,-4), S(45,-22), S(39,8), S(-8,30), S(-5,39),
    S(82,1), S(46,19), S(51,-6), S(66,-51), S(14,-47), S(77,-33), S(-19,3), S(27,-31),
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0)
},
{
    S(-100,-32), S(-8,-42), S(-32,2), S(-1,25), S(10,15), S(3,17), S(-4,-18), S(-57,-40),
    S(-28,-8), S(-13,6), S(-6,0), S(10,24), S(11,30), S(11,11), S(3,24), S(10,15),
    S(-8,-37), S(4,4), S(8,16), S(26,46), S(32,45), S(22,19), S(22,17), S(17,-26),
    S(-9,-10), S(4,28), S(22,46), S(34,60), S(29,64), S(25,58), S(37,34), S(13,8),
    S(4,6), S(23,22), S(39,52), S(37,78), S(41,75), S(48,65), S(43,47), S(33,17),
    S(-57,0), S(-21,32), S(15,58), S(24,51), S(41,48), S(52,61), S(-8,40), S(-13,-12),
    S(-4,-31), S(-16,4), S(13,11), S(23,60), S(10,59), S(23,6), S(-27,11), S(-21,-21),
    S(-236,-66), S(-126,-4), S(-125,38), S(-79,26), S(-37,19), S(-139,17), S(-128,-10), S(-183,-114)
},
{
    S(38,-23), S(33,-5), S(10,2), S(-1,11), S(1,18), S(10,22), S(28,12), S(32,-13),
    S(35,-23), S(34,-4), S(34,1), S(17,19), S(16,24), S(34,0), S(49,12), S(44,-41),
    S(21,1), S(35,9), S(26,27), S(28,35), S(32,33), S(34,25), S(39,10), S(36,15),
    S(4,-2), S(20,16), S(23,27), S(31,33), S(33,35), S(14,37), S(20,24), S(20,-1),
    S(-13,25), S(17,34), S(11,35), S(31,38), S(27,46), S(8,42), S(17,43), S(-16,28),
    S(6,26), S(-1,42), S(21,41), S(12,40), S(4,42), S(26,60), S(-2,50), S(7,35),
    S(-17,33), S(3,46), S(14,34), S(-6,46), S(-9,46), S(-4,44), S(-28,46), S(-20,33),
    S(15,30), S(-16,37), S(-64,34), S(-76,56), S(-103,49), S(-61,35), S(-11,22), S(6,6)
},
{
    S(7,23), S(5,35), S(11,39), S(20,26), S(22,26), S(21,30), S(30,23), S(4,12),
    S(-31,29), S(5,21), S(8,28), S(12,23), S(16,21), S(30,11), S(37,8), S(-35,36),
    S(-8,30), S(5,48), S(1,46), S(15,36), S(18,36), S(15,34), S(43,29), S(6,21),
    S(-5,54), S(-2,73), S(5,73), S(23,60), S(22,58), S(16,60), S(26,57), S(12,51),
    S(10,77), S(34,73), S(47,76), S(79,63), S(71,65), S(63,62), S(60,56), S(30,73),
    S(8,74), S(55,62), S(46,73), S(74,54), S(82,50), S(83,66), S(93,38), S(19,75),
    S(18,57), S(8,65), S(38,56), S(56,62), S(45,67), S(63,42), S(13,62), S(51,55),
    S(34,92), S(24,98), S(-16,109), S(10,94), S(14,99), S(36,103), S(41,103), S(64,101)
},
{
    S(30,-91), S(8,-70), S(17,-91), S(21,-68), S(23,-76), S(2,-71), S(4,-65), S(-18,0),
    S(7,-50), S(26,-63), S(32,-88), S(22,-36), S(23,-42), S(37,-106), S(44,-91), S(30,-68),
    S(10,-24), S(27,-26), S(18,-3), S(16,-14), S(15,-6), S(19,12), S(47,-34), S(29,-16),
    S(6,-3), S(20,13), S(10,11), S(-2,65), S(0,60), S(22,38), S(33,31), S(36,40),
    S(2,24), S(2,55), S(-2,41), S(-8,89), S(-14,116), S(13,90), S(24,94), S(31,71),
    S(-11,25), S(2,23), S(-13,52), S(4,54), S(0,81), S(48,67), S(47,74), S(17,86),
    S(-6,40), S(-52,83), S(0,53), S(-28,103), S(-22,124), S(43,71), S(-14,117), S(64,59),
    S(3,14), S(13,33), S(24,51), S(30,48), S(72,24), S(70,65), S(82,74), S(54,69)
},
{
    S(4,-80), S(31,-44), S(34,-17), S(-32,-19), S(-21,-44), S(-38,6), S(32,-41), S(38,-113),
    S(17,-29), S(-9,-2), S(11,20), S(-49,36), S(-9,26), S(-30,31), S(7,-10), S(22,-46),
    S(-15,-19), S(-9,9), S(36,26), S(3,54), S(34,42), S(11,32), S(10,-2), S(-16,-27),
    S(-44,-14), S(27,21), S(63,54), S(-25,83), S(21,70), S(39,52), S(36,22), S(-64,-14),
    S(29,20), S(39,47), S(60,77), S(-27,89), S(17,80), S(53,87), S(31,59), S(-72,25),
    S(29,14), S(120,57), S(189,59), S(16,68), S(17,70), S(99,93), S(122,81), S(18,16),
    S(102,-41), S(84,50), S(155,34), S(38,38), S(-8,54), S(99,58), S(199,59), S(-103,2),
    S(-156,-151), S(69,-59), S(80,-23), S(74,-6), S(-77,44), S(10,16), S(88,-39), S(-26,-104)
}
};

const int my_pieces[5][5] = {
    {  16                    },
    { 188,  10               },
    {  47, 177, -44          },
    {  31, 195, 163, -85     },
    {  60,   3, -14,-459, -72}
};

const int opponent_pieces[5][5] = {
    {   0                    },
    {  59,   0               },
    {  43,-107,   0          },
    {  93, -26,-128,   0     },
    { 287,   9, 232, 266,   0}
};

const int bishop_pair = 70;

const Score passedRankBonus[8] = {
    S(0, 0), S(0, 0), S(9, 13), S(-12, 12),
    S(13, 72), S(-2, 98), S(67, 149), S(0, 0)
};

const Score passedUnsafeBonus[2][8] = {
    {S(0, 0), S(0, 0), S(-13, -8), S(-8, 54), S(-50, 45), S(8, 97), S(-25, 191), S(0, 0)},
    {S(0, 0), S(0, 0), S(-12, -12), S(-1, 26), S(-22, -37), S(57, -82), S(83, -95), S(0, 0)},
};

const Score passedBlockedBonus[2][8] = {
    {S(0, 0), S(0, 0), S(1, 3), S(-2, 1), S(6, 20), S(11, 72), S(73, 242), S(0, 0)},
    {S(0, 0), S(0, 0), S(-11, 1), S(-10, -23), S(7, -38), S(23, -83), S(145, -121), S(0, 0)},
};

const Score passedFriendlyDistance[8] = {
    S(0, 0), S(5, -2), S(-1, -4), S(10, -12),
    S(15, -14), S(20, -15), S(1, -9), S(0, 0)
};

const Score passedEnemyDistance[8] = {
    S(0, 0), S(-3, 4), S(1, 6), S(-4, 10),
    S(-11, 32), S(-31, 58), S(15, 8), S(0, 0)
};

const Score tarraschRule_enemy = S(-28, 99);
const Score tarraschRule_friendly[8] = {
    S(0, 0), S(0, 0), S(0, 0), S(21, -6),
    S(21, 8), S(-13, 40), S(-32, 62), S(0, 0)
};

const Score kingflankAttack = S(0, 0);
const Score pawnDistancePenalty = S(0, 10);
const Score minorThreat[7] = {
    S(0, 0), S(3, 22), S(33, 33), S(39, 34),
    S(68, 9), S(65, -17), S(287, 932)
};

const Score rookThreat[7] = {
    S(0, 0), S(-1, 25), S(19, 32), S(35, 40),
    S(7, 16), S(87, -40), S(471, 590)
};

const Score kingThreat = S(-12, 53);
const Score kingMultipleThreat = S(-32, 98);
const Score pawnPushThreat = S(20, 21);
const Score safePawnThreat = S(65, 9);
const Score hangingPiece = S(7, 17);
const Score isolated_penalty[2] = { S(7, 26),  S(12, 27) };

const Score isolated_penaltyAH[2] = { S(-8, 27),  S(8, 22) };

const Score isolated_doubled_penalty[2] = { S(12, 31),  S(2, -6) };

const Score isolated_doubled_penaltyAH[2] = { S(-4, 60),  S(4, 39) };

const Score doubled_penalty[2] = { S(42, 22),  S(0, 117) };

const Score doubled_penalty_undefended[2] = { S(-10, 16),  S(-1, 19) };

const Score backward_penalty[2] = { S(-5, -5),  S(-2, 3) };

const Score connected_bonus[2][2][8] = {
{
    {S(0, 0), S(0, 0), S(14, -6), S(18, 0), S(31, 21), S(58, 47), S(274, -2), S(0, 0)},
    {S(0, 0), S(13, -22), S(14, 0), S(37, 31), S(91, 89), S(101, 271), S(-164, 1043), S(0, 0)}
},
{
    {S(0, 0), S(0, 0), S(0, -5), S(-3, -9), S(-1, -4), S(-8, 31), S(0, 0), S(0, 0)},
    {S(0, 0), S(-1, -22), S(3, -3), S(19, 0), S(39, 35), S(113, -57), S(0, 0), S(0, 0)}
},
};

const Score bishopPawns = S(-1, 1);
const Score kingProtector = S(4, -1);
const Score outpostBonus[2][2] = {
    {S(21, 23), S(30, 22)},
    {S(70, 21), S(71, 39)},
};

const Score reachableOutpost[2] = { S(0, 21),  S(18, 25) };

const Score bishopOpposerBonus = S(-1, 7);
const Score trappedBishopPenalty = S(53, 30);
const Score veryTrappedBishopPenalty = S(74, 121);
const Score defendedRookFile = S(1, -33);
const Score rank7Rook = S(5, 39);
const Score rookFile[2] = { S(29, -7),  S(40, 6) };

const Score battery = S(11, -19);


/////////////////////////////////////////////

const int attackerWeights[7] = {0, 0, 37, 19, 14, 0, 0};

const int checkPenalty[7] = {0, 0, 366, 326, 487, 326, 0};

const int unsafeCheckPenalty[7] = {0, 0, 79, 160, 132, 104, 0};

const int queenContactCheck = 186;
const int kingDangerBase = -10;
const int kingringAttack = 52;
const int kingpinnedPenalty = 37;
const int kingweakPenalty = 96;
const int kingShieldBonus = 14;
const int noQueen = 584;


const int kingShield[4][8] = {
    {1, 57, 60, 39, 62, 135, 103, 0},
    {-26, 42, 28, -2, -10, 132, 64, 0},
    {25, 76, 48, 41, 60, 135, 97, 0},
    {5, 25, 15, 7, 17, 50, 63, 0}
};

const int pawnStormBlocked[4][8] = {
    {0, 0, 50, -8, 6, 59, 128, 0},
    {0, 0, 71, 10, 7, 0, 94, 0},
    {0, 0, 98, 37, 33, 50, 154, 0},
    {0, 0, 84, 42, 19, 28, 16, 0}
};

const int pawnStormFree[4][8] = {
    {38, -275, -120, 14, 25, 27, 18, 0},
    {32, -158, -24, 27, 11, 6, 7, 0},
    {21, -125, 9, 29, 20, 20, 18, 0},
    {26, -32, 48, 58, 32, 17, 15, 0}
};

////////////Not modified/////////////////////
int pieceValues[2][14] = {{0, 0, PAWN_MG, PAWN_MG, KNIGHT_MG, KNIGHT_MG, BISHOP_MG, BISHOP_MG, ROOK_MG, ROOK_MG, QUEEN_MG, QUEEN_MG, 0, 0},
                          {0, 0, PAWN_EG, PAWN_EG, KNIGHT_EG, KNIGHT_EG, BISHOP_EG, BISHOP_EG, ROOK_EG, ROOK_EG, QUEEN_EG, QUEEN_EG, 0, 0}};
int nonPawnValue[14] = {0, 0, 0, 0, KNIGHT_MG, KNIGHT_MG, BISHOP_MG, BISHOP_MG, ROOK_MG, ROOK_MG, QUEEN_MG, QUEEN_MG, 0, 0};
