/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// Sync planning for one project folder against a remote copy (Google Drive),
// ADR 0004 §3. Pure C++17 with no Qt, so the rules are unit-tested on their
// own; the Drive client and the file system live in GoogleDriveProvider.
//
// Inputs are three snapshots keyed by project-relative path ('/'-separated):
// the local files now, the remote files now, and both as recorded at the last
// successful sync. Hashes are MD5 hex strings, the same as Drive's
// md5Checksum, so local and remote content compare directly.

#ifndef UFIELD_SYNCPLAN_H
#define UFIELD_SYNCPLAN_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ufield::sync
{

  struct LocalFile
  {
    std::string md5;
    std::int64_t size = 0;
  };

  struct RemoteFile
  {
    std::string id;
    std::string md5; //!< empty for Google Workspace documents, which are not synced
    std::int64_t size = 0;
  };

  //! A file as it was on both sides after the last successful sync.
  struct SyncedFile
  {
    std::string remoteId;
    std::string md5;
    std::int64_t size = 0;
    std::int64_t localMtimeMs = 0; //!< lets a scan reuse `md5` when size and mtime are unchanged
  };

  enum class ActionType
  {
    UploadNew,    //!< create `path` on the remote from the local file
    Upload,       //!< replace the content of remote file `remoteId` with the local file
    Download,     //!< write remote file `remoteId` to `path`
    DeleteRemote, //!< move remote file `remoteId` to the remote trash
    DeleteLocal,  //!< delete the local file `path`
    //! Both sides changed differently: rename the local file to `conflictPath`, download
    //! remote `remoteId` to `path`, then upload `conflictPath` as a new remote file.
    Conflict,
    RecordOnly,   //!< both sides already equal; only update the sync record
    ForgetRecord, //!< gone on both sides; drop the sync record
  };

  struct Action
  {
    ActionType type;
    std::string path;
    std::string remoteId;
    std::string conflictPath;

    bool operator==( const Action &other ) const;
  };

  using LocalFiles = std::map<std::string, LocalFile>;
  using RemoteFiles = std::map<std::string, RemoteFile>;
  using SyncedFiles = std::map<std::string, SyncedFile>;

  //! Paths that are never synced: the sync state folder `.ufield/` and SQLite side files.
  bool isExcluded( const std::string &path );

  //! "dir/name (suffix).ext"; the extension is the part after the last dot of the file name.
  std::string conflictName( const std::string &path, const std::string &suffix );

  /**
   * The actions that bring local and remote to the same content, in path order.
   * \param conflictSuffix text for conflict copies, e.g. "conflict 2026-10-21 Pixel 8"
   *
   * Never loses data: a file changed on one side and deleted on the other is kept,
   * and when both sides changed differently both versions are kept.
   */
  std::vector<Action> planSync( const LocalFiles &local, const RemoteFiles &remote, const SyncedFiles &synced,
                                const std::string &conflictSuffix );

} // namespace ufield::sync

#endif // UFIELD_SYNCPLAN_H
