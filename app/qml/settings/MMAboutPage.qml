/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// UFIELD: μField About page — own name, GPL notice with source link, upstream credit as text
// (no Mergin Maps or Lutra Consulting logos, see docs/licensing.md §1.4)

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../components"

MMPage {
  id: root

  // Kept for MMSettingsController; opens the source code repository
  signal visitWebsiteClicked()

  pageHeader.title: qsTr( "About" )

  pageContent: ScrollView {

    width: parent.width
    height: parent.height

    contentWidth: availableWidth // to only scroll vertically
    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
      width: parent.width

      Item {
        Layout.preferredHeight: __style.margin40
        Layout.fillWidth: true
      }

      Column {
        id: middleContentGroup

        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height

        spacing: __style.spacing20

        Text {
          id: nameText

          text: "μField" // Do not translate
          font: __style.h1
          color: __style.forestColor
          anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
          id: vText

          text: "v" + __version
          font: __style.t4
          color: __style.deepOceanColor
          anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
          id: descText

          text: qsTr( "Field observations with location and metadata, stored where you choose." )
          font: __style.p5
          color: __style.nightColor
          wrapMode: Text.WordWrap
          width: sourceBtn.width
          horizontalAlignment: Text.AlignHCenter
          anchors.horizontalCenter: parent.horizontalCenter
        }

        MMButton {
          id: sourceBtn

          width: parent.width
          anchors.horizontalCenter: parent.horizontalCenter
          onClicked: root.visitWebsiteClicked()
          text: qsTr( "Source code (GPL-3.0)" )
        }
      }

      Item {
        Layout.preferredHeight: __style.margin40
        Layout.fillWidth: true
      }

      Column {
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height

        spacing: __style.spacing12

        Text {
          text: qsTr( "Published by Biomitta Oy" )
          font: __style.t4
          color: __style.deepOceanColor
          width: parent.width
          wrapMode: Text.WordWrap
          horizontalAlignment: Text.AlignHCenter
        }

        Text {
          text: qsTr( "Based on Mergin Maps mobile by Lutra Consulting Ltd and contributors, licensed under GPL-2.0-or-later." )
          font: __style.p6
          color: __style.nightColor
          width: parent.width
          wrapMode: Text.WordWrap
          horizontalAlignment: Text.AlignHCenter
        }

        Text {
          text: qsTr( "This program comes with ABSOLUTELY NO WARRANTY. It is free software, and you are welcome to redistribute it under the terms of the GNU General Public License version 3." )
          font: __style.p6
          color: __style.nightColor
          width: parent.width
          wrapMode: Text.WordWrap
          horizontalAlignment: Text.AlignHCenter
        }
      }

      Item {
        Layout.preferredHeight: __style.margin40
        Layout.fillWidth: true
      }
    }
  }
}
