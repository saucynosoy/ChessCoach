// ChessCoach, a neural network-based chess engine capable of natural-language commentary
// Copyright 2021 Chris Butner
//
// ChessCoach is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// ChessCoach is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with ChessCoach. If not, see <https://www.gnu.org/licenses/>.

#include <gtest/gtest.h>

#include <ChessCoach/SelfPlay.h>
#include <ChessCoach/ChessCoach.h>
#include <ChessCoach/Random.h>

TEST(Game, Flip)
{
    Move move = make_move(SQ_B3, SQ_E5);
    Move moveFlipped = make_move(SQ_B6, SQ_E4);

    // Flip moves.
    EXPECT_EQ(Game::FlipMove(WHITE, move), move);
    EXPECT_EQ(Game::FlipMove(BLACK, move), moveFlipped);
    EXPECT_EQ(Game::FlipMove(WHITE, moveFlipped), moveFlipped);
    EXPECT_EQ(Game::FlipMove(BLACK, moveFlipped), move);

    // Flip squares.
    EXPECT_EQ(Game::FlipSquare(WHITE, from_sq(move)), from_sq(move));
    EXPECT_EQ(Game::FlipSquare(WHITE, to_sq(move)), to_sq(move));
    EXPECT_EQ(Game::FlipSquare(BLACK, from_sq(move)), from_sq(moveFlipped));
    EXPECT_EQ(Game::FlipSquare(BLACK, to_sq(move)), to_sq(moveFlipped));
    EXPECT_EQ(Game::FlipSquare(WHITE, from_sq(moveFlipped)), from_sq(moveFlipped));
    EXPECT_EQ(Game::FlipSquare(WHITE, to_sq(moveFlipped)), to_sq(moveFlipped));
    EXPECT_EQ(Game::FlipSquare(BLACK, from_sq(moveFlipped)), from_sq(move));
    EXPECT_EQ(Game::FlipSquare(BLACK, to_sq(moveFlipped)), to_sq(move));

    // Flipping value is statically tested enough in Game.h.

    // Flip pieces.
    std::array<Piece, 6> whitePieces = { W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING };
    std::array<Piece, 6> blackPieces = { B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING };
    EXPECT_EQ(Game::FlipPiece[WHITE][NO_PIECE], NO_PIECE);
    EXPECT_EQ(Game::FlipPiece[BLACK][NO_PIECE], NO_PIECE);
    for (int i = 0; i < whitePieces.size(); i++)
    {
        EXPECT_EQ(Game::FlipPiece[WHITE][whitePieces[i]], whitePieces[i]);
        EXPECT_EQ(Game::FlipPiece[BLACK][whitePieces[i]], blackPieces[i]);
        EXPECT_EQ(Game::FlipPiece[WHITE][blackPieces[i]], blackPieces[i]);
        EXPECT_EQ(Game::FlipPiece[BLACK][blackPieces[i]], whitePieces[i]);
    }
}

TEST(Game, FlipBoard)
{
    ChessCoach chessCoach;
    chessCoach.Initialize();

    // Flip the board and flip back.
    Game startingPosition;
    Position& position = startingPosition.GetPosition();
    Bitboard whitePawns = position.pieces(WHITE, PAWN);
    Bitboard flipWhitePawns1 = Game::FlipBoard(whitePawns);
    Bitboard flipWhitePawns2 = Game::FlipBoard(flipWhitePawns1);
    EXPECT_NE(whitePawns, flipWhitePawns1);
    EXPECT_EQ(whitePawns, flipWhitePawns2);

    // Do the Stockfish debug flip, expect the same except colors reversed.
    Bitboard flipBlackPawns = Game::FlipBoard(position.pieces(BLACK, PAWN));
    position.flip();
    Bitboard debugFlipWhitePawns = position.pieces(WHITE, PAWN);
    EXPECT_EQ(flipBlackPawns, debugFlipWhitePawns);
}

TEST(Game, FlipSpecialMoves)
{
    const Move moves[] =
    {
        make_move(SQ_E2, SQ_E4),

        make<ENPASSANT>(SQ_E5, SQ_D6),

        make<PROMOTION>(SQ_D7, SQ_D8, QUEEN),
        make<PROMOTION>(SQ_D7, SQ_D8, ROOK),
        make<PROMOTION>(SQ_D7, SQ_D8, BISHOP),
        make<PROMOTION>(SQ_D7, SQ_D8, KNIGHT),

        make<PROMOTION>(SQ_D7, SQ_E8, QUEEN),
        make<PROMOTION>(SQ_D7, SQ_E8, ROOK),
        make<PROMOTION>(SQ_D7, SQ_E8, BISHOP),
        make<PROMOTION>(SQ_D7, SQ_E8, KNIGHT),

        make<PROMOTION>(SQ_D2, SQ_D1, QUEEN),
        make<PROMOTION>(SQ_D2, SQ_D1, ROOK),
        make<PROMOTION>(SQ_D2, SQ_D1, BISHOP),
        make<PROMOTION>(SQ_D2, SQ_D1, KNIGHT),

        make<PROMOTION>(SQ_D2, SQ_E1, QUEEN),
        make<PROMOTION>(SQ_D2, SQ_E1, ROOK),
        make<PROMOTION>(SQ_D2, SQ_E1, BISHOP),
        make<PROMOTION>(SQ_D2, SQ_E1, KNIGHT),
    };
    for (const Move move : moves)
    {
        EXPECT_EQ(move, Game::FlipMove(WHITE, move));
        EXPECT_NE(move, Game::FlipMove(BLACK, move));
        EXPECT_EQ(move, Game::FlipMove(WHITE, Game::FlipMove(WHITE, move)));
        EXPECT_EQ(move, Game::FlipMove(BLACK, Game::FlipMove(BLACK, move)));
    }
}