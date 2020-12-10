#include <thread>

#include <ChessCoach/ChessCoach.h>
#include <ChessCoach/Platform.h>

class ChessCoachGui : public ChessCoach
{
public:

    void InitializeLight();
    void FinalizeLight();
    void Run();
};

int main()
{
    ChessCoachGui gui;

    gui.PrintExceptions();
    gui.InitializeLight();

    gui.Run();

    // Cleanup is unreachable for now.

    //gui.FinalizeLight();

    //return 0;
}

void ChessCoachGui::InitializeLight()
{
    // Suppress all Python/TensorFlow output.
    Platform::SetEnvironmentVariable("CHESSCOACH_SILENT", "1");

    InitializePython();
    InitializeStockfish();
    InitializeChessCoach();
}

void ChessCoachGui::FinalizeLight()
{
    FinalizePython();
    FinalizeStockfish();
}

void ChessCoachGui::Run()
{
    // Call in to Python.
    std::unique_ptr<INetwork> network(CreateNetwork(Config::TrainingNetwork));
    network->LaunchGui();

    // Sleep and let Python run its own separate message pump. We may want to handle command-line input here to drive UI in future.
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::hours(1));
    }
}