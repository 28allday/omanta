import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// The mount credential prompt — GMountOperation's ask-password, as a dialog.
// Which fields appear is the server's call: smb wants username, domain and
// password; sftp usually just the password; ftp may allow anonymous.
Dialog {
    id: root

    // What the current question needs, set by ask().
    property bool needsUsername: false
    property bool needsDomain: false
    property bool needsPassword: false
    property bool canAnonymous: false

    signal answered(string username, string domain, string password, bool anonymous, bool remember)
    signal dismissed()

    anchors.centerIn: Overlay.overlay
    width: 420
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Authentication required")

    function ask(message, defaultUser, defaultDomain, wantsUsername, wantsDomain, wantsPassword, allowsAnonymous) {
        messageText.text = message;
        needsUsername = wantsUsername;
        needsDomain = wantsDomain;
        needsPassword = wantsPassword;
        canAnonymous = allowsAnonymous;
        usernameField.text = defaultUser;
        domainField.text = defaultDomain;
        passwordField.text = "";
        anonymousCheck.checked = false;
        rememberCheck.checked = false;
        open();
        if (wantsUsername && defaultUser === "")
            usernameField.forceActiveFocus();
        else
            passwordField.forceActiveFocus();
    }

    Column {
        width: parent.width
        spacing: 10

        Text {
            id: messageText

            width: parent.width
            color: Colors.text
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        CheckBox {
            id: anonymousCheck

            visible: root.canAnonymous
            text: qsTr("Connect anonymously")
        }

        TextField {
            id: usernameField

            width: parent.width
            visible: root.needsUsername
            enabled: !anonymousCheck.checked
            placeholderText: qsTr("Username")
            color: Colors.text
            selectByMouse: true
        }

        TextField {
            id: domainField

            width: parent.width
            visible: root.needsDomain
            enabled: !anonymousCheck.checked
            placeholderText: qsTr("Domain")
            color: Colors.text
            selectByMouse: true
        }

        TextField {
            id: passwordField

            width: parent.width
            visible: root.needsPassword
            enabled: !anonymousCheck.checked
            placeholderText: qsTr("Password")
            echoMode: TextInput.Password
            color: Colors.text
            selectByMouse: true
            onAccepted: root.accept()
        }

        CheckBox {
            id: rememberCheck

            visible: root.needsPassword
            enabled: !anonymousCheck.checked
            text: qsTr("Remember password")
        }
    }

    onAccepted: root.answered(usernameField.text.trim(), domainField.text.trim(),
                              passwordField.text, anonymousCheck.checked,
                              rememberCheck.checked)
    onRejected: root.dismissed()
}
