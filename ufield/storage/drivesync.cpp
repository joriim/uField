/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "drivesync.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>
#include <QThread>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include "driveauth.h"
#include "syncengine.h"

namespace ufield
{

  namespace
  {
    const char *kRoot = "root"; // My Drive

    QString linkPath( const QString &projectDir ) { return QDir( projectDir ).filePath( QStringLiteral( ".ufield/drive.json" ) ); }

    bool writeLink( const QString &projectDir, const QString &folderId, QString &error )
    {
      QDir().mkpath( QFileInfo( linkPath( projectDir ) ).path() );
      QSaveFile f( linkPath( projectDir ) );
      const QJsonObject link { { QStringLiteral( "version" ), 1 }, { QStringLiteral( "folder_id" ), folderId } };
      if ( !f.open( QIODevice::WriteOnly ) || f.write( QJsonDocument( link ).toJson() ) < 0 || !f.commit() )
      {
        error = QStringLiteral( "Cannot link the project to Google Drive: %1" ).arg( f.errorString() );
        return false;
      }
      return true;
    }

    bool parseDirection( const QString &text, SyncDirection &direction )
    {
      if ( text == QLatin1String( "both" ) )
        direction = SyncDirection::Both;
      else if ( text == QLatin1String( "push" ) )
        direction = SyncDirection::Push;
      else if ( text == QLatin1String( "pull" ) )
        direction = SyncDirection::Pull;
      else
        return false;
      return true;
    }

    QVariantMap toMap( const QString &projectDir, const SyncResult &r )
    {
      return {
        { QStringLiteral( "ok" ), r.ok },
        { QStringLiteral( "error" ), r.error },
        { QStringLiteral( "project_dir" ), projectDir },
        { QStringLiteral( "uploaded" ), r.uploaded },
        { QStringLiteral( "downloaded" ), r.downloaded },
        { QStringLiteral( "deleted" ), r.deleted },
        { QStringLiteral( "skipped" ), r.skipped },
        { QStringLiteral( "conflicts" ), r.conflictCopies },
      };
    }

    QVariantMap failure( const QString &projectDir, const QString &error )
    {
      SyncResult r;
      r.error = error;
      return toMap( projectDir, r );
    }
  } // namespace

  //! The phone model on Android ("Pixel 8"); the host name elsewhere.
  static QString defaultDeviceName()
  {
#ifdef Q_OS_ANDROID
    const QString model = QJniObject::getStaticObjectField( "android/os/Build", "MODEL", "Ljava/lang/String;" ).toString();
    if ( !model.isEmpty() )
      return model;
#endif
    return QSysInfo::machineHostName();
  }

  QString DriveSync::appFolderName() { return QString( QChar( 0x03BC ) ) + QStringLiteral( "Field" ); } // μField, CLAUDE.md rule 4

  DriveSync::DriveSync( QObject *parent, TokenProviderFactory tokens, const QUrl &apiBase )
    : QObject( parent )
    , mTokens( std::move( tokens ) )
    , mApiBase( apiBase )
    , mDeviceName( defaultDeviceName() )
  {
  }

  void DriveSync::setDeviceName( const QString &name )
  {
    if ( name == mDeviceName )
      return;
    mDeviceName = name;
    emit deviceNameChanged();
  }

  void DriveSync::setProjectsDir( const QString &dir )
  {
    if ( dir == mProjectsDir )
      return;
    mProjectsDir = dir;
    emit projectsDirChanged();
  }

  QString DriveSync::linkedFolderId( const QString &projectDir )
  {
    QFile f( linkPath( projectDir ) );
    if ( !f.open( QIODevice::ReadOnly ) )
      return QString();
    return QJsonDocument::fromJson( f.readAll() ).object().value( QStringLiteral( "folder_id" ) ).toString();
  }

  QString DriveSync::conflictSuffix() const
  {
    QString device = mDeviceName;
    device.remove( QLatin1Char( '/' ) ).remove( QLatin1Char( '\\' ) );
    QString suffix = QStringLiteral( "conflict " ) + QDate::currentDate().toString( Qt::ISODate );
    if ( !device.trimmed().isEmpty() )
      suffix += QLatin1Char( ' ' ) + device.trimmed();
    return suffix;
  }

  GoogleDriveProvider::TokenProvider DriveSync::tokens() const { return mTokens ? mTokens() : driveTokenProvider( true ); }

  bool DriveSync::start( std::function<std::function<void()>()> work )
  {
    if ( mBusy )
      return false;
    mBusy = true;
    emit busyChanged();
    // The worker never touches this object. Its result is delivered through QThread::finished,
    // a queued connection that Qt drops if this object is destroyed first.
    auto deliver = std::make_shared<std::function<void()>>();
    QThread *thread = QThread::create( [work = std::move( work ), deliver] { *deliver = work(); } );
    connect( thread, &QThread::finished, this, [this, deliver] {
      finish();
      if ( *deliver )
        ( *deliver )();
    } );
    connect( thread, &QThread::finished, thread, &QObject::deleteLater );
    thread->start();
    return true;
  }

  void DriveSync::finish()
  {
    mBusy = false;
    emit busyChanged();
  }

  bool DriveSync::syncProject( const QString &projectDir, const QString &direction )
  {
    SyncDirection dir;
    if ( !parseDirection( direction, dir ) )
      return false;
    return startSync( projectDir, dir, false );
  }

  bool DriveSync::startSync( const QString &projectDir, SyncDirection dir, bool download )
  {
    if ( mBusy || !QFileInfo( projectDir ).isDir() )
      return false;

    const QString suffix = conflictSuffix();
    const QString linked = linkedFolderId( projectDir );
    const QString name = QFileInfo( projectDir ).fileName();
    return start( [this, projectDir, dir, download, suffix, linked, name, token = tokens(), apiBase = mApiBase]() -> std::function<void()> {
      QVariantMap result;
      QString folderId = linked;
      QString error;
      if ( folderId.isEmpty() )
      {
        GoogleDriveProvider drive( QString::fromLatin1( kRoot ), token, apiBase );
        QString rootId;
        if ( !drive.findOrCreateFolder( QString::fromLatin1( kRoot ), appFolderName(), rootId, error ) ||
             !drive.findOrCreateFolder( rootId, name, folderId, error ) || !writeLink( projectDir, folderId, error ) )
          folderId.clear();
      }
      if ( folderId.isEmpty() )
        result = failure( projectDir, error );
      else
      {
        GoogleDriveProvider drive( folderId, token, apiBase );
        result = toMap( projectDir, SyncEngine( projectDir, drive ).sync( suffix, dir ) );
      }
      return std::function<void()>( [this, result, download, projectDir, name] {
        emit syncFinished( result );
        if ( download && result.value( QStringLiteral( "ok" ) ).toBool() )
          emit projectDownloaded( projectDir, name );
      } );
    } );
  }

  bool DriveSync::listDriveProjects()
  {
    return start( [this, token = tokens(), apiBase = mApiBase]() -> std::function<void()> {
      GoogleDriveProvider drive( QString::fromLatin1( kRoot ), token, apiBase );
      QString rootId, error;
      QList<QPair<QString, QString>> folders;
      QVariantList projects;
      if ( drive.findOrCreateFolder( QString::fromLatin1( kRoot ), appFolderName(), rootId, error ) && drive.listFolders( rootId, folders, error ) )
      {
        for ( const auto &folder : folders )
          projects << QVariantMap { { QStringLiteral( "name" ), folder.first }, { QStringLiteral( "folder_id" ), folder.second } };
      }
      return std::function<void()>( [this, projects, error] { emit projectsListed( projects, error ); } );
    } );
  }

  bool DriveSync::downloadProject( const QString &folderId, const QString &name, const QString &parentDir )
  {
    if ( mBusy || folderId.isEmpty() || name.isEmpty() || name.contains( QLatin1Char( '/' ) ) || name.contains( QLatin1Char( '\\' ) ) ||
         name == QLatin1String( "." ) || name == QLatin1String( ".." ) )
      return false;
    const QString parent = parentDir.isEmpty() ? mProjectsDir : parentDir;
    if ( parent.isEmpty() )
      return false;
    const QString projectDir = QDir( parent ).filePath( name );
    if ( QFileInfo::exists( projectDir ) || !QDir().mkpath( projectDir ) )
      return false;

    QString error;
    if ( !writeLink( projectDir, folderId, error ) )
      return false;
    return startSync( projectDir, SyncDirection::Pull, true );
  }

} // namespace ufield
