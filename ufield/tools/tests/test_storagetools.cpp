/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// The storage tools through the ToolDispatcher, the way the UI and the voice
// agent call them, against the fake Drive API.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "drivesync.h"
#include "fakedrive.h"
#include "storagetools.h"
#include "tooldispatcher.h"

using namespace ufield;

class TestStorageTools : public QObject
{
    Q_OBJECT

  private:
    std::unique_ptr<FakeDrive> drive;
    std::unique_ptr<DriveSync> sync;
    std::unique_ptr<ToolDispatcher> tools;
    QString openProject;
    QTemporaryDir projects;

    static QByteArray schemaJson()
    {
      QFile f( QStringLiteral( ":/ufield/schema/tools.json" ) );
      return f.open( QIODevice::ReadOnly ) ? f.readAll() : QByteArray();
    }

    static void write( const QString &path, const QByteArray &data )
    {
      QDir().mkpath( QFileInfo( path ).path() );
      QFile f( path );
      QVERIFY( f.open( QIODevice::WriteOnly ) );
      f.write( data );
    }

    QJsonObject call( const QString &tool, const QJsonObject &args )
    {
      QJsonObject result;
      bool done = false;
      tools->call( tool, args, ToolContext { ToolClient::VoiceAgent, "test", "" }, [&]( const QJsonObject &r ) {
        result = r;
        done = true;
      } );
      if ( !QTest::qWaitFor( [&] { return done; }, 10000 ) )
        return ToolDispatcher::error( "timeout", "no reply" );
      return result;
    }

  private slots:
    void init()
    {
      drive = std::make_unique<FakeDrive>();
      const QString token = drive->validToken;
      sync = std::make_unique<DriveSync>( nullptr, [token] { return [token]( bool, QString & ) { return token; }; }, drive->base() );
      sync->setProjectsDir( projects.path() );
      tools = std::make_unique<ToolDispatcher>( schemaJson() );
      QVERIFY( tools->isValid() );
      openProject.clear();
      registerStorageTools( *tools, *sync, { [this] { return openProject; }, [] {
        return QJsonArray { QJsonObject { { "project_id", "kotitila" }, { "name", "Kotitila" } } }; } } );
    }

    void syncListDownload()
    {
      QCOMPARE( call( "sync_project", {} ).value( "error" ).toString(), QStringLiteral( "no_open_project" ) );

      openProject = projects.filePath( "Kotitila" );
      write( openProject + "/project.qgz", "qgz" );
      write( openProject + "/notes/u1/given/a.md", "note" );
      QJsonObject r = call( "sync_project", { { "direction", "both" } } );
      QVERIFY2( !ToolDispatcher::isError( r ), qPrintable( r.value( "message" ).toString() ) );
      QCOMPARE( r.value( "uploaded" ).toInt(), 2 );
      QCOMPARE( r.value( "storage" ).toString(), QStringLiteral( "google_drive" ) );

      r = call( "list_projects", { { "storage", "all" } } );
      const QJsonArray all = r.value( "projects" ).toArray();
      QCOMPARE( all.size(), 2 );
      QCOMPARE( all.at( 0 ).toObject().value( "storage" ).toString(), QStringLiteral( "local" ) );
      const QJsonObject inDrive = all.at( 1 ).toObject();
      QCOMPARE( inDrive.value( "storage" ).toString(), QStringLiteral( "google_drive" ) );
      QCOMPARE( inDrive.value( "name" ).toString(), QStringLiteral( "Kotitila" ) );

      QCOMPARE( call( "list_projects", { { "storage", "local" } } ).value( "projects" ).toArray().size(), 1 );
      QCOMPARE( call( "list_projects", { { "storage", "onedrive" } } ).value( "error" ).toString(), QStringLiteral( "not_available" ) );

      // the same name is taken on this device; under another name it downloads
      const QString id = inDrive.value( "project_id" ).toString();
      QCOMPARE( call( "download_project", { { "storage", "google_drive" }, { "project_id", id } } ).value( "error" ).toString(), QStringLiteral( "already_exists" ) );
      r = call( "download_project", { { "storage", "google_drive" }, { "project_id", id }, { "name", "Kotitila kopio" } } );
      QVERIFY2( !ToolDispatcher::isError( r ), qPrintable( r.value( "message" ).toString() ) );
      QCOMPARE( r.value( "downloaded" ).toInt(), 2 );
      QVERIFY( QFile::exists( projects.filePath( "Kotitila kopio/notes/u1/given/a.md" ) ) );

      QCOMPARE( call( "download_project", { { "storage", "google_drive" }, { "project_id", "unknown1" } } ).value( "error" ).toString(), QStringLiteral( "not_found" ) );
      QCOMPARE( call( "download_project", { { "storage", "google_drive" }, { "project_id", id }, { "name", "../x" } } ).value( "error" ).toString(), QStringLiteral( "invalid_input" ) );
    }

    void busyAndErrors()
    {
      openProject = projects.filePath( "Busy" );
      write( openProject + "/a.md", "a" );
      QVERIFY( sync->syncProject( openProject ) ); // a sync started from the UI
      QCOMPARE( call( "sync_project", {} ).value( "error" ).toString(), QStringLiteral( "busy" ) );
      QSignalSpy finished( sync.get(), &DriveSync::syncFinished );
      QVERIFY( finished.wait( 10000 ) );

      drive->validToken = "revoked";
      QJsonObject r = call( "sync_project", {} );
      QCOMPARE( r.value( "error" ).toString(), QStringLiteral( "sync_failed" ) );
      r = call( "list_projects", { { "storage", "google_drive" } } );
      QCOMPARE( r.value( "error" ).toString(), QStringLiteral( "storage_error" ) );
      // with "all", local projects still come back, with a warning
      r = call( "list_projects", {} );
      QCOMPARE( r.value( "projects" ).toArray().size(), 1 );
      QCOMPARE( r.value( "warnings" ).toArray().size(), 1 );
    }
};

QTEST_GUILESS_MAIN( TestStorageTools )
#include "test_storagetools.moc"
