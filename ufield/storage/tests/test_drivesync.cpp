/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// DriveSync, the app-facing controller, against the fake Drive API: linking a
// project to μField/<name>, syncing on a worker thread, listing and downloading
// Drive projects on a second "device".

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "drivesync.h"
#include "fakedrive.h"

using namespace ufield;

class TestDriveSync : public QObject
{
    Q_OBJECT

  private:
    std::unique_ptr<FakeDrive> drive;

    std::unique_ptr<DriveSync> controller()
    {
      const QString token = drive->validToken;
      auto c = std::make_unique<DriveSync>( nullptr, [token] { return [token]( bool, QString & ) { return token; }; }, drive->base() );
      c->setDeviceName( "Pixel" );
      return c;
    }

    static void write( const QString &path, const QByteArray &data )
    {
      QDir().mkpath( QFileInfo( path ).path() );
      QFile f( path );
      QVERIFY( f.open( QIODevice::WriteOnly ) );
      f.write( data );
    }

    static QVariantMap waitSync( DriveSync &c )
    {
      QSignalSpy spy( &c, &DriveSync::syncFinished );
      if ( !spy.wait( 10000 ) )
        return {};
      return spy.at( 0 ).at( 0 ).toMap();
    }

  private slots:
    void init() { drive = std::make_unique<FakeDrive>(); }

    void linkSyncListAndDownload()
    {
      QTemporaryDir phoneA, phoneB;
      const QString project = phoneA.filePath( "Pellot 2026" );
      write( project + "/project.qgz", "qgz" );
      write( project + "/media/p1.jpg", "jpeg" );

      auto a = controller();
      QVERIFY( a->syncProject( project ) );
      QVERIFY( a->busy() );
      QVERIFY( !a->syncProject( project ) ); // one call at a time
      const QVariantMap r = waitSync( *a );
      QVERIFY2( r.value( "ok" ).toBool(), qPrintable( r.value( "error" ).toString() ) );
      QCOMPARE( r.value( "uploaded" ).toInt(), 2 );
      QVERIFY( !a->busy() );

      const QString appFolder = drive->idOf( DriveSync::appFolderName(), "root" );
      QVERIFY( !appFolder.isEmpty() );
      const QString folder = drive->idOf( "Pellot 2026", appFolder );
      QCOMPARE( DriveSync::linkedFolderId( project ), folder );

      // the second phone sees the project and downloads it
      auto b = controller();
      QSignalSpy listed( b.get(), &DriveSync::projectsListed );
      QVERIFY( b->listDriveProjects() );
      QVERIFY( listed.wait( 10000 ) );
      const QVariantList projects = listed.at( 0 ).at( 0 ).toList();
      QCOMPARE( projects.size(), 1 );
      QCOMPARE( projects.at( 0 ).toMap().value( "name" ).toString(), QStringLiteral( "Pellot 2026" ) );
      QCOMPARE( projects.at( 0 ).toMap().value( "folder_id" ).toString(), folder );

      b->setProjectsDir( phoneB.path() );
      QSignalSpy downloaded( b.get(), &DriveSync::projectDownloaded );
      QVERIFY( b->downloadProject( folder, "Pellot 2026" ) );
      const QVariantMap rb = waitSync( *b );
      QCOMPARE( downloaded.size(), 1 );
      QCOMPARE( downloaded.at( 0 ).at( 0 ).toString(), phoneB.filePath( "Pellot 2026" ) );
      QCOMPARE( downloaded.at( 0 ).at( 1 ).toString(), QStringLiteral( "Pellot 2026" ) );
      QVERIFY2( rb.value( "ok" ).toBool(), qPrintable( rb.value( "error" ).toString() ) );
      QCOMPARE( rb.value( "downloaded" ).toInt(), 2 );
      QFile photo( phoneB.filePath( "Pellot 2026/media/p1.jpg" ) );
      QVERIFY( photo.open( QIODevice::ReadOnly ) );
      QCOMPARE( photo.readAll(), QByteArray( "jpeg" ) );

      // an existing folder is never overwritten by a download
      QVERIFY( !b->downloadProject( folder, "Pellot 2026", phoneB.path() ) );

      // a second linked sync reuses the link and conflicts carry the device name
      write( project + "/project.qgz", "edit on a" );
      write( phoneB.filePath( "Pellot 2026/project.qgz" ), "edit on b" );
      QVERIFY( b->syncProject( phoneB.filePath( "Pellot 2026" ) ) );
      QVERIFY( waitSync( *b ).value( "ok" ).toBool() );
      QVERIFY( a->syncProject( project ) );
      const QVariantMap conflict = waitSync( *a );
      QVERIFY( conflict.value( "ok" ).toBool() );
      const QStringList copies = conflict.value( "conflicts" ).toStringList();
      QCOMPARE( copies.size(), 1 );
      QVERIFY2( copies.at( 0 ).startsWith( "project (conflict " ) && copies.at( 0 ).endsWith( " Pixel).qgz" ), qPrintable( copies.at( 0 ) ) );
      // folders created once each: μField, the project, media/
      QCOMPARE( drive->requests.filter( QRegularExpression( "^POST /drive/v3/files$" ) ).size(), 3 );
    }

    void destroyedWhileSyncing()
    {
      QTemporaryDir dir;
      write( dir.filePath( "p/a.md" ), "a" );
      {
        auto c = controller();
        QVERIFY( c->syncProject( dir.filePath( "p" ) ) );
      } // destroyed with the worker still running: its result must be dropped, not delivered
      QTest::qWait( 500 );
      QVERIFY( !drive->idOf( "a.md", drive->idOf( "p", drive->idOf( DriveSync::appFolderName(), "root" ) ) ).isEmpty() );
    }

    void invalidArguments()
    {
      QTemporaryDir dir;
      auto c = controller();
      QVERIFY( !c->syncProject( dir.path(), "sideways" ) );
      QVERIFY( !c->syncProject( dir.filePath( "missing" ) ) );
      QVERIFY( !c->downloadProject( "f1", "../escape", dir.path() ) );
      QVERIFY( !c->downloadProject( "", "x", dir.path() ) );
      QVERIFY( !c->downloadProject( "f1", "x" ) ); // no projectsDir set
      QVERIFY( !c->busy() );
    }

    void signInErrorIsReported()
    {
      QTemporaryDir dir;
      DriveSync c( nullptr, [] { return []( bool, QString &e ) { e = "cancelled"; return QString(); }; }, drive->base() );
      QVERIFY( c.syncProject( dir.path() ) );
      const QVariantMap r = waitSync( c );
      QVERIFY( !r.value( "ok" ).toBool() );
      QCOMPARE( r.value( "error" ).toString(), QStringLiteral( "cancelled" ) );
      // a sync that never ran leaves nothing in the result counts
      QCOMPARE( r.value( "uploaded" ).toInt(), 0 );
      QVERIFY( DriveSync::linkedFolderId( dir.path() ).isEmpty() );
    }
};

QTEST_GUILESS_MAIN( TestDriveSync )
#include "test_drivesync.moc"
