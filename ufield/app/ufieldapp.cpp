/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "ufieldapp.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlContext>

#include "activeproject.h"
#include "drivesync.h"
#include "localprojectsmanager.h"
#include "storagetools.h"
#include "tooldispatcher.h"

namespace ufield
{

  namespace
  {
    QByteArray toolsJson()
    {
      QFile f( QStringLiteral( ":/ufield/schema/tools.json" ) );
      return f.open( QIODevice::ReadOnly ) ? f.readAll() : QByteArray();
    }
  } // namespace

  UfieldApp::UfieldApp( LocalProjectsManager &localProjects, ActiveProject &activeProject )
    : mDrive( std::make_unique<DriveSync>() )
    , mTools( std::make_unique<ToolDispatcher>( toolsJson() ) )
  {
    // Downloaded Drive projects join the local project list.
    mDrive->setProjectsDir( localProjects.dataDir() );
    QObject::connect( mDrive.get(), &DriveSync::projectDownloaded, &localProjects, &LocalProjectsManager::addLocalProject );

    StorageToolsHost host;
    host.openProjectDir = [&activeProject] { return activeProject.localProject().projectDir; };
    host.localProjects = [&localProjects] {
      QJsonArray out;
      for ( const LocalProject &p : localProjects.projects() )
        out.append( QJsonObject { { QStringLiteral( "project_id" ), p.id() }, { QStringLiteral( "name" ), p.projectName } } );
      return out;
    };
    registerStorageTools( *mTools, *mDrive, host );

    // Files of the open project changed on disk: reload it so QGIS shows them, as upstream does after a Mergin sync.
    QObject::connect( mDrive.get(), &DriveSync::syncFinished, &activeProject, [&activeProject]( const QVariantMap &r ) {
      const bool changed = r.value( QStringLiteral( "downloaded" ) ).toInt() > 0 || r.value( QStringLiteral( "deleted" ) ).toInt() > 0;
      if ( r.value( QStringLiteral( "ok" ) ).toBool() && changed )
        activeProject.reloadProject( r.value( QStringLiteral( "project_dir" ) ).toString() );
    } );
  }

  UfieldApp::~UfieldApp() = default;

  void UfieldApp::registerContextProperties( QQmlContext *context )
  {
    context->setContextProperty( QStringLiteral( "__ufieldTools" ), mTools.get() );
    context->setContextProperty( QStringLiteral( "__ufieldDriveSync" ), mDrive.get() );
  }

} // namespace ufield
