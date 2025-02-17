/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import QtQuick 2.5
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14

import Mozilla.VPN 1.0
import components 0.1


VPNViewBase {
    id: settingsList
    objectName: "settingsView"

    //% "Settings"
    _menuTitle: qsTrId("vpn.main.settings")
    _viewContentData: ColumnLayout {
        spacing: VPNTheme.theme.windowMargin
        Layout.fillWidth: true

        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true

            VPNUserProfile {
                property bool subscriptionManagementEnabled: VPNFeatureList.get("subscriptionManagement").isSupported
                objectName: "settingsUserProfile"
                _iconSource: subscriptionManagementEnabled
                    ? "qrc:/nebula/resources/chevron.svg"
                    : "qrc:/nebula/resources/open-in-new.svg"
                _iconMirror: subscriptionManagementEnabled && VPNLocalizer.isRightToLeft
                _buttonOnClicked: () => {
                    if (subscriptionManagementEnabled) {
                        VPNProfileFlow.start();
                    } else {
                        VPNGleanDeprecated.recordGleanEvent("manageAccountClicked")
                        Glean.sample.manageAccountClicked.record();
                        VPNUrlOpener.openUrlLabel("account");
                    }
                }
                _loaderVisible: VPNProfileFlow.state === VPNProfileFlow.StateLoading

            }

            VPNVerticalSpacer {
                Layout.preferredHeight: VPNTheme.theme.windowMargin * 1
            }

            Rectangle {
                id: divider

                Layout.preferredHeight: 1
                Layout.fillWidth: true
                Layout.leftMargin: VPNTheme.theme.windowMargin
                Layout.rightMargin: VPNTheme.theme.windowMargin
                color: VPNTheme.colors.grey10
            }
        }


        // TODO: Move to subscription management
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: VPNTheme.theme.windowMargin /2
            Layout.rightMargin: VPNTheme.theme.windowMargin /2
            Layout.alignment: Qt.AlignHCenter

            VPNSettingsItem {
                objectName: "settingsTipsAndTricks"
                settingTitle: VPNl18n.TipsAndTricksSettingsEntryLabel
                imageLeftSrc: "qrc:/nebula/resources/sparkles.svg"
                imageRightSrc: "qrc:/nebula/resources/chevron.svg"
                imageRightMirror: VPNLocalizer.isRightToLeft
                onClicked: VPNNavigator.requestScreen(VPNNavigator.ScreenTipsAndTricks);
            }

            VPNSettingsItem {
                objectName: "settingsNetworking"
                settingTitle: VPNFeatureList.get("splitTunnel").isSupported ? qsTrId("vpn.settings.networking") : VPNl18n.CustomDNSSettingsDnsNavItem
                imageLeftSrc: "qrc:/ui/resources/settings/networkSettings.svg"
                imageRightSrc: "qrc:/nebula/resources/chevron.svg"
                imageRightMirror: VPNLocalizer.isRightToLeft
                onClicked: VPNFeatureList.get("splitTunnel").isSupported ? stackview.push("qrc:/ui/screens/settings/ViewNetworkSettings.qml", {
                                                      //% "App permissions"
                                                      _appPermissionsTitle: Qt.binding(() => qsTrId("vpn.settings.appPermissions2"))
                                                  }) : stackview.push("qrc:/ui/screens/settings/dnsSettings/ViewAdvancedDNSSettings.qml")
            }

            VPNSettingsItem {
                id: preferencesSetting
                objectName: "settingsPreferences"
                settingTitle: VPNl18n.SettingsSystemPreferences
                imageLeftSrc: "qrc:/ui/resources/settings/preferences.svg"
                imageRightSrc: "qrc:/nebula/resources/chevron.svg"
                imageRightMirror: VPNLocalizer.isRightToLeft
                onClicked: stackview.push("qrc:/ui/screens/settings/ViewPreferences.qml", {
                                                    _startAtBootTitle: Qt.binding(() => VPNl18n.SettingsStartAtBootTitle),
                                                    _languageTitle:  Qt.binding(() => qsTrId("vpn.settings.language")),
                                                    _notificationsTitle:  Qt.binding(() => qsTrId("vpn.settings.notifications")),
                                                    _menuTitle: Qt.binding(() => preferencesSetting.settingTitle)
                                                  })
            }

            VPNSettingsItem {
                objectName: "settingsDeviceList"
                //% "My devices"
                settingTitle: qsTrId("vpn.devices.myDevices")
                imageLeftSrc: "qrc:/nebula/resources/devices.svg"
                imageRightSrc: "qrc:/nebula/resources/chevron.svg"
                imageRightMirror: VPNLocalizer.isRightToLeft
                onClicked: stackview.push("qrc:/ui/screens/devices/ViewDevices.qml")
            }

            VPNSettingsItem {
                //% "Give feedback"
                property string giveFeedbackTitle: qsTrId("vpn.settings.giveFeedback")
                objectName: "settingsGetHelp"
                settingTitle: qsTrId("vpn.main.getHelp2")
                imageLeftSrc: "qrc:/ui/resources/settings/questionMark.svg"
                imageRightSrc: "qrc:/nebula/resources/chevron.svg"
                imageRightMirror: VPNLocalizer.isRightToLeft
                onClicked: {
                    VPNGleanDeprecated.recordGleanEvent("getHelpClickedViewSettings");
                    Glean.sample.getHelpClickedViewSettings.record();
                    VPNNavigator.requestScreen(VPNNavigator.ScreenGetHelp);
                }
            }

            VPNSettingsItem {
                objectName: "settingsAboutUs"
                settingTitle: qsTrId("vpn.settings.aboutUs")
                imageLeftSrc: "qrc:/ui/resources/settings/aboutUs.svg"
                imageRightSrc: "qrc:/nebula/resources/chevron.svg"
                imageRightMirror: VPNLocalizer.isRightToLeft
                onClicked: stackview.push("qrc:/ui/screens/settings/ViewAboutUs.qml")
            }

            VPNSignOut {
                id: signOutLink

                objectName: "settingsLogout"
                anchors {
                    horizontalCenter: undefined
                    bottom: undefined
                    bottomMargin: undefined
                }
                Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
                Layout.topMargin: VPNTheme.theme.vSpacingSmall - parent.spacing
            }

        }
    }

    Connections {
        target: VPNProfileFlow

        function onStateChanged() {
            if (
                VPNProfileFlow.state === VPNProfileFlow.StateReady
                && stackview.currentItem.objectName !== "subscriptionManagmentView"
            ) {
                return stackview.push("qrc:/ui/screens/settings/ViewSubscriptionManagement/ViewSubscriptionManagement.qml");
            }
            // Only push the profile view if it’s not already in the stack
            if (
                VPNProfileFlow.state === VPNProfileFlow.StateAuthenticationNeeded
                && stackview.currentItem.objectName !== "reauthenticationFlow"
            ) {
                return stackview.push("qrc:/ui/screens/settings/ViewSubscriptionManagement/ViewReauthenticationFlow.qml", {
                    _onClose: () => {
                        VPNProfileFlow.reset();
                        stackview.pop(null, StackView.Immediate);
                    }
                });
            }

            // An error occurred during the profile flow. Let’s reset and return
            // to the main settings view.
            const hasError = VPNProfileFlow.state === VPNProfileFlow.StateError;
            if (hasError) {
                if (stackview.currentItem.objectName === "reauthenticationFlow") {
                  stackview.pop(null, StackView.Immediate);
                }
                VPNProfileFlow.reset();
            }
        }
    }
    Component.onCompleted: {
        VPNGleanDeprecated.recordGleanEvent("settingsViewOpened");
        Glean.sample.settingsViewOpened.record();
    }
}
