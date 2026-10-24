/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// μField: the Google Drive tab of the project list (ADR 0004). Every action is a
// tool call through __ufieldTools (list_projects, download_project, sync_project),
// the same calls the voice agent and MCP make (CLAUDE.md rule 1); this file only
// shows results. __ufieldDriveSync.busy tells whether a sync is running.
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

  //! call id -> "list"; sync and download results are reported by syncReporter
  property var pendingCalls: ({})

  function callTool( kind, tool, args ) {
    let pending = root.pendingCalls
    pending[ __ufieldTools.callFromUi( tool, args ) ] = kind
    root.pendingCalls = pending
  }

  function refreshProjectsList() {
    if ( !__ufieldDriveSync.busy ) {
      callTool( "list", "list_projects", { "storage": "google_drive" } )
    }
  }

  Connections {
    target: __ufieldTools

    function onToolFinished( callId, result ) {
      const kind = root.pendingCalls[ callId ]
      if ( kind === undefined ) {
        return // a call made elsewhere
      }
      let pending = root.pendingCalls
      delete pending[ callId ]
      root.pendingCalls = pending

      if ( kind === "list" ) {
        root.driveProjects = result.projects !== undefined ? result.projects : []
        root.listError = result.error !== undefined ? result.message : ""
        root.listed = true
      }
    }
  }

  MMUfieldSync {
    id: syncReporter
    onFinished: root.refreshProjectsList()
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
      onClicked: syncReporter.syncOpenProject()
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
        onClicked: syncReporter.track( __ufieldTools.callFromUi( "download_project",
                                         { "storage": "google_drive", "project_id": modelData.project_id, "name": modelData.name } ) )
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
