﻿// Code is really low-effort, apologies.
(function () {

    function highlight(element, proportion) {
        if (proportion > 0) {
            proportion = 0.6 * proportion + 0.2;
        }
        element.style.backgroundColor = `rgba(128, 0, 0, ${proportion}`;
    }

    function overlaysHighlightAll() {
        const proportions = {};
        for (const square in overlays) {
            proportions[square] = 0;
        }
        for (const move of data.policy) {
            proportions[move.from] = Math.min(1, proportions[move.from] + move.target);
            proportions[move.to] = Math.min(1, proportions[move.to] + move.target);
        }
        for (const square in proportions) {
            highlight(overlays[square], proportions[square]);
        }
    }

    function overlaysHighlightMove(move) {
        clearHighlights();
        highlight(overlays[move.from], move.target);
        highlight(overlays[move.to], move.target);
    }

    function clearHighlights() {
        for (const overlay of Object.values(overlays)) {
            overlay.style.backgroundColor = null;
        }
    }

    function clearMoves(example) {
        document.getElementById("moves").innerHTML = (example["prior"] !== undefined) ?
            "<tr><th>Move</th><th>Policy</th><th>Prior</th><th>Value</th><th>PUCT</th><th>Visits</th><th>Weight</th><th>UpWeight</th></tr>" :
            "<tr><th>Move</th><th>Policy</th>";
    }

    function addField(row, move, field) {
        const cell = document.createElement("td");
        cell.textContent = String(move[field]);
        row.appendChild(cell);
    }

    function addMove(move) {
        const row = document.createElement("tr");
        document.getElementById("moves").appendChild(row);

        addField(row, move, "san");
        addField(row, move, "target");
        if (move["prior"] !== undefined) {
            addField(row, move, "prior");
            addField(row, move, "value");
            addField(row, move, "puct");
            addField(row, move, "visits");
            addField(row, move, "weight");
            addField(row, move, "up_weight");
        }

        row.addEventListener("mouseover", () => {
            highlight(row, move.target);
            overlaysHighlightMove(move);
        });

        row.addEventListener("mouseout", () => {
            row.style.backgroundColor = null;
            overlaysHighlightAll();
        });

        row.addEventListener("click", () => {
            showLineAddSan(move.san);
        });
    }

    function showLine(line) {
        socket.send(JSON.stringify({
            type: "line",
            line: line,
        }));
    }

    function showLineAddSan(san) {
        line = (data.line ? data.line + " " + san : san);
        showLine(line);
    }

    function requestCommentary() {
        socket.send(JSON.stringify({
            type: "commentary_request",
            fen: data.fen,
        }));
    }

    function runCommentarySuite() {
        socket.send(JSON.stringify({
            type: "commentary_suite_request",
        }));
    }

    function requestPosition() {
        socket.send(JSON.stringify({
            type: "request",
            game: game,
            position: position,
        }));
    }

    function handleInitialize(message) {
        game_count = message.game_count;

        if (game_count > 0) {
            requestPosition();
        }
    }

    function handleTrainingData(trainingData) {
        data = trainingData;

        document.getElementById("trainingControls").style.display = "block";

        game = data.game;
        position_count = data.position_count;
        position = data.position;
        renderPosition(data.fen);

        displayGame = (game + 1).toString();
        displayPosition = (position + 1).toString();
        document.getElementById("gameInput").value = displayGame;
        document.getElementById("positionInput").value = displayPosition;
        document.getElementById("fen").textContent = data.fen;
        document.getElementById("pgn").textContent = data.pgn;
        document.getElementById("gameInfo").textContent = `Game ${displayGame} of ${game_count}`;
        document.getElementById("positionInfo").textContent = `Position ${displayPosition} of ${position_count}`;
        document.getElementById("evaluation").textContent = data.evaluation;

        data.policy.sort((a, b) => { return b.target - a.target; });

        clearMoves(data.policy[0]);
        for (const move of data.policy) {
            addMove(move);
        }

        overlaysHighlightAll();
    }

    function handleUciData(uciData) {
        // Add the new UCI data to history, clearing history first if it's a new search.
        if (dataHistory.length > 0) {
            const last = dataHistory[dataHistory.length - 1];
            if ((uciData.fen !== last.fen) || (uciData.line !== last.line) || (uciData.node_count < last.node_count)) {
                dataHistory = [];
                data = null;
            }
        }
        dataHistory.push(uciData);

        // If we were showing the latest data, show the new data.
        if (!data || (data === dataHistory[dataHistory.length - 2])) {
            data = uciData;
            renderUciData();
        }

        // Update the global UI.
        document.getElementById("uciControls").style.display = "block";
        range = document.getElementById("nodeCountRange");
        range.min = 0
        range.max = (dataHistory.length - 1);
        if (data === uciData) {
            range.valueAsNumber = (dataHistory.length - 1);
        }
    }

    function handleCommentary(commentary) {
        element = document.getElementById("commentary");
        element.textContent = "";
        const list = document.createElement("ul");
        for (const comment of commentary.commentary) {
            const line = document.createElement("li");
            line.textContent = comment;
            list.appendChild(line);
        }
        element.appendChild(list);
    }

    function handleCommentarySuite(commentary) {
        // Wipe out the whole info section and clear highlights.
        info = document.getElementById("info");
        info.textContent = "";
        clearHighlights();

        // Fill in the info section with commentary suite outputs.
        const suite_count = commentary.commentary.length;
        for (let i = 0; i < suite_count; i++) {
            info.appendChild(document.createElement("hr"));

            const fenLine = document.createElement("div");
            const fenPre = document.createElement("span");
            fenPre.innerText = "FEN: ";
            const fenLink = document.createElement("a");
            fenLink.href = "javascript:void(0)";
            const fen = commentary.fens[i];
            fenLink.innerText = fen;
            fenLink.addEventListener("click", (e) => {
                renderPosition(fen);
            });
            fenLine.appendChild(fenPre);
            fenLine.appendChild(fenLink);
            info.appendChild(fenLine);

            const baseline = document.createElement("div");
            baseline.innerText = `Baseline: ${commentary.baselines[i]}`;
            baseline.style.fontWeight = "bold";
            info.appendChild(baseline);

            const list = document.createElement("ul");
            for (const comment of commentary.commentary[i]) {
                const commentLine = document.createElement("li");
                commentLine.innerText = comment;
                list.appendChild(commentLine);
            }
            list.style.margin = "5px 0px";
            info.appendChild(list);
        }
        info.appendChild(document.createElement("hr"));

        // Hack the info section into commentary suite mode.
        info.style.width = "800px";
        info.style.height = "900px";
        info.style.overflow = "auto";

        // Render the first position.
        if (suite_count) {
            renderPosition(commentary.fens[0]);
        }
    }

    function renderUciData() {
        renderPosition(data.fen);

        document.getElementById("fen").textContent = data.fen;
        document.getElementById("line").textContent = data.line || "(home)";
        document.getElementById("nodeCount").textContent = `${data.node_count} nodes`;
        document.getElementById("evaluation").textContent = data.evaluation;
        document.getElementById("principleVariation").textContent = "Principle variation: " + data.principle_variation;

        data.policy.sort((a, b) => { return b.target - a.target; });

        clearMoves(data.policy[0]);
        for (const move of data.policy) {
            addMove(move);
        }

        overlaysHighlightAll();
    }

    function renderPosition(fen) {
        board.position(fen);
        document.getElementById("commentary").textContent = "";
    }

    const config = {
        pieceTheme: "chessboardjs/img/chesspieces/wikipedia/{piece}.png",
        position: "start",
    };

    const board = Chessboard("board", config);
    const squares = {};
    const overlays = {};
    let game_count = 0;
    let game = 0;
    let position_count = 0;
    let position = 0;
    let data = null;
    let dataHistory = [];

    for (const file of "abcdefgh") {
        for (let rank = 1; rank <= 8; rank++) {
            const square = file + rank;
            const squareElement = document.querySelector(".square-" + square);
            const squareOverlay = document.createElement("div");
            squareOverlay.style.width = getComputedStyle(squareElement, null).width;
            squareOverlay.style.height = getComputedStyle(squareElement, null).height;
            squareOverlay.style.position = "absolute";
            squareOverlay.style.top = "0px";
            squareOverlay.style.left = "0px";
            squareElement.appendChild(squareOverlay);

            squares[square] = squareElement;
            overlays[square] = squareOverlay;
        }
    }

    messageHandlers = {
        "ping": () => { },
        "initialize": handleInitialize,
        "training_data": handleTrainingData,
        "uci_data": handleUciData,
        "commentary_response": handleCommentary,
        "commentary_suite_response": handleCommentarySuite,
    };

    const socket = new WebSocket("ws://localhost:8001/");
    socket.addEventListener("message", (e) => {
        message = JSON.parse(e.data);
        messageHandlers[message.type](message);
    });
    socket.addEventListener("open", (e) => {
        socket.send(JSON.stringify({ type: "hello" }));
    });

    document.getElementById("gamePrevBig").addEventListener("click", (e) => {
        if (game_count > 0) {
            game -= 2000;
            position = null;
            requestPosition();
        }
    });
    document.getElementById("gamePrev").addEventListener("click", (e) => {
        if (game_count > 0) {
            game--;
            position = null;
            requestPosition();
        }
    });
    document.getElementById("gameNext").addEventListener("click", (e) => {
        if (game_count > 0) {
            game++;
            position = null;
            requestPosition();
        }
    });
    document.getElementById("gameNextBig").addEventListener("click", (e) => {
        if (game_count > 0) {
            game += 2000;
            position = null;
            requestPosition();
        }
    });

    document.getElementById("positionPrevBig").addEventListener("click", (e) => {
        if (game_count > 0) {
            // Don't accidentally wrap to negative: -1 means last.
            position = Math.max(0, position - 10);
            requestPosition();
        }
    });
    document.getElementById("positionPrev").addEventListener("click", (e) => {
        if (game_count > 0) {
            // Don't accidentally wrap to negative: -1 means last.
            position = Math.max(0, position - 1);
            requestPosition();
        }
    });
    document.getElementById("positionNext").addEventListener("click", (e) => {
        if (game_count > 0) {
            position++;
            requestPosition();
        }
    });
    document.getElementById("positionNextBig").addEventListener("click", (e) => {
        if (game_count > 0) {
            position += 10;
            requestPosition();
        }
    });

    gameInput = document.getElementById("gameInput");
    gameInput.addEventListener("focus", (e) => {
        gameInput.select();
    });
    gameInput.addEventListener("blur", (e) => {
        if (game_count > 0) {
            value = (parseInt(gameInput.value) - 1);
            if (!isNaN(value) && (value !== game)) {
                game = value;
                position = null;
                requestPosition();
            } else {
                gameInput.value = (game + 1).toString();
            }
        }
    });
    positionInput = document.getElementById("positionInput");
    positionInput.addEventListener("focus", (e) => {
        positionInput.select();
    });
    positionInput.addEventListener("blur", (e) => {
        if (game_count > 0) {
            value = (parseInt(positionInput.value) - 1);
            if (!isNaN(value) && (value !== position)) {
                position = value;
                requestPosition();
            } else {
                positionInput.value = (position + 1).toString();
            }
        }
    });

    document.getElementById("nodeCountRange").addEventListener("input", (e) => {
        newData = dataHistory[e.target.value];
        if (newData !== data) {
            data = newData;
            renderUciData();
        }
    });

    document.getElementById("lineBack").addEventListener("click", () => {
        if (data && data.line) {
            line = data.line.substring(0, data.line.lastIndexOf(" "));
            showLine(line);
        }
    });

    document.getElementById("lineHome").addEventListener("click", () => {
        showLine("");
    });

    document.getElementById("requestCommentary").addEventListener("click", () => {
        requestCommentary();
    });

    document.getElementById("commentarySuite").addEventListener("click", () => {
        runCommentarySuite();
    });
})();