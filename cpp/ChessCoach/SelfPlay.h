#ifndef _SELFPLAY_H_
#define _SELFPLAY_H_

#include <map>
#include <vector>
#include <random>
#include <atomic>

#include <Stockfish/Position.h>
#include <Stockfish/movegen.h>

#include "Game.h"
#include "Network.h"
#include "Storage.h"
#include "SavedGame.h"
#include "Threading.h"
#include "PredictionCache.h"
#include "PoolAllocator.h"

class Node
{
public:

    thread_local static PoolAllocator<Node> Allocator;

public:

    Node(float setPrior);

    void* operator new(size_t byteCount);
    void operator delete(void* memory) noexcept;

    bool IsExpanded() const;
    float Value() const;
    int SumChildVisits() const;

    std::map<Move, Node*> children;
    float originalPrior;
    float prior;
    int visitCount;
    float valueSum;
    float terminalValue;

private:

    // Doesn't strictly follow "mutable" if SumChildVisits() is misused and
    // called before children are changed while this node is still around.
    mutable int _sumChildVisits;
};

enum class SelfPlayState
{
    Working,
    WaitingForPrediction,
    Finished,
};

class SelfPlayGame : public Game
{
public:

    static std::atomic_uint ThreadSeed;
    thread_local static std::default_random_engine Random;

public:

    SelfPlayGame();
    SelfPlayGame(INetwork::InputPlanes* image, float* value, INetwork::OutputPlanes* policy);
    SelfPlayGame(const std::string& fen, INetwork::InputPlanes* image, float* value, INetwork::OutputPlanes* policy);

    SelfPlayGame(const SelfPlayGame& other);
    SelfPlayGame& operator=(const SelfPlayGame& other);
    SelfPlayGame(SelfPlayGame&& other) noexcept;
    SelfPlayGame& operator=(SelfPlayGame&& other) noexcept;
    ~SelfPlayGame();

    Node* Root() const;
    bool IsTerminal() const;
    float TerminalValue() const;
    float Result() const;
    
    void ApplyMoveWithRoot(Move move, Node* newRoot);
    void ApplyMoveWithRootAndHistory(Move move, Node* newRoot);
    float ExpandAndEvaluate(SelfPlayState& state, PredictionCacheEntry*& cacheStore);
    void LimitBranchingToBest(int moveCount, Move* moves, float* priors);
    bool IsDrawByNoProgressOrRepetition(int plyToSearchRoot);
    void Softmax(int moveCount, float* distribution) const;
    std::pair<Move, Node*> SelectMove() const;
    void StoreSearchStatistics();
    void Complete();
    SavedGame Save() const;
    void PruneExcept(Node* root, Node* except);
    void PruneAll();

private:

    void PruneAllInternal(Node* root);

private:

    // Used for both real and scratch games.
    Node* _root;
    INetwork::InputPlanes* _image;
    float* _value;
    INetwork::OutputPlanes* _policy;
    int _searchRootPly;

    // Stored history and statistics.
    // Only used for real games, so no need to copy, but may make sense for primitives.
    std::vector<std::map<Move, float>> _childVisits;
    std::vector<Move> _history;
    float _result;

    // Coroutine state.
    // Only used for real games, so no need to copy.
    ExtMove _expandAndEvaluate_moves[MAX_MOVES];
    ExtMove* _expandAndEvaluate_endMoves;
    Key _imageKey;
    std::array<Move, MAX_MOVES> _cachedMoves;
    std::array<float, MAX_MOVES> _cachedPriors;
};

class SelfPlayWorker
{
public:

    SelfPlayWorker();

    SelfPlayWorker(const SelfPlayWorker& other) = delete;
    SelfPlayWorker& operator=(const SelfPlayWorker& other) = delete;
    SelfPlayWorker(SelfPlayWorker&& other) = delete;
    SelfPlayWorker& operator=(SelfPlayWorker&& other) = delete;

    void ResetGames();
    void PlayGames(WorkCoordinator& workCoordinator, Storage* storage, INetwork* network);
    void Initialize(Storage* storage);
    void SetUpGame(int index);
    void DebugGame(INetwork* network, int index, const SavedGame& saved, int startingPly);
    void TrainNetwork(INetwork* network, int stepCount, int checkpoint) const;
    void Play(int index);
    void SaveToStorageAndLog(int index);
    std::pair<Move, Node*> RunMcts(SelfPlayGame& game, SelfPlayGame& scratchGame, SelfPlayState& state, int& mctsSimulation,
        std::vector<Node*>& searchPath, PredictionCacheEntry*& cacheStore);
    void AddExplorationNoise(SelfPlayGame& game) const;
    std::pair<Move, Node*> SelectChild(const Node* node) const;
    float CalculateUcbScore(const Node* parent, const Node* child) const;
    void Backpropagate(const std::vector<Node*>& searchPath, float value) const;

    void DebugGame(int index, SelfPlayGame** gameOut, SelfPlayState** stateOut, float** valuesOut, INetwork::OutputPlanes** policiesOut);

private:

    Storage* _storage;

    std::vector<SelfPlayState> _states;
    std::vector<INetwork::InputPlanes> _images;
    std::vector<float> _values;
    std::vector<INetwork::OutputPlanes> _policies;

    std::vector<SelfPlayGame> _games;
    std::vector<SelfPlayGame> _scratchGames;
    std::vector<std::chrono::steady_clock::time_point> _gameStarts;
    std::vector<int> _mctsSimulations;
    std::vector<std::vector<Node*>> _searchPaths;
    std::vector<PredictionCacheEntry*> _cacheStores;
};

#endif // _SELFPLAY_H_