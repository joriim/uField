/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_REMOTESTORE_H
#define UFIELD_REMOTESTORE_H

#include <QString>

#include "syncplan.h"

namespace ufield
{

  /**
   * One project folder on a remote store (Google Drive, later OneDrive), as the
   * sync engine sees it. Paths are project-relative with '/' separators.
   *
   * Calls block until done: SyncEngine runs on a worker thread, never the UI
   * thread. Each call returns false and sets \a error on failure; the engine
   * stops at the first failure and the next sync resumes from the saved state.
   * Implementations must not put credentials in \a error.
   */
  class RemoteStore
  {
    public:
      virtual ~RemoteStore() = default;

      //! All files in the project folder and its subfolders. Files without an md5 (Google Docs) may be included with an empty md5.
      virtual bool list( sync::RemoteFiles &files, QString &error ) = 0;

      //! Writes the content of \a remoteId to \a localFile, replacing it.
      virtual bool download( const QString &remoteId, const QString &localFile, QString &error ) = 0;

      /**
       * Uploads \a localFile byte-for-byte. With an empty \a remoteId, creates \a path (and any
       * missing folders); otherwise replaces the content of \a remoteId. Sets \a uploaded to the
       * file as the remote now has it.
       */
      virtual bool upload( const QString &localFile, const QString &path, const QString &remoteId, sync::RemoteFile &uploaded, QString &error ) = 0;

      //! Moves \a remoteId to the remote trash (recoverable), not a permanent delete.
      virtual bool trash( const QString &remoteId, QString &error ) = 0;
  };

} // namespace ufield

#endif // UFIELD_REMOTESTORE_H
