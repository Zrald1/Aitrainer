import QtQuick
import QtQuick.Controls

Window {
    id: root
    width: 1080
    height: 720
    minimumWidth: 980
    minimumHeight: 680
    visible: true
    title: qsTr("AI Trainer - Cognitive Memory Dashboard")
    color: "#0a0a14"

    // Styling Constants
    readonly property string fontFamily: "Segoe UI"
    readonly property color colorBgDark: "#07070f"
    readonly property color colorCardBg: "#121226"
    readonly property color colorCardBorder: "#252545"
    readonly property color colorPrimary: "#8b5cf6"
    readonly property color colorPrimaryGrad: "#7c3aed"
    readonly property color colorSecondary: "#3b82f6"
    readonly property color colorTextLight: "#ffffff"
    readonly property color colorTextMuted: "#94a3b8"
    readonly property color colorBubbleUser: "#1d4ed8"
    readonly property color colorBubbleAgent: "#5b21b6"
    readonly property int cardRadius: 8
    readonly property int controlHeight: 30
    readonly property int labelWidth: 76

    // App state
    property int activeTab: 1 // 0 = Inspector, 1 = Simulator
    property bool showApiKey: false
    property bool showHfToken: false

    Component.onCompleted: {
        agent.loadMemory();
        updateDashboard();
        
        chatModel.append({
            "sender": "agent",
            "text": "Hello! Ask a question or send a statement. I will check my answer first. Normal chat is read-only unless you start explicit training.",
            "timestamp": new Date().toLocaleTimeString(Qt.locale(), "hh:mm AP")
        });
    }

    Connections {
        target: agent
        function onMemoryChanged() { updateDashboard(); }
        function onSimulationLogChanged() {
            simConsole.text = agent.simulationLog;
            // Force auto-scroll to the bottom of the console log
            simConsole.cursorPosition = simConsole.text.length;
        }
        function onSimulationMessageAdded(sender, text) {
            chatModel.append({
                "sender": sender,
                "text": text,
                "timestamp": new Date().toLocaleTimeString(Qt.locale(), "hh:mm AP")
            });
            scrollTimer.start();
        }
    }

    function updateDashboard() {
        statsVocabText.text = agent.vocabularySize;
        statsAssocText.text = agent.totalAssociations;
        statsFileText.text = agent.databasePath.split('/').pop().split('\\').pop();
        updateInspector();
        updateTopAssociations();
    }

    function updateInspector() {
        inspectorModel.clear();
        var searchWord = searchInput.text.trim().toLowerCase();
        if (searchWord !== "") {
            var list = agent.getAssociationsForWord(searchWord);
            for (var i = 0; i < list.length; i++) {
                inspectorModel.append({
                    "word": list[i].word,
                    "count": list[i].count,
                    "percentage": list[i].percentage
                });
            }
            inspectorTitleText.text = "Transitions for '" + searchWord + "'";
        } else {
            inspectorTitleText.text = "Brain Inspector";
        }
    }

    function updateTopAssociations() {
        topModel.clear();
        var list = agent.getTopAssociations(8);
        for (var i = 0; i < list.length; i++) {
            topModel.append({
                "word": list[i].word,
                "nextWord": list[i].nextWord,
                "count": list[i].count
            });
        }
    }

    function sendMessage() {
        var text = chatInputField.text.trim();
        if (text === "") return;

        chatModel.append({
            "sender": "user",
            "text": text,
            "timestamp": new Date().toLocaleTimeString(Qt.locale(), "hh:mm AP")
        });

        chatInputField.text = "";
        var reply = agent.learnAndRespond(text);

        chatModel.append({
            "sender": "agent",
            "text": reply,
            "timestamp": new Date().toLocaleTimeString(Qt.locale(), "hh:mm AP")
        });

        scrollTimer.start();
    }

    function chatSenderName(sender) {
        if (sender === "user") return "User";
        if (sender === "teacher") return "Tutor";
        if (sender === "system") return "System";
        return "Student AI";
    }

    function chatTranscript() {
        var lines = [];
        for (var i = 0; i < chatModel.count; i++) {
            var item = chatModel.get(i);
            var timestamp = item.timestamp && item.timestamp.length > 0 ? item.timestamp : "--:--";
            lines.push("[" + timestamp + "] " + chatSenderName(item.sender) + ":\n" + item.text);
        }
        return lines.join("\n\n");
    }

    function showCopyStatus(message) {
        chatCopyStatusText.text = message;
        copyStatusTimer.restart();
    }

    function copyChatTranscript() {
        var transcript = chatTranscript();
        showCopyStatus(agent.copyTextToClipboard(transcript) ? "Copied chat" : "Nothing to copy");
    }

    function copySimulationLog() {
        showCopyStatus(agent.copyTextToClipboard(agent.simulationLog) ? "Copied log" : "No log yet");
    }

    Timer {
        id: scrollTimer
        interval: 50
        repeat: false
        onTriggered: chatListView.positionViewAtEnd()
    }

    Timer {
        id: copyStatusTimer
        interval: 1800
        repeat: false
        onTriggered: chatCopyStatusText.text = ""
    }

    Item {
        anchors.fill: parent
        anchors.margins: 15

        // Left Panel (Chat Interface)
        Rectangle {
            id: leftPanel
            width: parent.width * 0.50 - 10
            height: parent.height
            anchors.left: parent.left
            color: colorCardBg
            border.color: colorCardBorder
            border.width: 1
            radius: root.cardRadius
            clip: true

            // Chat Header
            Rectangle {
                id: chatHeader
                width: parent.width; height: 55; color: "#181830"; anchors.top: parent.top; radius: root.cardRadius
                Rectangle { width: parent.width; height: 12; color: parent.color; anchors.bottom: parent.bottom }

                Text {
                    anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 20
                    width: parent.width - headerActions.width - 45
                    text: "Training Session"; color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 16; font.bold: true
                    elide: Text.ElideRight
                }

                Row {
                    id: headerActions
                    width: implicitWidth
                    height: implicitHeight
                    anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 20; spacing: 6
                    Rectangle {
                        width: 72; height: 24; radius: 4; color: "#1f1f3a"; border.color: colorCardBorder
                        Text { text: "Copy Chat"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                        MouseArea { anchors.fill: parent; onClicked: copyChatTranscript() }
                    }
                    Rectangle {
                        width: 66; height: 24; radius: 4; color: "#1f1f3a"; border.color: colorCardBorder
                        Text { text: "Copy Log"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                        MouseArea { anchors.fill: parent; onClicked: copySimulationLog() }
                    }
                    Text {
                        id: chatCopyStatusText
                        text: ""
                        color: "#10b981"; font.family: root.fontFamily; font.pixelSize: 9; font.bold: true
                        width: text.length > 0 ? 64 : 0
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: agent.learningEnabled ? "#10b981" : "#ef4444"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: agent.learningEnabled ? "LEARNING ACTIVE" : "STANDBY"
                        color: agent.learningEnabled ? "#10b981" : "#ef4444"
                        font.family: root.fontFamily; font.pixelSize: 10; font.bold: true
                    }
                }
            }

            // Messages ListView
            ListView {
                id: chatListView; width: parent.width - 20; anchors.top: chatHeader.bottom; anchors.bottom: inputContainer.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.margins: 10; clip: true
                model: ListModel { id: chatModel }
                spacing: 10

                delegate: Item {
                    width: chatListView.width
                    height: isSystem ? (systemText.contentHeight + 16) : (bubbleBg.height + 22)
                    property bool isUser: model.sender === "user"
                    property bool isTeacher: model.sender === "teacher"
                    property bool isSystem: model.sender === "system"

                    // System notifications
                    TextEdit {
                        id: systemText
                        visible: isSystem
                        text: model.text
                        color: "#10b981"
                        font.pixelSize: 10
                        font.family: root.fontFamily
                        font.bold: true
                        width: parent.width - 20
                        height: contentHeight
                        readOnly: true
                        selectByMouse: true
                        selectByKeyboard: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        selectedTextColor: "#ffffff"
                        selectionColor: colorPrimary
                        horizontalAlignment: TextEdit.AlignHCenter
                        anchors.centerIn: parent
                    }

                    // Timestamp indicator
                    Text {
                        visible: !isSystem
                        text: model.timestamp; color: colorTextMuted; font.pixelSize: 9; font.family: root.fontFamily
                        anchors.horizontalCenter: bubbleBg.horizontalCenter; anchors.bottom: bubbleBg.top; anchors.bottomMargin: 2
                    }

                    Rectangle {
                        id: bubbleBg
                        visible: !isSystem
                        width: Math.min(Math.max(180, msgText.implicitWidth + 20), parent.width * 0.82); height: msgText.contentHeight + 16; radius: root.cardRadius
                        // User: Blue, Teacher: Emerald Green, Agent: Purple
                        color: isUser ? colorBubbleUser : (isTeacher ? "#047857" : colorBubbleAgent)
                        anchors.right: (isUser || isTeacher) ? parent.right : undefined
                        anchors.left: (!isUser && !isTeacher) ? parent.left : undefined
                        anchors.rightMargin: (isUser || isTeacher) ? 5 : 0; anchors.leftMargin: (!isUser && !isTeacher) ? 5 : 0

                        TextEdit {
                            id: msgText; text: (isTeacher ? "[Tutor] " : "") + model.text; color: colorTextLight; font.pixelSize: 12; font.family: root.fontFamily
                            readOnly: true
                            selectByMouse: true
                            selectByKeyboard: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            selectedTextColor: "#ffffff"
                            selectionColor: colorPrimary
                            width: parent.width - 20
                            height: contentHeight
                            anchors.centerIn: parent
                        }
                    }
                }
            }

            // Input Bar & Action Buttons
            Rectangle {
                id: inputContainer
                width: parent.width; height: 95; color: "#181830"; anchors.bottom: parent.bottom; border.color: colorCardBorder; border.width: 1; radius: root.cardRadius
                Rectangle { width: parent.width; height: 12; color: parent.color; anchors.top: parent.top }

                Rectangle {
                    id: textInputWrapper
                    height: 36; color: colorBgDark; border.color: chatInputField.focus ? colorPrimary : colorCardBorder; border.width: 1; radius: 6
                    anchors.left: parent.left; anchors.leftMargin: 15; anchors.top: parent.top; anchors.topMargin: 10
                    anchors.right: sendBtn.left; anchors.rightMargin: 10

                    TextInput {
                        id: chatInputField; anchors.fill: parent; anchors.margins: 8; color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 12; selectByMouse: true; verticalAlignment: Text.AlignVCenter
                        clip: true
                        Keys.onReturnPressed: sendMessage()

                        Text {
                            text: "Ask a question or type a statement..."; color: "#4e4e7a"; font.family: root.fontFamily; font.pixelSize: 12
                            visible: parent.text.length === 0 && !parent.focus; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            elide: Text.ElideRight
                        }
                    }
                }

                Rectangle {
                    id: sendBtn
                    width: 60; height: 36; radius: 6
                    color: sendMouse.containsPress ? colorPrimaryGrad : (sendMouse.containsMouse ? colorPrimary : "#302654")
                    anchors.right: parent.right; anchors.rightMargin: 15; anchors.top: parent.top; anchors.topMargin: 10

                    Text { text: "Send"; color: colorTextLight; font.bold: true; font.family: root.fontFamily; font.pixelSize: 11; anchors.centerIn: parent }
                    MouseArea { id: sendMouse; anchors.fill: parent; hoverEnabled: true; onClicked: sendMessage() }
                }

                Row {
                    spacing: 8; anchors.left: parent.left; anchors.leftMargin: 15; anchors.bottom: parent.bottom; anchors.bottomMargin: 10

                    Rectangle {
                        width: 90; height: 22; radius: 4; color: agent.learningEnabled ? "#065f46" : "#451a03"; border.color: agent.learningEnabled ? "#10b981" : "#f97316"
                        Text { text: agent.learningEnabled ? "Learn: ACTIVE" : "Learn: PAUSED"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                        MouseArea { anchors.fill: parent; onClicked: agent.learningEnabled = !agent.learningEnabled }
                    }

                    Rectangle {
                        width: 50; height: 22; radius: 4; color: "#1e1b4b"; border.color: "#4f46e5"
                        Text { text: "SAVE"; color: "#c7d2fe"; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                var success = agent.saveMemory();
                                chatModel.append({ "sender": "agent", "text": success ? "Cognitive state saved successfully." : "Error: Failed to save state.", "timestamp": new Date().toLocaleTimeString(Qt.locale(), "hh:mm AP") });
                                scrollTimer.start();
                            }
                        }
                    }

                    Rectangle {
                        width: 50; height: 22; radius: 4; color: "#1e1b4b"; border.color: "#4f46e5"
                        Text { text: "LOAD"; color: "#c7d2fe"; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                var success = agent.loadMemory();
                                chatModel.append({ "sender": "agent", "text": success ? "Loaded stored cognitive database." : "Error loading database file.", "timestamp": new Date().toLocaleTimeString(Qt.locale(), "hh:mm AP") });
                                scrollTimer.start();
                            }
                        }
                    }


                }
            }
        }

        // Right Panel (TabView: Inspector vs Simulator)
        Rectangle {
            id: rightPanel
            width: parent.width * 0.50
            height: parent.height
            anchors.right: parent.right
            color: colorCardBg
            border.color: colorCardBorder; border.width: 1; radius: root.cardRadius; clip: true

            // Tab bar header
            Rectangle {
                id: tabHeader
                width: parent.width; height: 45; color: "#151528"
                anchors.top: parent.top; radius: root.cardRadius
                Rectangle { width: parent.width; height: 10; color: parent.color; anchors.bottom: parent.bottom }

                Row {
                    anchors.fill: parent; anchors.margins: 5
                    spacing: 5

                    // Tab Button 1: Inspector
                    Rectangle {
                        width: (parent.width - 5) / 2; height: parent.height; radius: 6
                        color: activeTab === 0 ? colorPrimary : "transparent"
                        Text { text: "Brain Inspector"; color: colorTextLight; font.bold: true; font.family: root.fontFamily; font.pixelSize: 12; width: parent.width - 12; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; anchors.centerIn: parent }
                        MouseArea { anchors.fill: parent; onClicked: activeTab = 0 }
                    }

                    // Tab Button 2: Simulator
                    Rectangle {
                        width: (parent.width - 5) / 2; height: parent.height; radius: 6
                        color: activeTab === 1 ? colorPrimary : "transparent"
                        Text { text: "AI Teacher Simulator"; color: colorTextLight; font.bold: true; font.family: root.fontFamily; font.pixelSize: 12; width: parent.width - 12; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; anchors.centerIn: parent }
                        MouseArea { anchors.fill: parent; onClicked: activeTab = 1 }
                    }
                }
            }

            // Tab 1 Contents: Brain Inspector
            ScrollView {
                visible: activeTab === 0
                anchors.top: tabHeader.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 15
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                Column {
                    width: parent.width
                    spacing: 12

                    // Stats Grid
                    Rectangle {
                        width: parent.width; height: 75; color: colorBgDark; border.color: colorCardBorder; radius: 8
                        Grid {
                            anchors.fill: parent; anchors.margins: 10; columns: 3; spacing: 8
                            Column {
                                width: (parent.width - 16) / 3; spacing: 2
                                Text { text: "VOCABULARY"; color: colorTextMuted; font.pixelSize: 8; font.bold: true; font.family: root.fontFamily }
                                Text { id: statsVocabText; text: "0"; color: colorPrimary; font.pixelSize: 20; font.bold: true; font.family: root.fontFamily }
                            }
                            Column {
                                width: (parent.width - 16) / 3; spacing: 2
                                Text { text: "CONNECTIONS"; color: colorTextMuted; font.pixelSize: 8; font.bold: true; font.family: root.fontFamily }
                                Text { id: statsAssocText; text: "0"; color: colorSecondary; font.pixelSize: 20; font.bold: true; font.family: root.fontFamily }
                            }
                            Column {
                                width: (parent.width - 16) / 3; spacing: 2
                                Text { text: "PERSISTENT DB"; color: colorTextMuted; font.pixelSize: 8; font.bold: true; font.family: root.fontFamily }
                                Text { id: statsFileText; text: "None"; color: "#10b981"; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; width: parent.width; font.family: root.fontFamily }
                            }
                        }
                    }

                    // Hyperparameters
                    Rectangle {
                        width: parent.width; height: 110; color: colorBgDark; border.color: colorCardBorder; radius: 8
                        Column {
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            Text { text: "AGENT HYPERPARAMETERS"; color: colorTextMuted; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily }

                            Row {
                                width: parent.width; spacing: 10
                                Text { text: "Temperature: " + agent.temperature.toFixed(2); color: colorTextLight; font.pixelSize: 11; width: 90; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Slider {
                                    id: tempSlider; from: 0.0; to: 2.0; value: agent.temperature; width: parent.width - 110; anchors.verticalCenter: parent.verticalCenter
                                    onMoved: agent.temperature = value
                                }
                            }

                            Row {
                                width: parent.width; spacing: 10
                                Text { text: "Context Window:"; color: colorTextLight; font.pixelSize: 11; width: 90; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Row {
                                    spacing: 8
                                    Rectangle {
                                        width: 80; height: 24; radius: 4; color: agent.contextWindow === 1 ? colorPrimary : "#1f1f3a"; border.color: colorCardBorder
                                        Text { text: "Mono-gram (1)"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                                        MouseArea { anchors.fill: parent; onClicked: agent.contextWindow = 1 }
                                    }
                                    Rectangle {
                                        width: 80; height: 24; radius: 4; color: agent.contextWindow === 2 ? colorPrimary : "#1f1f3a"; border.color: colorCardBorder
                                        Text { text: "Bi-gram (2)"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                                        MouseArea { anchors.fill: parent; onClicked: agent.contextWindow = 2 }
                                    }
                                }
                            }
                        }
                    }

                    // Interactive Memory Explorer
                    Rectangle {
                        width: parent.width; height: 200; color: colorBgDark; border.color: colorCardBorder; radius: 8
                        Column {
                            anchors.fill: parent; anchors.margins: 10; spacing: 6
                            Text { id: inspectorTitleText; text: "Brain Inspector"; color: colorTextMuted; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily }

                            Rectangle {
                                width: parent.width; height: 28; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                TextInput {
                                    id: searchInput; anchors.fill: parent; anchors.margins: 6; color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 11; selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                    onTextChanged: updateInspector()
                                    Text { text: "Type word to see child connections..."; color: "#4e4e7a"; font.family: root.fontFamily; font.pixelSize: 11; visible: parent.text.length === 0; anchors.verticalCenter: parent.verticalCenter }
                                }
                            }

                            ListView {
                                id: inspectorListView; width: parent.width; height: 115; clip: true; spacing: 5
                                model: ListModel { id: inspectorModel }
                                delegate: Item {
                                    width: inspectorListView.width; height: 20
                                    Row {
                                        width: parent.width; spacing: 8
                                        Rectangle {
                                            width: 75; height: 18; color: "#1e1b4b"; radius: 3; anchors.verticalCenter: parent.verticalCenter
                                            Text { text: model.word; color: "#c7d2fe"; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; anchors.centerIn: parent; font.family: root.fontFamily }
                                            MouseArea { anchors.fill: parent; onClicked: searchInput.text = model.word }
                                        }
                                        Item {
                                            width: parent.width - 135; height: 10; anchors.verticalCenter: parent.verticalCenter
                                            Rectangle { anchors.fill: parent; color: "#121226"; radius: 3 }
                                            Rectangle { width: parent.width * model.percentage; height: parent.height; color: colorPrimary; radius: 3 }
                                            Text { text: (model.percentage * 100).toFixed(0) + "%"; color: colorTextLight; font.pixelSize: 8; anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                        }
                                        Text { text: "(" + model.count + ")"; color: colorTextMuted; font.pixelSize: 9; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                    }
                                }
                            }
                        }
                    }

                    // Top Core Associations
                    Rectangle {
                        width: parent.width; height: 180; color: colorBgDark; border.color: colorCardBorder; radius: 8
                        Column {
                            anchors.fill: parent; anchors.margins: 10; spacing: 6
                            Text { text: "CORE ASSOCIATIONS (TOP STRENGTH)"; color: colorTextMuted; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily }

                            ListView {
                                id: topAssocListView; width: parent.width; height: 135; clip: true; spacing: 4
                                model: ListModel { id: topModel }
                                delegate: Item {
                                    width: topAssocListView.width; height: 20
                                    Row {
                                        width: parent.width; spacing: 5; anchors.verticalCenter: parent.verticalCenter
                                        Rectangle {
                                            width: 65; height: 14; color: "#111827"; radius: 3
                                            Text { text: model.word; color: colorTextMuted; font.pixelSize: 8; elide: Text.ElideRight; anchors.centerIn: parent; font.family: root.fontFamily }
                                            MouseArea { anchors.fill: parent; onClicked: searchInput.text = model.word }
                                        }
                                        Text { text: "➜"; color: colorPrimary; font.pixelSize: 9; font.family: root.fontFamily }
                                        Rectangle {
                                            width: 65; height: 14; color: "#1e1b4b"; radius: 3
                                            Text { text: model.nextWord; color: "#a78bfa"; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight; anchors.centerIn: parent; font.family: root.fontFamily }
                                            MouseArea { anchors.fill: parent; onClicked: searchInput.text = model.nextWord }
                                        }
                                        Text { text: "reinforced " + model.count + " times"; color: colorTextMuted; font.pixelSize: 9; font.family: root.fontFamily; width: parent.width - 158; elide: Text.ElideRight }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Tab 2 Contents: AI Teacher Simulator (Featherless Integration)
            ScrollView {
                visible: activeTab === 1
                anchors.top: tabHeader.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 15
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                Column {
                    width: parent.width
                    spacing: 12

                    // Learning Curve & Progress Card
                    Rectangle {
                        width: parent.width; height: 220; color: colorBgDark; border.color: colorCardBorder; radius: root.cardRadius
                        Column {
                            id: chartCol
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            readonly property real contentWidth: width

                            Row {
                                width: parent.width; height: 20; spacing: 8
                                Text {
                                    text: "LEARNING CURVE"
                                    color: colorTextLight; font.pixelSize: 12; font.bold: true; font.family: root.fontFamily
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: agent.testScores.length + " assessments"
                                    color: colorTextMuted; font.pixelSize: 10; font.family: root.fontFamily
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                width: chartCol.contentWidth; height: 170; spacing: 10

                                Rectangle {
                                    width: chartCol.contentWidth - 125; height: 170; color: "#07070f"; border.color: colorCardBorder; radius: 4
                                    Canvas {
                                        id: chartCanvas
                                        anchors.fill: parent; anchors.margins: 8
                                        onPaint: {
                                            var ctx = getContext("2d");
                                            ctx.clearRect(0, 0, width, height);

                                            var scores = agent.testScores;
                                            var maxVisiblePoints = 16;
                                            var startIndex = scores.length > maxVisiblePoints ? scores.length - maxVisiblePoints : 0;
                                            var displayScores = [];
                                            for (var idx = startIndex; idx < scores.length; idx++) {
                                                displayScores.push(scores[idx]);
                                            }

                                            var graphLeft = 28;
                                            var graphRight = width - 8;
                                            var graphTop = 12;
                                            var graphBottom = height - 24;
                                            var graphHeight = graphBottom - graphTop;
                                            var graphWidth = graphRight - graphLeft;

                                            ctx.strokeStyle = "#1a1a36";
                                            ctx.lineWidth = 1;
                                            ctx.fillStyle = "#94a3b8";
                                            ctx.font = "9px sans-serif";
                                            for (var label = 0; label <= 100; label += 25) {
                                                var gy = graphBottom - (label / 100.0) * graphHeight;
                                                ctx.beginPath();
                                                ctx.moveTo(graphLeft, gy);
                                                ctx.lineTo(graphRight, gy);
                                                ctx.stroke();
                                                ctx.fillText(label, 2, gy + 3);
                                            }

                                            if (displayScores.length === 0) {
                                                ctx.fillStyle = "#4e4e7a";
                                                ctx.font = "10px sans-serif";
                                                ctx.fillText("No assessments yet.", graphLeft + 8, graphTop + 34);
                                                ctx.fillText("Start the simulator to plot scores here.", graphLeft + 8, graphTop + 50);
                                                return;
                                            }

                                            function scoreX(i) {
                                                return displayScores.length === 1
                                                    ? graphLeft + graphWidth / 2
                                                    : graphLeft + i * (graphWidth / (displayScores.length - 1));
                                            }

                                            function scoreY(score) {
                                                return graphBottom - (Math.max(0, Math.min(100, score)) / 100.0) * graphHeight;
                                            }

                                            if (displayScores.length > 1) {
                                                ctx.strokeStyle = "#10b981";
                                                ctx.lineWidth = 2;
                                                ctx.beginPath();
                                                for (var i = 0; i < displayScores.length; i++) {
                                                    var px = scoreX(i);
                                                    var py = scoreY(displayScores[i].score);
                                                    if (i === 0) ctx.moveTo(px, py);
                                                    else ctx.lineTo(px, py);
                                                }
                                                ctx.stroke();
                                            }

                                            for (var dot = 0; dot < displayScores.length; dot++) {
                                                var s = displayScores[dot].score;
                                                var x = scoreX(dot);
                                                var y = scoreY(s);

                                                ctx.fillStyle = s >= 95 ? "#10b981" : (s >= 60 ? "#f59e0b" : "#ef4444");
                                                ctx.beginPath();
                                                ctx.arc(x, y, 4, 0, Math.PI * 2);
                                                ctx.fill();

                                                ctx.fillStyle = "#ffffff";
                                                ctx.font = "bold 9px sans-serif";
                                                ctx.fillText(s + "%", Math.max(0, Math.min(width - 28, x - 10)), Math.max(10, y - 8));
                                            }
                                        }
                                        Connections {
                                            target: agent
                                            function onTestScoresChanged() { chartCanvas.requestPaint(); }
                                        }
                                        Component.onCompleted: requestPaint()
                                    }
                                }

                                Column {
                                    width: 115; height: 170; spacing: 7
                                    Text { text: "LAST SCORE"; color: colorTextMuted; font.pixelSize: 8; font.bold: true; font.family: root.fontFamily }
                                    Text {
                                        text: agent.lastTestResult.score !== undefined ? agent.lastTestResult.score + "%" : "--"
                                        color: "#10b981"; font.pixelSize: 26; font.bold: true; font.family: root.fontFamily
                                    }
                                    Text {
                                        text: agent.lastTestResult.feedback !== undefined ? agent.lastTestResult.feedback : "Waiting for the first assessment"
                                        color: colorTextMuted; font.pixelSize: 9; wrapMode: Text.Wrap; width: parent.width; height: 74; maximumLineCount: 6; elide: Text.ElideRight; font.family: root.fontFamily
                                    }
                                    Rectangle {
                                        width: parent.width; height: 24; radius: 4; color: "#1f1f3a"; border.color: colorCardBorder
                                        Text { text: "Clear History"; color: colorTextLight; font.pixelSize: 8; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                                        MouseArea { anchors.fill: parent; onClicked: agent.clearTestScores() }
                                    }
                                }
                            }
                        }
                    }

                    // API Settings Card
                    Rectangle {
                        width: parent.width; height: 174; color: colorBgDark; border.color: colorCardBorder; radius: root.cardRadius
                        Column {
                            id: apiSettingsCol
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            readonly property real contentWidth: width
                            
                            Text { text: "API SETTINGS"; color: colorTextMuted; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily }

                            // API Key Row
                            Row {
                                width: apiSettingsCol.contentWidth; height: root.controlHeight; spacing: 8
                                Text { text: "API key"; color: colorTextLight; font.pixelSize: 11; width: root.labelWidth; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                
                                Rectangle {
                                    width: apiSettingsCol.contentWidth - root.labelWidth - 62 - 16; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        id: apiKeyField; anchors.fill: parent; anchors.margins: 5; color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 11; selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        echoMode: showApiKey ? TextInput.Normal : TextInput.Password
                                        text: agent.featherlessApiKey
                                        onTextEdited: agent.featherlessApiKey = text
                                        clip: true
                                        Text { text: "Enter Featherless API key"; color: "#4e4e7a"; font.family: root.fontFamily; font.pixelSize: 11; visible: parent.text.length === 0; width: parent.width; elide: Text.ElideRight }
                                    }
                                }

                                // Show/Hide Button
                                Rectangle {
                                    width: 62; height: root.controlHeight; radius: 4; color: "#1f1f3a"; border.color: colorCardBorder
                                    Text { text: showApiKey ? "Hide" : "Show"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                                    MouseArea { anchors.fill: parent; onClicked: showApiKey = !showApiKey }
                                }
                            }

                            // Hugging Face Token Row
                            Row {
                                width: apiSettingsCol.contentWidth; height: root.controlHeight; spacing: 8
                                Text { text: "HF token"; color: colorTextLight; font.pixelSize: 11; width: root.labelWidth; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }

                                Rectangle {
                                    width: apiSettingsCol.contentWidth - root.labelWidth - 62 - 16; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        id: hfTokenField; anchors.fill: parent; anchors.margins: 5; color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 11; selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        echoMode: showHfToken ? TextInput.Normal : TextInput.Password
                                        text: agent.huggingFaceToken
                                        onTextEdited: agent.huggingFaceToken = text
                                        clip: true
                                        Text { text: "Optional token for private datasets"; color: "#4e4e7a"; font.family: root.fontFamily; font.pixelSize: 11; visible: parent.text.length === 0; width: parent.width; elide: Text.ElideRight }
                                    }
                                }

                                Rectangle {
                                    width: 62; height: root.controlHeight; radius: 4; color: "#1f1f3a"; border.color: colorCardBorder
                                    Text { text: showHfToken ? "Hide" : "Show"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent }
                                    MouseArea { anchors.fill: parent; onClicked: showHfToken = !showHfToken }
                                }
                            }

                            // Teacher Model Input
                            Row {
                                width: apiSettingsCol.contentWidth; height: root.controlHeight; spacing: 8
                                Text { text: "Teacher model"; color: colorTextLight; font.pixelSize: 11; width: root.labelWidth; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Rectangle {
                                    width: apiSettingsCol.contentWidth - root.labelWidth - 8; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        id: modelNameField; anchors.fill: parent; anchors.margins: 5; color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 11; selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        text: agent.teacherModel
                                        onTextEdited: agent.teacherModel = text
                                        clip: true
                                    }
                                }
                            }
                        }
                    }

                    // Agent Package and LoRA Training Card
                    Rectangle {
                        width: parent.width; height: 666; color: colorBgDark; border.color: colorCardBorder; radius: root.cardRadius
                        Column {
                            id: packageCol
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            readonly property real contentWidth: width

                            Text { text: "AGENT PACKAGE AND C++ LORA TRAINING"; color: colorTextMuted; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily }

                            Rectangle {
                                width: parent.width; height: 58; color: "#0b0b18"; border.color: colorCardBorder; radius: 4; clip: true
                                Text {
                                    text: agent.agentFilesSummary()
                                    color: colorTextMuted; font.pixelSize: 9; wrapMode: Text.Wrap; width: parent.width - 12; anchors.centerIn: parent; font.family: root.fontFamily
                                }
                            }

                            Row {
                                width: packageCol.contentWidth; height: root.controlHeight; spacing: 8
                                Rectangle {
                                    width: packageCol.contentWidth - 196; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        id: packagePathField
                                        anchors.fill: parent; anchors.margins: 6
                                        color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 11
                                        selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        clip: true
                                        text: "student_agent.ai"
                                    }
                                }
                                Rectangle {
                                    width: 90; height: root.controlHeight; radius: 4; color: "#065f46"; border.color: "#10b981"
                                    Text { text: "Export .ai"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; width: parent.width - 8; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; anchors.centerIn: parent }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: packageStatusText.text = agent.exportAgentPackage(packagePathField.text)
                                    }
                                }
                                Rectangle {
                                    width: 90; height: root.controlHeight; radius: 4; color: "#1e3a8a"; border.color: "#3b82f6"
                                    Text { text: "Import .ai"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; width: parent.width - 8; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; anchors.centerIn: parent }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            packageStatusText.text = agent.importAgentPackage(packagePathField.text);
                                            updateDashboard();
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: parent.width; height: 92; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                TextArea {
                                    id: loraTrainingText
                                    anchors.fill: parent; anchors.margins: 6
                                    color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 10
                                    wrapMode: Text.Wrap
                                    placeholderText: "Paste examples, corrections, Q&A, or skills to train the C++ LoRA-like adapter..."
                                    background: Rectangle { color: "transparent" }
                                }
                            }

                            Row {
                                width: packageCol.contentWidth; height: root.controlHeight; spacing: 8
                                Rectangle {
                                    width: packageCol.contentWidth - 126; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        id: datasetUrlField
                                        anchors.fill: parent; anchors.margins: 6
                                        color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 10
                                        selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        clip: true
                                        Text {
                                            text: "Dataset repo id, URL, or file: MAKILINGDING/english_dictionary"
                                            color: "#4e4e7a"; font.family: root.fontFamily; font.pixelSize: 10
                                            visible: parent.text.length === 0
                                            width: parent.width; elide: Text.ElideRight
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                }
                                Rectangle {
                                    width: 118; height: root.controlHeight; radius: 4; color: "#0f766e"; border.color: "#14b8a6"
                                    Text { text: "Train Dataset"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; width: parent.width - 8; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; anchors.centerIn: parent }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            packageStatusText.text = agent.trainLoraFromDatasetUrl(datasetUrlField.text, 4);
                                            updateDashboard();
                                        }
                                    }
                                }
                            }

                            Row {
                                width: packageCol.contentWidth; height: root.controlHeight; spacing: 8
                                Rectangle {
                                    width: 118; height: root.controlHeight; radius: 4; color: colorPrimary; border.color: colorPrimaryGrad
                                    Text { text: "Train C++ LoRA"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; width: parent.width - 8; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; anchors.centerIn: parent }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            packageStatusText.text = agent.trainLoraFromText(loraTrainingText.text, 4);
                                            updateDashboard();
                                        }
                                    }
                                }
                                Rectangle {
                                    width: 126; height: root.controlHeight; radius: 4; color: "#1f1f3a"; border.color: colorCardBorder
                                    Text { text: "HF LoRA Guide"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; width: parent.width - 8; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; anchors.centerIn: parent }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: Qt.openUrlExternally("https://huggingface.co/docs/peft/main/conceptual_guides/lora")
                                    }
                                }
                                Text {
                                    text: agent.loraTrainingSummary()
                                    color: colorTextMuted; font.pixelSize: 9; wrapMode: Text.Wrap
                                    width: packageCol.contentWidth - 260; height: parent.height; maximumLineCount: 2; elide: Text.ElideRight; font.family: root.fontFamily
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                width: packageCol.contentWidth; height: root.controlHeight; spacing: 8
                                CheckBox {
                                    id: localGpuCheck
                                    width: 138; height: root.controlHeight
                                    checked: agent.useLocalGpuTraining
                                    text: "Use local GPU"
                                    spacing: 7
                                    indicator: Rectangle {
                                        implicitWidth: 16; implicitHeight: 16
                                        x: localGpuCheck.leftPadding
                                        y: (localGpuCheck.height - height) / 2
                                        radius: 3
                                        color: localGpuCheck.checked ? "#064e3b" : "#111827"
                                        border.color: localGpuCheck.checked ? "#10b981" : colorCardBorder
                                        Rectangle {
                                            width: 8; height: 8; radius: 2
                                            anchors.centerIn: parent
                                            visible: localGpuCheck.checked
                                            color: "#10b981"
                                        }
                                    }
                                    contentItem: Text {
                                        text: localGpuCheck.text
                                        color: colorTextLight
                                        font.pixelSize: 10
                                        font.bold: true
                                        font.family: root.fontFamily
                                        verticalAlignment: Text.AlignVCenter
                                        leftPadding: localGpuCheck.indicator.width + localGpuCheck.spacing
                                        elide: Text.ElideRight
                                    }
                                    onToggled: agent.useLocalGpuTraining = checked
                                }
                                Text {
                                    text: agent.localGpuTrainingStatus
                                    color: colorTextMuted; font.pixelSize: 9; wrapMode: Text.Wrap
                                    width: packageCol.contentWidth - 146; height: parent.height; maximumLineCount: 2; elide: Text.ElideRight; font.family: root.fontFamily
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Text { text: "DIGITALOCEAN GPU REMOTE TRAINING"; color: colorTextMuted; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily }

                            Row {
                                width: packageCol.contentWidth; height: root.controlHeight; spacing: 8
                                Text { text: "GPU host"; color: colorTextLight; font.pixelSize: 11; width: root.labelWidth; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Rectangle {
                                    width: packageCol.contentWidth - root.labelWidth - 8; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        anchors.fill: parent; anchors.margins: 6
                                        color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 10
                                        selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        clip: true
                                        text: agent.gpuHost
                                        onTextEdited: agent.gpuHost = text
                                        Text {
                                            text: "DigitalOcean GPU IP or hostname"
                                            color: "#4e4e7a"; font.family: root.fontFamily; font.pixelSize: 10
                                            visible: parent.text.length === 0
                                            width: parent.width; elide: Text.ElideRight
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                }
                            }

                            Row {
                                width: packageCol.contentWidth; height: root.controlHeight; spacing: 8
                                Text { text: "User"; color: colorTextLight; font.pixelSize: 11; width: 36; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Rectangle {
                                    width: Math.max(70, (packageCol.contentWidth - 36 - 34 - 54 - 32 - 92 - 40) * 0.35); height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        anchors.fill: parent; anchors.margins: 6
                                        color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 10
                                        selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        clip: true
                                        text: agent.gpuUsername
                                        onTextEdited: agent.gpuUsername = text
                                    }
                                }
                                Text { text: "Port"; color: colorTextLight; font.pixelSize: 11; width: 34; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Rectangle {
                                    width: 54; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        anchors.fill: parent; anchors.margins: 6
                                        color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 10
                                        selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        clip: true
                                        text: String(agent.gpuSshPort)
                                        validator: IntValidator { bottom: 1; top: 65535 }
                                        onTextEdited: {
                                            var value = parseInt(text);
                                            if (!isNaN(value)) {
                                                agent.gpuSshPort = value;
                                            }
                                        }
                                    }
                                }
                                Text { text: "Max"; color: colorTextLight; font.pixelSize: 11; width: 32; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Rectangle {
                                    width: 92; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        anchors.fill: parent; anchors.margins: 6
                                        color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 10
                                        selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        clip: true
                                        text: String(agent.gpuMaxSamples)
                                        validator: IntValidator { bottom: 1; top: 10000000 }
                                        onTextEdited: {
                                            var value = parseInt(text);
                                            if (!isNaN(value)) {
                                                agent.gpuMaxSamples = value;
                                            }
                                        }
                                    }
                                }
                            }

                            Row {
                                width: packageCol.contentWidth; height: root.controlHeight; spacing: 8
                                Text { text: "SSH key"; color: colorTextLight; font.pixelSize: 11; width: root.labelWidth; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Rectangle {
                                    width: packageCol.contentWidth - root.labelWidth - 8; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        anchors.fill: parent; anchors.margins: 6
                                        color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 10
                                        selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        clip: true
                                        text: agent.gpuSshKeyPath
                                        onTextEdited: agent.gpuSshKeyPath = text
                                        Text {
                                            text: "Optional private key path"
                                            color: "#4e4e7a"; font.family: root.fontFamily; font.pixelSize: 10
                                            visible: parent.text.length === 0
                                            width: parent.width; elide: Text.ElideRight
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                }
                            }

                            Row {
                                width: packageCol.contentWidth; height: root.controlHeight; spacing: 8
                                Text { text: "Remote dir"; color: colorTextLight; font.pixelSize: 11; width: root.labelWidth; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Rectangle {
                                    width: packageCol.contentWidth - root.labelWidth - 8; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        anchors.fill: parent; anchors.margins: 6
                                        color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 10
                                        selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        clip: true
                                        text: agent.gpuRemoteRoot
                                        onTextEdited: agent.gpuRemoteRoot = text
                                    }
                                }
                            }

                            Row {
                                width: packageCol.contentWidth; height: root.controlHeight; spacing: 8
                                Rectangle {
                                    width: 118; height: root.controlHeight; radius: 4
                                    color: agent.isGpuTrainingRunning ? "#374151" : "#7c2d12"
                                    border.color: agent.isGpuTrainingRunning ? "#64748b" : "#fb923c"
                                    Text { text: agent.isGpuTrainingRunning ? "GPU Running" : "Train on GPU"; color: colorTextLight; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily; width: parent.width - 8; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; anchors.centerIn: parent }
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: !agent.isGpuTrainingRunning
                                        onClicked: {
                                            packageStatusText.text = agent.trainCurrentAgentOnGpuServer(datasetUrlField.text, 4);
                                            updateDashboard();
                                        }
                                    }
                                }
                                Text {
                                    text: agent.gpuTrainingStatus
                                    color: colorTextMuted; font.pixelSize: 9; wrapMode: Text.Wrap
                                    width: packageCol.contentWidth - 126; height: parent.height; maximumLineCount: 2; elide: Text.ElideRight; font.family: root.fontFamily
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Text {
                                id: packageStatusText
                                text: "Ready."
                                color: "#10b981"; font.pixelSize: 9; wrapMode: Text.Wrap; width: parent.width; height: 34; maximumLineCount: 3; elide: Text.ElideRight; font.family: root.fontFamily
                            }
                        }
                    }

                    // Simulation Parameters Card
                    Rectangle {
                        width: parent.width; height: 166; color: colorBgDark; border.color: colorCardBorder; radius: root.cardRadius
                        Column {
                            id: simParamsCol
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            readonly property real contentWidth: width
                            
                            Text { text: "SIMULATION CONTROLS"; color: colorTextMuted; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily }

                            // Topic Input
                            Row {
                                width: simParamsCol.contentWidth; height: root.controlHeight; spacing: 8
                                Text { text: "Topic"; color: colorTextLight; font.pixelSize: 11; width: root.labelWidth; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Rectangle {
                                    width: simParamsCol.contentWidth - root.labelWidth - 8; height: root.controlHeight; color: colorCardBg; border.color: colorCardBorder; radius: 4
                                    TextInput {
                                        id: simTopicField; anchors.fill: parent; anchors.margins: 5; color: colorTextLight; font.family: root.fontFamily; font.pixelSize: 11; selectByMouse: true; verticalAlignment: Text.AlignVCenter
                                        text: "artificial intelligence"
                                        clip: true
                                        Text { text: "Topic to obey, for example: coding python"; color: "#4e4e7a"; font.family: root.fontFamily; font.pixelSize: 11; visible: parent.text.length === 0; width: parent.width; elide: Text.ElideRight }
                                    }
                                }
                            }

                            // Delay input
                            Row {
                                width: simParamsCol.contentWidth; height: root.controlHeight; spacing: 8
                                Text { text: "Delay " + simDelaySlider.value.toFixed(1) + "s"; color: colorTextLight; font.pixelSize: 11; width: root.labelWidth; anchors.verticalCenter: parent.verticalCenter; font.family: root.fontFamily }
                                Slider {
                                    id: simDelaySlider; from: 1.0; to: 5.0; value: 2.0; stepSize: 0.5; width: simParamsCol.contentWidth - root.labelWidth - 8; anchors.verticalCenter: parent.verticalCenter
                                    Component.onCompleted: value = agent.simulationDelay / 1000
                                    onMoved: agent.simulationDelay = value * 1000
                                }
                            }

                            // Start/Stop Row
                            Row {
                                width: simParamsCol.contentWidth; height: 32; spacing: 10

                                Rectangle {
                                    width: 108; height: 32; radius: 6
                                    color: agent.isSimulationRunning ? "#7f1d1d" : colorPrimary
                                    border.color: agent.isSimulationRunning ? "#ef4444" : colorPrimaryGrad
                                    Text {
                                        text: agent.isSimulationRunning ? "Stop" : "Start"
                                        color: colorTextLight; font.pixelSize: 11; font.bold: true; font.family: root.fontFamily; anchors.centerIn: parent
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            if (agent.isSimulationRunning) {
                                                agent.stopSimulation();
                                            } else {
                                                chatModel.clear();
                                                chatModel.append({
                                                    "sender": "system",
                                                    "text": "Starting simulation. Long-term student memory is retained and reinforced.",
                                                    "timestamp": new Date().toLocaleTimeString(Qt.locale(), "hh:mm AP")
                                                });
                                                agent.startSimulation(simTopicField.text, 0);
                                            }
                                        }
                                    }
                                }

                                Column {
                                    width: simParamsCol.contentWidth - 118; spacing: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: agent.isSimulationRunning

                                    Row {
                                        width: parent.width; spacing: 6
                                        Rectangle {
                                            width: 8; height: 8; radius: 4; color: "#10b981"
                                            anchors.verticalCenter: parent.verticalCenter
                                            SequentialAnimation on opacity {
                                                loops: Animation.Infinite
                                                NumberAnimation { from: 1.0; to: 0.3; duration: 800 }
                                                NumberAnimation { from: 0.3; to: 1.0; duration: 800 }
                                            }
                                        }
                                        Text {
                                            text: "SIMULATOR ACTIVE"
                                            color: "#10b981"; font.pixelSize: 10; font.bold: true; font.family: root.fontFamily; width: parent.width - 20; elide: Text.ElideRight
                                        }
                                    }

                                    Text {
                                        text: "Active Cycles: " + agent.simulationCurrentTurn
                                        color: colorTextMuted; font.pixelSize: 10; font.family: root.fontFamily; width: parent.width; elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }

                    // Terminal Live Dialogue Stream Console
                    Rectangle {
                        width: parent.width; height: 260; color: "#030307"; border.color: colorCardBorder; radius: 8
                        clip: true

                        Column {
                            anchors.fill: parent; anchors.margins: 10; spacing: 6
                            Text { text: "LIVE DIALOGUE STREAM"; color: "#00ff66"; font.pixelSize: 9; font.bold: true; font.family: root.fontFamily }

                            ScrollView {
                                width: parent.width; height: parent.height - 20; clip: true
                                TextArea {
                                    id: simConsole
                                    text: agent.simulationLog
                                    color: "#aaffcc"
                                    font.family: "Courier New"
                                    font.pixelSize: 10
                                    readOnly: true
                                    wrapMode: Text.Wrap
                                    background: Rectangle { color: "transparent" }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
