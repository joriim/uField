/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// μField: "sync the open project" from anywhere in the UI (the map's Sync button,
// the Drive tab), through the sync_project tool, with the result shown as a
// notification. Served as project/MMUfieldSync.qml.

import QtQuick

Item {
  id: root

  //! The tool's result: counts, conflicts and busy_files, or { error, message }
  signal finished( var result )

  property var pendingCalls: ({})

  function syncOpenProject() {
    track( __ufieldTools.callFromUi( "sync_project", { "direction": "both" } ) )
  }

  //! Report the result of another call with the same result shape (download_project)
  function track( callId ) {
    let pending = root.pendingCalls
    pending[ callId ] = true
    root.pendingCalls = pending
  }

  function describe( result ) {
    if ( result.error !== undefined ) {
      return qsTr( "Sync failed: %1" ).arg( result.message )
    }
    let text = qsTr( "Synced: %1 uploaded, %2 downloaded, %3 deleted" )
                 .arg( result.uploaded ).arg( result.downloaded ).arg( result.deleted )
    if ( result.conflicts.length > 0 ) {
      text += "\n" + qsTr( "%n file(s) changed on two devices; both versions were kept", "", result.conflicts.length )
    }
    if ( result.busy_files.length > 0 ) {
      text += "\n" + qsTr( "Not synced while being edited: %1. Sync again after saving." ).arg( result.busy_files.join( ", " ) )
    }
    return text
  }

  function notify( result ) {
    if ( result.error === "already_exists" || result.error === "busy" || result.error === "no_open_project" ) {
      __notificationModel.addError( result.message )
    }
    else if ( result.error !== undefined ) {
      __notificationModel.addError( root.describe( result ) )
    }
    else if ( result.conflicts.length > 0 || result.busy_files.length > 0 ) {
      __notificationModel.addWarning( root.describe( result ) )
    }
    else {
      __notificationModel.addSuccess( root.describe( result ) )
    }
  }

  Connections {
    target: __ufieldTools

    function onToolFinished( callId, result ) {
      if ( root.pendingCalls[ callId ] === undefined ) {
        return
      }
      let pending = root.pendingCalls
      delete pending[ callId ]
      root.pendingCalls = pending
      root.notify( result )
      root.finished( result )
    }
  }
}
