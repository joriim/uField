/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "syncengine.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <filesystem>
#include <system_error>

namespace ufield
{

  namespace
  {
    const int kStateVersion = 1;

    QString qs( const std::string &s ) { return QString::fromStdString( s ); }

    bool md5Of( const QString &file, std::string &md5, QString &error )
    {
      QFile f( file );
      if ( !f.open( QIODevice::ReadOnly ) )
      {
        error = QStringLiteral( "Cannot read %1: %2" ).arg( file, f.errorString() );
        return false;
      }
      QCryptographicHash hash( QCryptographicHash::Md5 );
      if ( !hash.addData( &f ) )
      {
        error = QStringLiteral( "Cannot read %1" ).arg( file );
        return false;
      }
      md5 = hash.result().toHex().toStdString();
      return true;
    }

    std::int64_t mtimeMs( const QFileInfo &info ) { return info.lastModified().toMSecsSinceEpoch(); }
  } // namespace

  // SyncState

  QString SyncState::filePath( const QString &projectDir ) { return QDir( projectDir ).filePath( QStringLiteral( ".ufield/sync.json" ) ); }

  bool SyncState::load( const QString &projectDir, sync::SyncedFiles &files, QString &error )
  {
    files.clear();
    QFile f( filePath( projectDir ) );
    if ( !f.exists() )
      return true;
    if ( !f.open( QIODevice::ReadOnly ) )
    {
      error = QStringLiteral( "Cannot read the sync state: %1" ).arg( f.errorString() );
      return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( f.readAll(), &parseError );
    if ( !doc.isObject() || doc.object().value( QStringLiteral( "version" ) ).toInt() != kStateVersion )
    {
      error = QStringLiteral( "Invalid sync state %1" ).arg( f.fileName() );
      return false;
    }
    const QJsonObject jsonFiles = doc.object().value( QStringLiteral( "files" ) ).toObject();
    for ( auto it = jsonFiles.begin(); it != jsonFiles.end(); ++it )
    {
      const QJsonObject o = it.value().toObject();
      sync::SyncedFile record;
      record.remoteId = o.value( QStringLiteral( "id" ) ).toString().toStdString();
      record.md5 = o.value( QStringLiteral( "md5" ) ).toString().toStdString();
      record.size = static_cast<std::int64_t>( o.value( QStringLiteral( "size" ) ).toDouble() );
      record.localMtimeMs = static_cast<std::int64_t>( o.value( QStringLiteral( "mtime_ms" ) ).toDouble() );
      files[it.key().toStdString()] = record;
    }
    return true;
  }

  bool SyncState::save( const QString &projectDir, const sync::SyncedFiles &files, QString &error )
  {
    QJsonObject jsonFiles;
    for ( const auto &entry : files )
    {
      QJsonObject o;
      o[QStringLiteral( "id" )] = qs( entry.second.remoteId );
      o[QStringLiteral( "md5" )] = qs( entry.second.md5 );
      o[QStringLiteral( "size" )] = static_cast<double>( entry.second.size );
      o[QStringLiteral( "mtime_ms" )] = static_cast<double>( entry.second.localMtimeMs );
      jsonFiles[qs( entry.first )] = o;
    }
    QJsonObject root;
    root[QStringLiteral( "version" )] = kStateVersion;
    root[QStringLiteral( "files" )] = jsonFiles;

    const QString path = filePath( projectDir );
    QDir().mkpath( QFileInfo( path ).path() );
    QSaveFile f( path );
    if ( !f.open( QIODevice::WriteOnly ) || f.write( QJsonDocument( root ).toJson() ) < 0 || !f.commit() )
    {
      error = QStringLiteral( "Cannot write the sync state: %1" ).arg( f.errorString() );
      return false;
    }
    return true;
  }

  // Scan

  bool scanProject( const QString &projectDir, const sync::SyncedFiles &synced, sync::LocalFiles &files,
                    std::map<std::string, std::int64_t> &mtimes, QString &error )
  {
    files.clear();
    mtimes.clear();
    const QDir root( projectDir );
    QDirIterator it( projectDir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories );
    while ( it.hasNext() )
    {
      const QFileInfo info( it.next() );
      if ( info.isSymLink() )
        continue;
      const std::string path = root.relativeFilePath( info.filePath() ).toStdString();
      if ( sync::isExcluded( path ) )
        continue;

      sync::LocalFile file;
      file.size = info.size();
      const std::int64_t mtime = mtimeMs( info );
      const auto record = synced.find( path );
      if ( record != synced.end() && record->second.size == file.size && record->second.localMtimeMs == mtime && !record->second.md5.empty() )
        file.md5 = record->second.md5;
      else if ( !md5Of( info.filePath(), file.md5, error ) )
        return false;
      files[path] = file;
      mtimes[path] = mtime;
    }
    return true;
  }

  // SyncEngine

  SyncEngine::SyncEngine( const QString &projectDir, RemoteStore &remote )
    : mDir( projectDir )
    , mRemote( remote )
  {
  }

  QString SyncEngine::absolute( const std::string &path ) const { return QDir( mDir ).filePath( qs( path ) ); }

  bool SyncEngine::saveState( QString &error ) { return SyncState::save( mDir, mSynced, error ); }

  static bool allowed( sync::ActionType type, SyncDirection direction )
  {
    using sync::ActionType;
    switch ( type )
    {
      case ActionType::UploadNew:
      case ActionType::Upload:
      case ActionType::DeleteRemote:
        return direction != SyncDirection::Pull;
      case ActionType::Download:
      case ActionType::DeleteLocal:
        return direction != SyncDirection::Push;
      case ActionType::Conflict:
        return direction == SyncDirection::Both;
      case ActionType::RecordOnly:
      case ActionType::ForgetRecord:
        return true;
    }
    return true;
  }

  SyncResult SyncEngine::sync( const QString &conflictSuffix, SyncDirection direction )
  {
    SyncResult result;
    if ( !SyncState::load( mDir, mSynced, result.error ) || !scanProject( mDir, mSynced, mLocal, mMtimes, result.error ) ||
         !mRemote.list( mRemoteFiles, result.error ) )
      return result;

    const std::vector<sync::Action> plan = sync::planSync( mLocal, mRemoteFiles, mSynced, conflictSuffix.toStdString() );
    for ( const sync::Action &action : plan )
    {
      // A skipped action leaves its sync record as it was, so the next sync plans it again.
      if ( !allowed( action.type, direction ) )
      {
        ++result.skipped;
        continue;
      }
      if ( !apply( action, result, result.error ) || !saveState( result.error ) )
        return result;
    }
    result.ok = saveState( result.error );
    return result;
  }

  bool SyncEngine::downloadTo( const QString &remoteId, const std::string &path, QString &error )
  {
    const QString tmpDir = QDir( mDir ).filePath( QStringLiteral( ".ufield/tmp" ) );
    QDir().mkpath( tmpDir );
    const QString tmp = QDir( tmpDir ).filePath( QStringLiteral( "download" ) );
    QFile::remove( tmp );
    if ( !mRemote.download( remoteId, tmp, error ) )
      return false;

    std::string md5;
    if ( !md5Of( tmp, md5, error ) )
      return false;
    const auto remote = mRemoteFiles.find( path );
    if ( remote == mRemoteFiles.end() || remote->second.md5 != md5 )
    {
      // Changed on the remote since the listing, or a broken transfer: the next sync picks it up.
      QFile::remove( tmp );
      error = QStringLiteral( "Downloaded %1 does not match the remote checksum" ).arg( qs( path ) );
      return false;
    }

    const QString target = absolute( path );
    QDir().mkpath( QFileInfo( target ).path() );
    std::error_code ec;
    std::filesystem::rename( QFile::encodeName( tmp ).toStdString(), QFile::encodeName( target ).toStdString(), ec );
    if ( ec )
    {
      error = QStringLiteral( "Cannot write %1: %2" ).arg( qs( path ), QString::fromStdString( ec.message() ) );
      return false;
    }

    const QFileInfo info( target );
    mSynced[path] = { remote->second.id, md5, info.size(), mtimeMs( info ) };
    return true;
  }

  bool SyncEngine::recordUpload( const std::string &path, const QString &remoteId, QString &error )
  {
    const QFileInfo info( absolute( path ) );
    sync::RemoteFile uploaded;
    if ( !mRemote.upload( info.filePath(), qs( path ), remoteId, uploaded, error ) )
      return false;
    if ( uploaded.md5.empty() )
    {
      error = QStringLiteral( "The remote returned no checksum for %1" ).arg( qs( path ) );
      return false;
    }
    // If the file changed during the upload, its mtime changed too, so the next scan rehashes it.
    const auto mtime = mMtimes.find( path );
    mSynced[path] = { uploaded.id, uploaded.md5, info.size(), mtime != mMtimes.end() ? mtime->second : mtimeMs( info ) };
    return true;
  }

  bool SyncEngine::apply( const sync::Action &action, SyncResult &result, QString &error )
  {
    using sync::ActionType;
    const std::string &path = action.path;
    switch ( action.type )
    {
      case ActionType::UploadNew:
      case ActionType::Upload:
        if ( !recordUpload( path, qs( action.remoteId ), error ) )
          return false;
        ++result.uploaded;
        return true;

      case ActionType::Download:
        if ( !downloadTo( qs( action.remoteId ), path, error ) )
          return false;
        ++result.downloaded;
        return true;

      case ActionType::DeleteRemote:
        if ( !mRemote.trash( qs( action.remoteId ), error ) )
          return false;
        mSynced.erase( path );
        ++result.deleted;
        return true;

      case ActionType::DeleteLocal:
        if ( !QFile::remove( absolute( path ) ) && QFileInfo::exists( absolute( path ) ) )
        {
          error = QStringLiteral( "Cannot delete %1" ).arg( qs( path ) );
          return false;
        }
        mSynced.erase( path );
        ++result.deleted;
        return true;

      case ActionType::Conflict:
      {
        // Keep the local version under the conflict name, take the remote one under the original
        // name, then upload the conflict copy. If interrupted after the rename, the next sync
        // sees the original as deleted locally (download) and the copy as new (upload).
        if ( !QFile::rename( absolute( path ), absolute( action.conflictPath ) ) )
        {
          error = QStringLiteral( "Cannot rename %1 for a conflict copy" ).arg( qs( path ) );
          return false;
        }
        mMtimes[action.conflictPath] = mtimeMs( QFileInfo( absolute( action.conflictPath ) ) );
        result.conflictCopies << qs( action.conflictPath );
        if ( !downloadTo( qs( action.remoteId ), path, error ) || !saveState( error ) )
          return false;
        ++result.downloaded;
        if ( !recordUpload( action.conflictPath, QString(), error ) )
          return false;
        ++result.uploaded;
        return true;
      }

      case ActionType::RecordOnly:
      {
        const auto local = mLocal.find( path );
        const auto mtime = mMtimes.find( path );
        mSynced[path] = { action.remoteId, local->second.md5, local->second.size, mtime != mMtimes.end() ? mtime->second : 0 };
        return true;
      }

      case ActionType::ForgetRecord:
        mSynced.erase( path );
        return true;
    }
    return true;
  }

} // namespace ufield
