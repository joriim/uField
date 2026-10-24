/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_SYNCENGINE_H
#define UFIELD_SYNCENGINE_H

#include <QString>
#include <QStringList>

#include "remotestore.h"
#include "syncplan.h"

namespace ufield
{

  //! The sync state of one project, kept in `<project>/.ufield/sync.json` (ADR 0004 §3).
  class SyncState
  {
    public:
      static QString filePath( const QString &projectDir );

      //! Reads the state; a missing file is an empty state (first sync). Returns false only for an unreadable or invalid file.
      static bool load( const QString &projectDir, sync::SyncedFiles &files, QString &error );

      //! Writes the state atomically.
      static bool save( const QString &projectDir, const sync::SyncedFiles &files, QString &error );
  };

  /**
   * The files in a project folder with their MD5s. A file whose size and mtime match its
   * sync record reuses the recorded hash, so unchanged media are not read again. Excluded
   * paths (sync::isExcluded) are left out. \a mtimes receives each file's mtime in ms.
   */
  bool scanProject( const QString &projectDir, const sync::SyncedFiles &synced, sync::LocalFiles &files,
                    std::map<std::string, std::int64_t> &mtimes, QString &error );

  //! `sync_project` direction: push and pull leave the other side's changes, and conflicts, for a later two-way sync.
  enum class SyncDirection
  {
    Both,
    Push, //!< local changes to the remote only
    Pull, //!< remote changes to the device only
  };

  struct SyncResult
  {
    bool ok = false;
    QString error;
    int uploaded = 0;
    int downloaded = 0;
    int deleted = 0;          //!< local and remote deletes
    QStringList conflictCopies; //!< local files renamed to keep both versions
    int skipped = 0;            //!< changes left for a later sync by a push or pull
  };

  /**
   * Syncs a project folder with a remote store: scan, plan (sync::planSync), apply. Blocks;
   * run it off the UI thread. The caller closes or checkpoints GeoPackages first so no data
   * sits in -wal files, which are never synced.
   *
   * The state is saved after every action, so a sync interrupted by a lost connection resumes
   * without redoing finished transfers. Downloads go to a temporary file in `.ufield/` and
   * replace the target only when complete.
   */
  class SyncEngine
  {
    public:
      SyncEngine( const QString &projectDir, RemoteStore &remote );

      //! \param conflictSuffix e.g. "conflict 2026-10-21 Pixel 8", see sync::conflictName
      SyncResult sync( const QString &conflictSuffix, SyncDirection direction = SyncDirection::Both );

    private:
      bool apply( const sync::Action &action, SyncResult &result, QString &error );
      bool downloadTo( const QString &remoteId, const std::string &path, QString &error );
      bool recordUpload( const std::string &path, const QString &remoteId, QString &error );
      bool saveState( QString &error );
      QString absolute( const std::string &path ) const;

      QString mDir;
      RemoteStore &mRemote;
      sync::LocalFiles mLocal;
      sync::RemoteFiles mRemoteFiles;
      sync::SyncedFiles mSynced;
      std::map<std::string, std::int64_t> mMtimes;
  };

} // namespace ufield

#endif // UFIELD_SYNCENGINE_H
