import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 390
    height: 760
    visible: true
    title: qsTr("Student AI")
    color: "#0b1020"

    ListModel {
        id: chatModel
    }

    function appendMessage(sender, text) {
        chatModel.append({
            "sender": sender,
            "text": text,
            "time": new Date().toLocaleTimeString(Qt.locale(), "hh:mm AP")
        });
        scrollTimer.restart();
    }

    function sendMessage() {
        var text = messageField.text.trim();
        if (text.length === 0) {
            return;
        }

        appendMessage("user", text);
        messageField.text = "";

        var reply = mobileAgent.sendMessage(text);
        if (reply.length > 0) {
            appendMessage("agent", reply);
        }
    }

    Timer {
        id: scrollTimer
        interval: 20
        repeat: false
        onTriggered: chatList.positionViewAtEnd()
    }

    FileDialog {
        id: packageDialog
        title: "Import Student AI"
        nameFilters: ["AI packages (*.ai)", "All files (*)"]
        onAccepted: {
            var result = mobileAgent.importAgentPackage(selectedFile);
            statusText.text = result;
            appendMessage("system", result);
        }
    }

    Component.onCompleted: {
        appendMessage("system", mobileAgent.statusText);
    }

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 104
        color: "#101936"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: "#24304f"
        }

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.top: parent.top
            anchors.topMargin: 16
            text: "Student AI"
            color: "#f8fafc"
            font.pixelSize: 22
            font.bold: true
        }

        Rectangle {
            anchors.left: titleText.right
            anchors.leftMargin: 10
            anchors.verticalCenter: titleText.verticalCenter
            width: modelStateText.implicitWidth + 18
            height: 24
            radius: 4
            color: mobileAgent.modelLoaded ? "#064e3b" : "#3f1d1d"
            border.color: mobileAgent.modelLoaded ? "#10b981" : "#ef4444"

            Text {
                id: modelStateText
                anchors.centerIn: parent
                text: mobileAgent.modelLoaded ? "LOCAL" : "EMPTY"
                color: "#ffffff"
                font.pixelSize: 10
                font.bold: true
            }
        }

        Text {
            id: statusText
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.right: importButton.left
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            text: mobileAgent.statusText
            color: "#a8b3cf"
            font.pixelSize: 12
            maximumLineCount: 2
            elide: Text.ElideRight
            wrapMode: Text.Wrap
        }

        Button {
            id: importButton
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            width: 104
            height: 34
            text: "Import .ai"
            onClicked: packageDialog.open()
        }
    }

    ListView {
        id: chatList
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: composer.top
        anchors.margins: 10
        clip: true
        spacing: 8
        model: chatModel

        delegate: Item {
            width: chatList.width
            height: bubble.height + 18

            readonly property bool isUser: model.sender === "user"
            readonly property bool isSystem: model.sender === "system"

            Rectangle {
                id: bubble
                width: Math.min(parent.width * 0.84, messageText.implicitWidth + 28)
                height: messageText.implicitHeight + timestampText.implicitHeight + 24
                radius: 8
                color: isUser ? "#1d4ed8" : (isSystem ? "#172033" : "#5b21b6")
                border.color: isSystem ? "#2b3757" : "transparent"
                anchors.right: isUser ? parent.right : undefined
                anchors.left: isUser ? undefined : parent.left
                anchors.top: parent.top

                Text {
                    id: messageText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    text: model.text
                    color: "#ffffff"
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                Text {
                    id: timestampText
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 7
                    text: model.time
                    color: "#cbd5e1"
                    opacity: 0.72
                    font.pixelSize: 10
                }
            }
        }
    }

    Rectangle {
        id: composer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 76
        color: "#101936"
        border.color: "#24304f"

        TextField {
            id: messageField
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: sendButton.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            height: 44
            placeholderText: "Message..."
            color: "#f8fafc"
            font.pixelSize: 14
            selectByMouse: true
            onAccepted: sendMessage()
            background: Rectangle {
                radius: 8
                color: "#0b1020"
                border.color: messageField.activeFocus ? "#60a5fa" : "#263554"
            }
        }

        Button {
            id: sendButton
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 74
            height: 44
            text: "Send"
            onClicked: sendMessage()
        }
    }
}
