#include <iostream>
#include <string>
#include <sstream>
#include <functional>
#include <vector>
#include <thread>
#include <cstdlib>

#include <Stockfish/thread.h>
#include <Stockfish/uci.h>

#include <ChessCoach/ChessCoach.h>
#include <ChessCoach/SelfPlay.h>

typedef std::function<void(std::stringstream&)> CommandHandler;
typedef std::pair<std::string, CommandHandler> CommandHandlerEntry;

class ChessCoachUci : public ChessCoach
{
public:

    void Initialize();
    void Finalize();
    void Work();

private:

    bool HandleCommand(std::stringstream& commands, std::string command);

    void HandleUci(std::stringstream& commands);
    void HandleDebug(std::stringstream& commands);
    void HandleIsReady(std::stringstream& commands);
    void HandleSetOption(std::stringstream& commands);
    void HandleRegister(std::stringstream& commands);
    void HandleUciNewGame(std::stringstream& commands);
    void HandlePosition(std::stringstream& commands);
    void HandleGo(std::stringstream& commands);
    void HandleStop(std::stringstream& commands);
    void HandlePonderHit(std::stringstream& commands);
    void HandleQuit(std::stringstream& commands);

    void InitializeSelfPlayWorker();

    template <typename T, typename... Ts>
    void Reply(T reply, Ts... replies)
    {
        DoReply(reply);
        Reply(replies...);
    }

    template <typename T>
    void Reply(T reply)
    {
        DoReply(reply);
        std::cout.flush();
    }

    template <typename T>
    void DoReply(T reply)
    {
        std::cout << reply << std::endl;
    }

private:

    bool _quit = false;
    bool _debug = false;

    std::vector<CommandHandlerEntry> _commandHandlers;

    std::unique_ptr<SelfPlayWorker> _selfPlayWorker;
    std::unique_ptr<std::thread> _selfPlayThread;
};

int main(int argc, char* argv[])
{
    ChessCoachUci chessCoachUci;

    chessCoachUci.Initialize();

    chessCoachUci.Work();

    chessCoachUci.Finalize();

    return 0;
}

void ChessCoachUci::Initialize()
{
    // Suppress all Python/TensorFlow output so that it doesn't interfere with UCI.
    _putenv_s("CHESSCOACH_SILENT", "1");

    InitializePython();
    InitializeStockfish();
    InitializeChessCoach();

    _commandHandlers.emplace_back("uci", std::bind(&ChessCoachUci::HandleUci, this, std::placeholders::_1));
    _commandHandlers.emplace_back("debug", std::bind(&ChessCoachUci::HandleDebug, this, std::placeholders::_1));
    _commandHandlers.emplace_back("isready", std::bind(&ChessCoachUci::HandleIsReady, this, std::placeholders::_1));
    _commandHandlers.emplace_back("setoption", std::bind(&ChessCoachUci::HandleSetOption, this, std::placeholders::_1));
    _commandHandlers.emplace_back("register", std::bind(&ChessCoachUci::HandleRegister, this, std::placeholders::_1));
    _commandHandlers.emplace_back("ucinewgame", std::bind(&ChessCoachUci::HandleUciNewGame, this, std::placeholders::_1));
    _commandHandlers.emplace_back("position", std::bind(&ChessCoachUci::HandlePosition, this, std::placeholders::_1));
    _commandHandlers.emplace_back("go", std::bind(&ChessCoachUci::HandleGo, this, std::placeholders::_1));
    _commandHandlers.emplace_back("stop", std::bind(&ChessCoachUci::HandleStop, this, std::placeholders::_1));
    _commandHandlers.emplace_back("ponderhit", std::bind(&ChessCoachUci::HandlePonderHit, this, std::placeholders::_1));
    _commandHandlers.emplace_back("quit", std::bind(&ChessCoachUci::HandleQuit, this, std::placeholders::_1));
}

void ChessCoachUci::Finalize()
{
    FinalizePython();
    FinalizeStockfish();
}

void ChessCoachUci::Work()
{
    std::string line;
    while (!_quit && std::getline(std::cin, line))
    {
        std::stringstream commands(line);
        std::string token;
        do
        {
            commands >> token;
        } while (!commands.fail() && !HandleCommand(commands, token));
    }
}

bool ChessCoachUci::HandleCommand(std::stringstream& commands, std::string command)
{
    for (const auto& [key, handler] : _commandHandlers)
    {
        if (command == key)
        {
            handler(commands);
            return true;
        }
    }

    return false;
}

void ChessCoachUci::HandleUci(std::stringstream& commands)
{
    Reply("id name ChessCoach",
        "id author C. Butner, T. Li",
        // Options are listed here when they exist.
        "uciok");
}

void ChessCoachUci::HandleDebug(std::stringstream& commands)
{
    std::string setting;

    commands >> setting;

    if (setting == "on")
    {
        _debug = true;
    }
    else if (setting == "off")
    {
        _debug = false;
    }

    InitializeSelfPlayWorker();
    _selfPlayWorker->SignalDebug(_debug);
}

void ChessCoachUci::HandleIsReady(std::stringstream& commands)
{
    InitializeSelfPlayWorker();
    _selfPlayWorker->WaitUntilReady();
    Reply("readyok");
}

void ChessCoachUci::HandleSetOption(std::stringstream& commands)
{
}

void ChessCoachUci::HandleRegister(std::stringstream& commands)
{
}

void ChessCoachUci::HandleUciNewGame(std::stringstream& commands)
{
    InitializeSelfPlayWorker();
}

void ChessCoachUci::HandlePosition(std::stringstream& commands)
{
    std::string token;
    std::string fen;
    std::vector<Move> moves;
    StateListPtr positionStates(new std::deque<StateInfo>(1));
    Position position;

    commands >> token;
    if (token == "fen")
    {
        // Trust the provided FEN.
        fen.reserve(128);
        while ((commands >> token) && (token != "moves"))
        {
            fen += token + " ";
        }
    }
    else
    {
        // Instead of "fen" we got "startpos" or something invalid.
        fen = Config::StartingPosition;
        while ((commands >> token) && (token != "moves"))
        {
        }
    }

    position.set(fen, false /* isChess960 */, &positionStates->back(), Threads.main());

    // If "moves" wasn't seen then we already consumed the rest of the line.
    while (commands >> token)
    {
        Move move = UCI::to_move(position, token);
        if (move == MOVE_NONE)
        {
            break;
        }

        positionStates->emplace_back();
        position.do_move(move, positionStates->back());

        moves.push_back(move);
    }

    InitializeSelfPlayWorker();
    _selfPlayWorker->SignalPosition(std::move(fen), std::move(moves));
}

void ChessCoachUci::HandleGo(std::stringstream& commands)
{
    // TODO: searchmoves
    // TODO: ponder
    // TODO: movestogo
    // TODO: depth
    // TODO: nodes
    // TODO: mate

    TimeControl timeControl = {};

    std::string token;
    while (commands >> token)
    {
        if (token == "infinite")
        {
            timeControl.infinite = true;
        }
        else if (token == "movetime")
        {
            commands >> timeControl.moveTimeMs;
        }
        else if (token == "wtime")
        {
            commands >> timeControl.timeRemainingMs[WHITE];
        }
        else if (token == "btime")
        {
            commands >> timeControl.timeRemainingMs[BLACK];
        }
        else if (token == "winc")
        {
            commands >> timeControl.incrementMs[WHITE];
        }
        else if (token == "binc")
        {
            commands >> timeControl.incrementMs[BLACK];
        }
    }

    InitializeSelfPlayWorker();
    _selfPlayWorker->SignalSearchGo(timeControl);
}

void ChessCoachUci::HandleStop(std::stringstream& commands)
{
    InitializeSelfPlayWorker();
    _selfPlayWorker->SignalSearchStop();
}

void ChessCoachUci::HandlePonderHit(std::stringstream& commands)
{
    // TODO: Do the necessary housekeeping here
}

void ChessCoachUci::HandleQuit(std::stringstream& commands)
{
    if (_selfPlayWorker)
    {
        _selfPlayWorker->SignalQuit();
        _selfPlayThread->join();
    }
    _quit = true;
}

void ChessCoachUci::InitializeSelfPlayWorker()
{
    if (_selfPlayWorker)
    {
        return;
    }

    _selfPlayWorker.reset(new SelfPlayWorker());
    _selfPlayThread.reset(new std::thread(&SelfPlayWorker::Search, _selfPlayWorker.get(), std::bind(&ChessCoach::CreateNetwork, this)));
}