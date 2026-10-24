/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_STORAGETOOLS_H
#define UFIELD_STORAGETOOLS_H

#include <QJsonArray>
#include <QString>

#include <functional>

namespace ufield
{
  class DriveSync;
  class ToolDispatcher;

  //! What the storage tools need from the app.
  struct StorageToolsHost
  {
    //! Folder of the open project, or empty.
    std::function<QString()> openProjectDir;
    //! Local projects as [{ project_id, name }].
    std::function<QJsonArray()> localProjects;
  };

  /**
   * Registers list_projects, download_project and sync_project (ADR 0004) on \a dispatcher,
   * backed by \a drive. Both must outlive the dispatcher's calls.
   *
   * Error codes: no_open_project, busy (another sync is running), not_available (a storage
   * that is not in the MVP), already_exists, not_found, sync_failed, storage_error.
   */
  void registerStorageTools( ToolDispatcher &dispatcher, DriveSync &drive, StorageToolsHost host );

} // namespace ufield

#endif // UFIELD_STORAGETOOLS_H
