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

vector<string> args;
Position *root_position;
extern unsigned TB_PROBE_DEPTH;
extern volatile bool ANALYSISMODE;
extern volatile bool is_pondering;
extern timeInfo globalLimits;

bool word_equal(int index, string comparison_str) {
    if (args.size() > (unsigned) index)
        return args[index] == comparison_str;
    return false;
}

vector<string> split_words(string s) {
    vector <string> tmp;
    unsigned l_index = 0;
    for (unsigned i = 0 ; i < s.length() ; i++) {
        if (s[i] == ' ') {
            tmp.push_back(s.substr(l_index, i - l_index));
            l_index = i + 1;
        }
        if (i == s.length() - 1) {
            tmp.push_back(s.substr(l_index));
        }
    }

    return tmp;
}

Move uci2Move(Position *pos, string s)
{
    int from = ( (s[0] - 'a') + 8*(s[1] - '1') );
    int to = ( (s[2] - 'a') + 8*(s[3] - '1') );
    PieceCode pc = pos->mailbox[from];
    SpecialType type = NORMAL;

    if (pc == WPAWN || pc == BPAWN)
    {
        if (RANK(to) == 0 || RANK(to) == 7)
            type = PROMOTION;
        else if (to == pos->epSquare)
            type = ENPASSANT;
    }
    else if (pc >= WKING && (abs(from - to) == 2 || (BITSET(to) & pos->pieceBB[make_piece(pos->activeSide, ROOK)])))
    {
        if (abs(from - to) == 2)
            to = (to > from) ? to + 1 : to - 2;
        type = CASTLING;
    }

    return makeMove(from, to, type);
}

void startpos()
{
    root_position = start_position();

    if (word_equal(2, "moves"))
    {
        for (unsigned i = 3 ; i < args.size() ; i++) {
            Move m = MOVE_NONE;
            if (args[i].length() == 4 || args[i].length() == 5)
            {
                m = uci2Move(root_position, args[i]);
                if (args[i].length() == 5)
                {
                    PieceType promote = GetPieceType(args[i][4]);
                    m = Move(m | ((promote - KNIGHT) << 12));
                }
            }
            if (m != MOVE_NONE && root_position->isPseudoLegal(m)) {
                root_position->do_move(m);
            }
        }
    }
}

void cmd_fen()
{
    string fen = args[2] + " " + args[3] + " " + args[4] + " " + args[5] + " " + args[6] + " " + args[7];
    root_position = import_fen(fen.c_str(), 0);

    if (word_equal(8, "moves"))
    {
        for (unsigned i = 9 ; i < args.size() ; i++) {
            Move m = MOVE_NONE;
            if (args[i].length() == 4 || args[i].length() == 5)
            {
                m = uci2Move(root_position, args[i]);
                if (args[i].length() == 5)
                {
                    PieceType promote = GetPieceType(args[i][4]);
                    m = Move(m | ((promote - KNIGHT) << 12));
                }
            }
            if (m != MOVE_NONE && root_position->isPseudoLegal(m)) {
                root_position->do_move(m);
            }
        }
    }
}

void ucinewgame()
{
    clear_threads();
    clear_tt();
}

template<bool Root>
U64 Perft(Position& pos, int depth) {
    U64 cnt, nodes = 0;
    const bool leaf = (depth == 2);

    for (const auto& m : MoveList<ALL>(pos))
    {
        if (Root && depth <= 1)
            cnt = 1, nodes++;
        else
        {
            pos.do_move(m);
            cnt = leaf ? MoveList<ALL>(pos).size() : Perft<false>(pos, depth - 1);
            nodes += cnt;
            pos.undo_move(m);
        }
        if (Root)
            cout << m.toString()<<": "<< m.code<<" : "<<cnt <<endl;
    }
    return nodes;
}

void perft()
{
    U64 nodes = Perft <true> (*root_position, stoi(args[1]));
    cout << "Nodes searched: "<<nodes<<endl;
}

void cmd_position() {
    if (args[1] == "fen")
        cmd_fen();
    if (args[1] == "startpos")
        startpos();
    get_ready();
}

void option(string name, string value) {
    if (name == "Hash")
    {
        int mb = stoi(value);
        if (!mb || MORETHANONE(uint64_t(mb))) {
            cout << "info Hash value needs to be a power of 2!" << endl;
        }
        reset_tt(mb);
    }
    else if (name == "Threads")
    {
        reset_threads(std::min(MAX_THREADS, std::max(1, stoi(value))));
    }
    else if (name == "MoveOverhead")
    {
        move_overhead = stoi(value);
    }
    else if (name == "BookFile")
    {
        openingBookPath = value;
        book.init(value);
    }
    else if (name == "BestBookLine")
    {
        if (value == "true")
            book.set_use_best(true);
        else if (value == "false")
            book.set_use_best(false);
    }
    else if (name == "MaxBookDepth")
    {
        book.set_max_depth(stoi(value));
    }
    else if (name == "ClearHash")
    {
        clear_tt();
    }
    else if (name == "SyzygyProbeDepth")
    {
        TB_PROBE_DEPTH = stoi((value));
    }
    else if (name == "SyzygyPath")
    {
        tb_init(value.c_str());
    }
    else if (name == "AnalysisMode")
    {
        if (value == "true")
            ANALYSISMODE = true;
        else if (value == "false")
            ANALYSISMODE = false;
    }
}

void setoption()
{
    if ( args[1] != "name" || args[3] != "value")
        return;

    string name = args[2];
    string value = args[4];
    option(name, value);
}

void setoption_fast()
{
    string name = args[1];
    string value = args[2];
    option(name, value);
}

void debug()
{
    cout << *root_position << endl;
    searchInfo *info = &main_thread.ss[2];
    MoveGen movegen = MoveGen(root_position, NORMAL_SEARCH, MOVE_NONE, 0, 0);
    Move m;
    cout << "Move ordering: " << endl;
    while ((m = movegen.next_move(info, 0)) != MOVE_NONE)
    {
        cout << move_to_str(m) << " ";
    }
    cout << endl;
}

void uci() {
    cout << "id name "<< NAME << " " << VERSION << endl << "id author " << AUTHOR << endl;
    cout << "option name Hash type spin default 128 min 1 max 65536" << endl;
    cout << "option name ClearHash type button" << endl;
    cout << "option name Threads type spin default 1 min 1 max " << MAX_THREADS << endl;
    cout << "option name MoveOverhead type spin default 100 min 0 max 5000" << endl;
    cout << "option name Ponder type check default false" << endl;
    cout << "option name BookFile type string default <empty>" << endl;
    cout << "option name BestBookLine type check default true" << endl;
    cout << "option name MaxBookDepth type spin default 255 min 1 max 255" << endl;
    cout << "option name SyzygyPath type string default <empty>" << endl;
    cout << "option name SyzygyProbeDepth type spin default 0 min 0 max 127" << endl;
    cout << "option name AnalysisMode type check default false" << endl;
    cout << "uciok" << endl;
}

void quit() {
    if (!is_searching) {
        exit(EXIT_SUCCESS);
    }

    is_timeout = true;
    is_pondering = false;
    quit_application = true;
}

void stop() {
    is_timeout = true;
    is_pondering = false;
}

void isready() {
    cout << "readyok" << endl;
}

void go() {

    int depth = 0;
    bool infinite = false, timelimited = false, depthlimited = false;
    int wtime = 0, btime = 0, movetime = 0;
    int winc = 0, binc = 0, movestogo = 0;
    think_depth_limit = MAX_PLY;
    memset(&globalLimits, 0, sizeof(timeInfo));

    if (args.size() <= 1) {
        globalLimits.movesToGo = 0;
        globalLimits.totalTimeLeft = 10000;
        globalLimits.increment = 0;
        globalLimits.movetime = 0;
        globalLimits.depthlimit = 0;
        globalLimits.timelimited = true;
        globalLimits.depthlimited = false;
        globalLimits.infinite = false;
    }

    else {

        for (unsigned i = 1; i < args.size(); ++i) {

            if (args[i] == "wtime")
            {
                wtime = stoi(args[i + 1]);
            }
            else if (args[i] == "btime")
            {
                btime = stoi(args[i + 1]);
            }
            else if (args[i] == "winc")
            {
                winc = stoi(args[i + 1]);
            }
            else if (args[i] == "binc")
            {
                binc = stoi(args[i + 1]);
            }
            else if (args[i] == "movestogo")
            {
                movestogo = stoi(args[i + 1]);
            }
            else if (args[i] == "depth")
            {
                depthlimited = true;
                depth = stoi(args[i + 1]);
            }

            else if (args[i] == "ponder")
            {
                is_pondering = true;
            }
            else if (args[i] == "infinite")
            {
                infinite = true;
            }
            else if (args[i] == "movetime")
            {
                movetime = stoi(args[i + 1]) * 99 / 100;
                timelimited = true;
            }
        }

    globalLimits.movesToGo = movestogo;
    globalLimits.totalTimeLeft = root_position->activeSide == WHITE ? wtime : btime;
    globalLimits.increment = root_position->activeSide == WHITE ? winc : binc;
    globalLimits.movetime = movetime;
    globalLimits.depthlimit = depth;
    globalLimits.timelimited = timelimited;
    globalLimits.depthlimited = depthlimited;
    globalLimits.infinite = infinite;
    }

    //std::thread think_thread(think, root_position);
    //think_thread.detach();
    pthread_t gothread;
    pthread_create(&gothread, NULL, &think, root_position);
    pthread_detach(gothread);
}

void eval() {
    cout << trace(*root_position) << endl;
}

void ponderhit() {
    is_pondering = false;
}

void see() {
    Move m = uci2Move(root_position, args[1]);
    cout << root_position->see(m, 0) << endl;
}

void run(string s)
{
    if (s == "ucinewgame")
        ucinewgame();
    if (s == "position")
        cmd_position();
    if (s == "go")
        go();
    if (s == "setoption")
        setoption();
    if (s == "set")
        setoption_fast();
    if (s == "isready")
        isready();
    if (s == "uci")
        uci();
    if (s == "perft")
        perft();
    if (s == "debug")
        debug();
    if (s == "quit" || s == "exit")
        quit();
    if (s == "stop")
        stop();
    if (s == "see")
        see();
    if (s == "bench")
        bench();
    if (s == "eval")
        eval();
    if (s == "ponderhit")
        ponderhit();
    if (s == "tune")
        tune();
}

void loop()
{
    cout << NAME << " "<< VERSION <<" by "<< AUTHOR <<endl;
#ifdef USE_POPCNT
    cout << "Using POPCOUNT" << endl;
#endif
    string input;

    root_position = start_position();

    while (true)
    {
        getline(cin, input);
        args = split_words(input);
        if (args.size() > 0)
        {
            run(args[0]);
        }
    }
}
