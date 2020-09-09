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

Score piece_bonus[7][64] = {
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
    S(-24,28), S(12,12), S(-23,17), S(7,2), S(13,20), S(-2,4), S(21,-1), S(-33,-6),
    S(-17,13), S(-1,9), S(-2,-5), S(11,-7), S(27,-2), S(-1,-3), S(28,-10), S(-36,-4),
    S(-27,30), S(-12,19), S(1,-9), S(22,-20), S(29,-26), S(16,-17), S(7,-6), S(-37,3),
    S(-4,47), S(19,19), S(14,0), S(39,-34), S(41,-23), S(24,-11), S(34,12), S(-3,16),
    S(-6,72), S(-10,53), S(-1,22), S(1,-22), S(54,-50), S(69,-19), S(26,16), S(13,29),
    S(10,52), S(9,24), S(-45,15), S(12,-49), S(-63,-14), S(54,-47), S(0,21), S(24,35),
    S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0), S(0,0)
},
{
    S(-105,11), S(4,-15), S(-26,1), S(-14,33), S(27,14), S(-4,23), S(1,-22), S(3,-41),
    S(-14,-13), S(-32,4), S(4,7), S(20,18), S(27,19), S(41,0), S(22,8), S(9,-15),
    S(1,-11), S(1,16), S(29,6), S(31,39), S(44,39), S(41,11), S(50,-9), S(11,0),
    S(-2,-2), S(24,1), S(24,36), S(27,53), S(29,49), S(35,39), S(28,36), S(6,1),
    S(-13,4), S(40,15), S(18,47), S(51,54), S(47,52), S(55,42), S(38,25), S(10,6),
    S(-100,-7), S(7,10), S(-5,40), S(6,43), S(22,38), S(67,19), S(39,-1), S(4,-39),
    S(-108,-14), S(-77,20), S(0,6), S(-8,44), S(-58,45), S(20,1), S(-5,11), S(-55,-51),
    S(-237,-23), S(-178,-5), S(-136,40), S(-101,-4), S(-4,2), S(-123,-8), S(-85,-30), S(-200,-101)
},
{
    S(-10,5), S(30,18), S(23,19), S(23,27), S(35,18), S(15,29), S(-1,19), S(-1,11),
    S(34,10), S(43,-1), S(37,11), S(23,24), S(34,33), S(46,22), S(71,3), S(41,-16),
    S(17,17), S(36,19), S(34,33), S(31,34), S(38,44), S(59,25), S(44,21), S(29,18),
    S(1,15), S(31,10), S(26,24), S(29,40), S(40,23), S(16,34), S(22,19), S(28,-2),
    S(3,16), S(14,21), S(12,28), S(31,35), S(18,42), S(19,40), S(8,18), S(-3,23),
    S(18,11), S(25,12), S(0,32), S(9,15), S(-15,30), S(43,28), S(17,27), S(25,17),
    S(-8,23), S(-2,30), S(-13,31), S(-93,42), S(1,39), S(-25,52), S(-35,45), S(-71,38),
    S(11,2), S(25,-4), S(-156,37), S(-85,36), S(-95,49), S(-58,27), S(-4,24), S(31,-26)
},
{
    S(30,30), S(27,40), S(27,42), S(46,27), S(53,24), S(50,26), S(34,23), S(21,16),
    S(3,42), S(29,34), S(14,45), S(45,33), S(62,18), S(57,21), S(40,18), S(-8,32),
    S(-2,47), S(22,46), S(32,35), S(27,39), S(46,30), S(41,26), S(42,32), S(25,14),
    S(-2,56), S(18,57), S(11,63), S(40,43), S(42,43), S(19,46), S(48,44), S(4,51),
    S(-3,63), S(23,52), S(41,65), S(49,46), S(50,53), S(46,63), S(4,66), S(0,66),
    S(3,57), S(5,68), S(19,55), S(30,47), S(5,55), S(24,52), S(86,34), S(10,45),
    S(1,56), S(10,51), S(40,41), S(32,46), S(44,35), S(30,59), S(-41,84), S(18,56),
    S(-10,73), S(18,70), S(-41,83), S(26,63), S(2,79), S(-35,86), S(8,76), S(0,72)
},
{
    S(35,-43), S(21,-43), S(27,-32), S(49,-54), S(22,6), S(25,-41), S(28,-39), S(-30,-22),
    S(-5,-5), S(22,-29), S(47,-43), S(34,-12), S(40,-1), S(57,-44), S(27,-41), S(45,-23),
    S(2,32), S(36,-37), S(5,23), S(25,-5), S(12,27), S(25,16), S(42,14), S(23,46),
    S(9,13), S(-18,60), S(6,33), S(-11,59), S(5,40), S(16,16), S(17,48), S(11,36),
    S(-26,75), S(-15,62), S(-22,53), S(-26,47), S(-28,62), S(14,-6), S(-10,70), S(9,42),
    S(25,-16), S(7,9), S(-1,7), S(-18,58), S(-39,47), S(57,-1), S(37,-22), S(65,-26),
    S(-17,-4), S(-43,25), S(-10,37), S(-15,75), S(-98,93), S(-43,12), S(-33,51), S(76,-20),
    S(4,-43), S(13,11), S(14,18), S(-2,14), S(110,-50), S(6,8), S(52,-25), S(30,44)
},
{
    S(5,-102), S(63,-70), S(86,-26), S(15,-7), S(25,-17), S(29,-7), S(55,-45), S(37,-81),
    S(23,-60), S(22,-17), S(38,13), S(2,27), S(23,23), S(36,9), S(30,-15), S(34,-45),
    S(0,-43), S(15,-16), S(22,27), S(-9,43), S(-9,41), S(17,22), S(7,0), S(-18,-26),
    S(-74,-26), S(20,-11), S(6,44), S(-64,64), S(-101,73), S(-15,42), S(-35,7), S(-61,-28),
    S(81,-35), S(16,34), S(45,48), S(-42,69), S(-1,59), S(49,59), S(27,40), S(-60,4),
    S(101,-1), S(73,27), S(146,19), S(-22,62), S(-4,69), S(140,76), S(168,52), S(24,20),
    S(132,-34), S(47,36), S(50,50), S(86,47), S(-20,73), S(74,72), S(93,21), S(-73,73),
    S(-154,-65), S(46,-7), S(97,-23), S(58,-19), S(-110,58), S(41,88), S(114,22), S(55,-27)
}
};

Score isolated_penalty[2] = {S(4,35), S(12,27)};
Score isolated_penaltyAH[2] = {S(0,34), S(10,21)};

Score doubled_penalty[2] = {S(22,2), S(-20,82)};
Score doubled_penalty_undefended[2] = {S(-11,11), S(-7,8)};

Score isolated_doubled_penalty[2] = {S(4,29), S(-6,15)};
Score isolated_doubled_penaltyAH[2] = {S(8,42), S(2,43)};

Score backward_penalty[2] = {S(-8,1), S(0,2)};

Score connected_bonus[2][2][8] = {
{
    {S(0,0), S(0,0), S(14,-15), S(21,-6), S(25,8), S(37,43), S(252,16), S(0,0)},
    {S(0,0), S(14,-30), S(21,-14), S(36,17), S(73,83), S(70,247), S(-144,910), S(0,0)}
},
{
    {S(0,0), S(0,0), S(-1,-8), S(0,-10), S(0,0), S(-20,64), S(0,0), S(0,0)},
    {S(0,0), S(-1,-23), S(1,-11), S(17,4), S(19,31), S(267,54), S(0,0), S(0,0)}
}
};

Score passedRankBonus[8] = {S(0,0), S(0,0), S(9,13), S(-12,8), S(12,74), S(1,97), S(72,144), S(0,0)};

Score passedUnsafeBonus[2][8] = {
    {S(0,0), S(0,0), S(-10,-7), S(-8,50), S(-57,38), S(-5,104), S(-16,200), S(0,0)},
    {S(0,0), S(0,0), S(-12,-14), S(-1,27), S(-15,-46), S(68,-95), S(121,-109), S(0,0)}
};

Score passedBlockedBonus[2][8] = {
    {S(0,0), S(0,0), S(4,-5), S(1,-1), S(9,21), S(24,75), S(50,217), S(0,0)},
    {S(0,0), S(0,0), S(4,-8), S(2,-21), S(5,-28), S(37,-87), S(169,-140), S(0,0)}
};

Score knightMobilityBonus[9] = {
    S(-52,-43), S(-55,-39), S(-44,0), S(-40,13), S(-30,19), S(-24,27), S(-16,31),
    S(-6,28), S(1,28)};

Score bishopMobilityBonus[14] = {
    S(-61,-59), S(-40,-65), S(-19,-49), S(-17,-14), S(-5,-7), S(3,3), S(12,9),
    S(13,14), S(19,17), S(16,19), S(36,8), S(37,19), S(45,32), S(-2,18)};

Score rookMobilityBonus[15] = {
    S(-165,-232), S(-117,-121), S(-102,-85), S(-98,-72), S(-96,-56), S(-92,-41), S(-87,-35),
    S(-82,-29), S(-73,-22), S(-60,-25), S(-60,-15), S(-50,-8), S(-53,-2), S(-51,3),
    S(-17,-7)};

Score queenMobilityBonus[28] = {
    S(-2378,-1284), S(29,157), S(19,67), S(19,53), S(25,25), S(25,-12), S(32,3),
    S(35,20), S(37,54), S(39,70), S(40,84), S(43,91), S(48,100), S(48,109),
    S(55,119), S(51,127), S(55,131), S(56,138), S(53,145), S(89,137), S(93,118),
    S(144,98), S(174,77), S(194,56), S(238,33), S(143,99), S(243,-36), S(184,-9)};

int PAWN_MG = 100;
int PAWN_EG = 164;
int KNIGHT_MG = 368;
int KNIGHT_EG = 435;
int BISHOP_MG = 427;
int BISHOP_EG = 528;
int ROOK_MG = 732;
int ROOK_EG = 952;
int QUEEN_MG = 1238;
int QUEEN_EG = 1657;
int KING_MG = 0;
int KING_EG = 0;

int attackerWeights[7] = {0, 0, 33, 32, 30, 12, 0};

int checkPenalty[7] = {0, 0, 423, 240, 556, 374, 0};

int unsafeCheckPenalty[7] = {0, 0, 56, 129, 143, 94, 0};

int queenContactCheck = 360;
int kingDangerBase = -8;
int kingflankAttack = 0;
int kingringAttack = 50;
int kingpinnedPenalty = 23;
int kingweakPenalty = 72;
int pawnDistancePenalty = 7;
int kingShieldBonus = 11;
int noQueen = 691;


int kingShield[4][8] = {
    {17, 61, 58, 39, 75, 137, 59, 0},
    {-25, 41, 25, 6, 6, 135, 66, 0},
    {17, 82, 52, 45, 52, 153, 147, 0},
    {11, 18, 12, 15, 35, 71, 148, 0}
};

int pawnStormBlocked[4][8] = {
    {0, 0, 68, -3, 5, 70, 152, 0},
    {0, 0, 106, 9, 18, 49, 150, 0},
    {0, 0, 111, 32, 27, 40, 184, 0},
    {0, 0, 211, 45, 30, 50, 47, 0}
};

int pawnStormFree[4][8] = {
    {21, -256, -140, -16, 22, 27, 17, 0},
    {26, -175, -65, 2, 5, 6, 16, 0},
    {24, -154, -23, 18, 18, 27, 23, 0},
    {26, -19, 74, 52, 48, 20, 22, 0}
};

Score bishopPawns = S(-1,1);
Score rookFile[2] = {S(34,-1), S(50,4)};

Score battery = S(10,-10);
Score kingProtector = S(3,-3);
Score outpostBonus[2][2] = {{S(14,8), S(31,16)}, {S(50,10), S(62,29)}};
Score reachableOutpost[2] = {S(0,8), S(18,22)};
Score minorThreat[7] = {S(0,0), S(6,17), S(24,43), S(35,31), S(65,1), S(54,-36), S(90,648)};

Score rookThreat[7] = {S(0,0), S(-7,21), S(21,28), S(31,40), S(20,-17), S(79,-63), S(626,809)};

Score kingThreat = S(2,50);
Score kingMultipleThreat = S(-22,94);
Score pawnPushThreat = S(23,10);
Score safePawnThreat = S(74,28);
Score hangingPiece = S(7,12);

Score bishopOpposerBonus = S(-3,12);
Score trappedBishopPenalty = S(42,8);
Score veryTrappedBishopPenalty = S(51,88);

Score defendedRookFile = S(0,-28);

Score rank7Rook = S(2,36);

Score passedFriendlyDistance[8] = {S(0,0), S(7,-1), S(-2,-3), S(9,-11), S(14,-13), S(22,-11), S(5,-9), S(0,0)};

Score passedEnemyDistance[8] = {S(0,0), S(-2,2), S(2,6), S(-5,11), S(-11,29), S(-49,53), S(-10,17), S(0,0)};

Score tarraschRule_friendly[8] = {S(0,0), S(0,0), S(0,0), S(24,6), S(14,33), S(-4,49), S(-53,85), S(0,0)};

Score tarraschRule_enemy = S(-41,96);


int my_pieces[5][5] = {
    {  14                    },
    { 186,  20               },
    {  47, 216, -42          },
    {  27, 195, 164, -76     },
    {  59,  20, -25,-439, -90}
};

int opponent_pieces[5][5] = {
    {   0                    },
    {  60,   0               },
    {  42,-120,   0          },
    {  69, -15,-100,   0     },
    { 271,  -8, 215, 340,   0}
};


int bishop_pair = 81;






////////////Not modified/////////////////////
int pieceValues[2][14] = {{0, 0, PAWN_MG, PAWN_MG, KNIGHT_MG, KNIGHT_MG, BISHOP_MG, BISHOP_MG, ROOK_MG, ROOK_MG, QUEEN_MG, QUEEN_MG, 0, 0},
                          {0, 0, PAWN_EG, PAWN_EG, KNIGHT_EG, KNIGHT_EG, BISHOP_EG, BISHOP_EG, ROOK_EG, ROOK_EG, QUEEN_EG, QUEEN_EG, 0, 0}};
int nonPawnValue[14] = {0, 0, 0, 0, KNIGHT_MG, KNIGHT_MG, BISHOP_MG, BISHOP_MG, ROOK_MG, ROOK_MG, QUEEN_MG, QUEEN_MG, 0, 0};
