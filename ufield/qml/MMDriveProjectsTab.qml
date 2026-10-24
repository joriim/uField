/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// μField: the Google Drive tab of the project list (ADR 0004). All work happens
// in DriveSync (__ufieldDriveSync), the same code the sync_project tool uses;
// this file only shows state and forwards taps (CLAUDE.md rule 1).
// Served as project/MMDriveProjectsTab.qml next to upstream's project tabs.

import QtQuick

import "../components"

Item {
  id: root

  //! Label of this tab's button in the project list footer
  readonly property string tabTitle: qsTr( "Drive" )

  //! Path of the open project's .qgz/.qgs, or empty
  property string activeProjectFilePath: ""

  property var driveProjects: []
  property string listError: ""
  property bool listed: false

  function refreshProjectsList() {
    if ( !__ufieldDriveSync.busy ) {
      __ufieldDriveSync.listDriveProjects()
    }
  }

  function describe( result ) {
    if ( !result.ok ) {
      return qsTr( "Sync failed: %1" ).arg( result.error )
    }
    let text = qsTr( "Synced: %1 uploaded, %2 downloaded, %3 deleted" )
                 .arg( result.uploaded ).arg( result.downloaded ).arg( result.deleted )
    if ( result.conflicts.length > 0 ) {
      text += "\n" + qsTr( "%n file(s) changed on two devices; both versions were kept", "", result.conflicts.length )
    }
    return text
  }

  Connections {
    target: __ufieldDriveSync

    function onProjectsListed( projects, error ) {
      root.driveProjects = projects
      root.listError = error
      root.listed = true
    }

    function onSyncFinished( result ) {
      if ( result.ok && result.conflicts.length === 0 ) {
        __notificationModel.addSuccess( root.describe( result ) )
      }
      else if ( result.ok ) {
        __notificationModel.addWarning( root.describe( result ) )
      }
      else {
        __notificationModel.addError( root.describe( result ) )
      }
      root.refreshProjectsList()
    }
  }

  Column {
    id: header

    width: parent.width
    spacing: __style.margin12

    MMText {
      width: parent.width
      wrapMode: Text.WordWrap
      font: __style.p5
      color: __style.nightColor
      text: qsTr( "Projects are kept in the μField folder of your Google Drive. μField can only see the files it created there." )
    }

    MMButton {
      width: parent.width
      text: qsTr( "Sync this project with Google Drive" )
      enabled: root.activeProjectFilePath !== "" && !__ufieldDriveSync.busy
      onClicked: __ufieldDriveSync.syncProject( root.activeProjectFilePath, "both" )
    }

    MMText {
      width: parent.width
      visible: root.activeProjectFilePath === ""
      wrapMode: Text.WordWrap
      font: __style.p6
      color: __style.nightColor
      text: qsTr( "Open a project to sync it." )
    }

    MMText {
      width: parent.width
      wrapMode: Text.WordWrap
      font: __style.t3
      color: __style.forestColor
      text: qsTr( "In Google Drive" )
    }
  }

  MMBusyIndicator {
    anchors.centerIn: parent
    running: __ufieldDriveSync.busy
  }

  MMListView {
    id: list

    anchors {
      top: header.bottom
      topMargin: __style.margin12
      left: parent.left
      right: parent.right
      bottom: parent.bottom
    }

    clip: true
    model: root.driveProjects

    delegate: MMListDelegate {
      width: ListView.view.width
      text: modelData.name

      rightContent: MMButton {
        text: qsTr( "Download" )
        size: MMButton.Sizes.Small
        type: MMButton.Types.Secondary
        enabled: !__ufieldDriveSync.busy
        onClicked: {
          if ( !__ufieldDriveSync.downloadProject( modelData.folder_id, modelData.name ) ) {
            __notificationModel.addError( qsTr( "A project named %1 is already on this device." ).arg( modelData.name ) )
          }
        }
      }
    }

    footer: MMText {
      width: ListView.view.width
      visible: root.listed && ( root.listError !== "" || root.driveProjects.length === 0 )
      wrapMode: Text.WordWrap
      topPadding: __style.margin12
      font: __style.p6
      color: __style.nightColor
      text: root.listError !== "" ? qsTr( "Could not read Google Drive: %1" ).arg( root.listError )
                                  : qsTr( "No projects in Google Drive yet. Sync a project to add it." )
    }
  }
}
