/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "storagetools.h"

#include <QCoreApplication>
#include <QJsonObject>

#include <memory>

#include "drivesync.h"
#include "tooldispatcher.h"

namespace ufield
{

  namespace
  {
    QString tr( const char *text ) { return QCoreApplication::translate( "ufield", text ); }

    QJsonObject busy()
    {
      return ToolDispatcher::error( QStringLiteral( "busy" ), QCoreApplication::translate( "ufield", "Another Google Drive sync is running; try again when it has finished" ) );
    }

    // Connects to the next emission of \a signal only, then disconnects.
    template<typename Signal, typename Slot>
    void once( DriveSync &drive, Signal signal, Slot slot )
    {
      auto connection = std::make_shared<QMetaObject::Connection>();
      *connection = QObject::connect( &drive, signal, &drive, [connection, slot]( auto &&...args ) mutable {
        QObject::disconnect( *connection );
        slot( args... );
      } );
    }

    QJsonObject syncResult( const QVariantMap &r )
    {
      if ( !r.value( QStringLiteral( "ok" ) ).toBool() )
        return ToolDispatcher::error( QStringLiteral( "sync_failed" ), r.value( QStringLiteral( "error" ) ).toString() );
      return {
        { QStringLiteral( "storage" ), QStringLiteral( "google_drive" ) },
        { QStringLiteral( "uploaded" ), r.value( QStringLiteral( "uploaded" ) ).toInt() },
        { QStringLiteral( "downloaded" ), r.value( QStringLiteral( "downloaded" ) ).toInt() },
        { QStringLiteral( "deleted" ), r.value( QStringLiteral( "deleted" ) ).toInt() },
        { QStringLiteral( "skipped" ), r.value( QStringLiteral( "skipped" ) ).toInt() },
        { QStringLiteral( "conflicts" ), QJsonArray::fromStringList( r.value( QStringLiteral( "conflicts" ) ).toStringList() ) },
        { QStringLiteral( "busy_files" ), QJsonArray::fromStringList( r.value( QStringLiteral( "busy_files" ) ).toStringList() ) },
      };
    }

    QJsonArray driveProjects( const QVariantList &projects )
    {
      QJsonArray out;
      for ( const QVariant &p : projects )
      {
        const QVariantMap m = p.toMap();
        out.append( QJsonObject { { QStringLiteral( "project_id" ), m.value( QStringLiteral( "folder_id" ) ).toString() },
                                  { QStringLiteral( "name" ), m.value( QStringLiteral( "name" ) ).toString() },
                                  { QStringLiteral( "storage" ), QStringLiteral( "google_drive" ) } } );
      }
      return out;
    }

    void startDownload( DriveSync &drive, const QString &folderId, const QString &name, ToolDispatcher::Reply reply )
    {
      if ( drive.busy() )
      {
        reply( busy() );
        return;
      }
      if ( !drive.downloadProject( folderId, name ) )
      {
        reply( ToolDispatcher::error( QStringLiteral( "already_exists" ), tr( "A project named %1 is already on this device" ).arg( name ) ) );
        return;
      }
      once( drive, &DriveSync::syncFinished, [reply, name]( const QVariantMap &r ) {
        QJsonObject result = syncResult( r );
        if ( !ToolDispatcher::isError( result ) )
          result.insert( QStringLiteral( "name" ), name );
        reply( result );
      } );
    }
  } // namespace

  void registerStorageTools( ToolDispatcher &dispatcher, DriveSync &drive, StorageToolsHost host )
  {
    DriveSync *d = &drive;

    dispatcher.registerTool( QStringLiteral( "sync_project" ), [d, host]( const QJsonObject &args, const ToolContext &, ToolDispatcher::Reply reply ) {
      const QString dir = host.openProjectDir ? host.openProjectDir() : QString();
      if ( dir.isEmpty() )
      {
        reply( ToolDispatcher::error( QStringLiteral( "no_open_project" ), tr( "Open a project first" ) ) );
        return;
      }
      if ( d->busy() )
      {
        reply( busy() );
        return;
      }
      if ( !d->syncProject( dir, args.value( QStringLiteral( "direction" ) ).toString( QStringLiteral( "both" ) ) ) )
      {
        reply( ToolDispatcher::error( QStringLiteral( "storage_error" ), tr( "The open project cannot be synced" ) ) );
        return;
      }
      once( *d, &DriveSync::syncFinished, [reply]( const QVariantMap &r ) { reply( syncResult( r ) ); } );
    } );

    dispatcher.registerTool( QStringLiteral( "list_projects" ), [d, host]( const QJsonObject &args, const ToolContext &, ToolDispatcher::Reply reply ) {
      const QString storage = args.value( QStringLiteral( "storage" ) ).toString( QStringLiteral( "all" ) );
      if ( storage != QLatin1String( "local" ) && storage != QLatin1String( "google_drive" ) && storage != QLatin1String( "all" ) )
      {
        reply( ToolDispatcher::error( QStringLiteral( "not_available" ), tr( "%1 storage is not available in this version" ).arg( storage ) ) );
        return;
      }
      QJsonArray projects;
      if ( storage != QLatin1String( "google_drive" ) && host.localProjects )
      {
        for ( const QJsonValue &p : host.localProjects() )
        {
          QJsonObject o = p.toObject();
          o.insert( QStringLiteral( "storage" ), QStringLiteral( "local" ) );
          projects.append( o );
        }
      }
      if ( storage == QLatin1String( "local" ) )
      {
        reply( { { QStringLiteral( "projects" ), projects } } );
        return;
      }
      if ( d->busy() || !d->listDriveProjects() )
      {
        // With "all", the local list is still useful; say why Drive is missing.
        if ( storage == QLatin1String( "all" ) )
          reply( { { QStringLiteral( "projects" ), projects }, { QStringLiteral( "warnings" ), QJsonArray { busy().value( QStringLiteral( "message" ) ) } } } );
        else
          reply( busy() );
        return;
      }
      once( *d, &DriveSync::projectsListed, [reply, projects, storage]( const QVariantList &listed, const QString &error ) mutable {
        if ( !error.isEmpty() && storage == QLatin1String( "google_drive" ) )
        {
          reply( ToolDispatcher::error( QStringLiteral( "storage_error" ), error ) );
          return;
        }
        for ( const QJsonValue &p : driveProjects( listed ) )
          projects.append( p );
        QJsonObject result { { QStringLiteral( "projects" ), projects } };
        if ( !error.isEmpty() )
          result.insert( QStringLiteral( "warnings" ), QJsonArray { error } );
        reply( result );
      } );
    } );

    dispatcher.registerTool( QStringLiteral( "download_project" ), [d]( const QJsonObject &args, const ToolContext &, ToolDispatcher::Reply reply ) {
      const QString folderId = args.value( QStringLiteral( "project_id" ) ).toString();
      const QString name = args.value( QStringLiteral( "name" ) ).toString();
      if ( !name.isEmpty() )
      {
        startDownload( *d, folderId, name, reply );
        return;
      }
      // No name given: use the project's name in Drive.
      if ( d->busy() || !d->listDriveProjects() )
      {
        reply( busy() );
        return;
      }
      once( *d, &DriveSync::projectsListed, [d, folderId, reply]( const QVariantList &listed, const QString &error ) {
        if ( !error.isEmpty() )
        {
          reply( ToolDispatcher::error( QStringLiteral( "storage_error" ), error ) );
          return;
        }
        for ( const QVariant &p : listed )
        {
          const QVariantMap m = p.toMap();
          if ( m.value( QStringLiteral( "folder_id" ) ).toString() == folderId )
          {
            startDownload( *d, folderId, m.value( QStringLiteral( "name" ) ).toString(), reply );
            return;
          }
        }
        reply( ToolDispatcher::error( QStringLiteral( "not_found" ), tr( "No project with this id in Google Drive" ) ) );
      } );
    } );
  }

} // namespace ufield
