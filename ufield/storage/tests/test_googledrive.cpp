/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// GoogleDriveProvider against a minimal fake of the Drive REST API v3 on
// localhost: the calls the provider makes, folders, paging, resumable upload,
// trash and token refresh, then a full SyncEngine round trip through it.

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "fakedrive.h"
#include "googledriveprovider.h"
#include "syncengine.h"

using namespace ufield;

class TestGoogleDrive : public QObject
{
    Q_OBJECT

  private:
    std::unique_ptr<FakeDrive> drive;
    QString currentToken;
    int tokenCalls = 0;

    GoogleDriveProvider provider()
    {
      return GoogleDriveProvider( "root-project", [this]( bool forceRefresh, QString & ) {
        ++tokenCalls;
        if ( forceRefresh )
          currentToken = drive->validToken;
        return currentToken; }, drive->base() );
    }

    static void write( const QTemporaryDir &dir, const QString &path, const QByteArray &data )
    {
      const QString abs = dir.filePath( path );
      QDir().mkpath( QFileInfo( abs ).path() );
      QFile f( abs );
      QVERIFY( f.open( QIODevice::WriteOnly ) );
      f.write( data );
    }

  private slots:
    void init()
    {
      drive = std::make_unique<FakeDrive>();
      currentToken = drive->validToken;
      tokenCalls = 0;
    }

    void uploadCreatesFoldersAndListsBack()
    {
      QTemporaryDir dir;
      write( dir, "media/2026/p1.jpg", "jpeg bytes" );
      GoogleDriveProvider p = provider();
      QString error;
      sync::RemoteFile up;
      QVERIFY2( p.upload( dir.filePath( "media/2026/p1.jpg" ), "media/2026/p1.jpg", QString(), up, error ), qPrintable( error ) );
      QCOMPARE( up.md5, QCryptographicHash::hash( "jpeg bytes", QCryptographicHash::Md5 ).toHex().toStdString() );

      const QString media = drive->idOf( "media", "root-project" );
      QVERIFY( !media.isEmpty() );
      QVERIFY( !drive->idOf( "p1.jpg", drive->idOf( "2026", media ) ).isEmpty() );

      // a second upload in the same folder reuses the folders
      write( dir, "media/2026/p2.jpg", "more" );
      QVERIFY( p.upload( dir.filePath( "media/2026/p2.jpg" ), "media/2026/p2.jpg", QString(), up, error ) );
      QCOMPARE( drive->requests.filter( "POST /drive/v3/files" ).size(), 2 );

      sync::RemoteFiles files;
      QVERIFY( p.list( files, error ) );
      QCOMPARE( files.size(), size_t( 2 ) );
      QVERIFY( files.count( "media/2026/p1.jpg" ) && files.count( "media/2026/p2.jpg" ) );
    }

    void listFollowsPagesAndSkipsTrash()
    {
      for ( int i = 0; i < 5; ++i )
        drive->files[QStringLiteral( "x%1" ).arg( i )] = { QStringLiteral( "n%1.md" ).arg( i ), "root-project", "text/markdown", "x", i == 4 };
      GoogleDriveProvider p = provider();
      sync::RemoteFiles files;
      QString error;
      QVERIFY( p.list( files, error ) );
      QCOMPARE( files.size(), size_t( 4 ) );
      QVERIFY( drive->requests.filter( "GET /drive/v3/files" ).size() >= 2 );
    }

    void updateDownloadTrash()
    {
      QTemporaryDir dir;
      write( dir, "a.md", "one" );
      GoogleDriveProvider p = provider();
      QString error;
      sync::RemoteFile up;
      QVERIFY( p.upload( dir.filePath( "a.md" ), "a.md", QString(), up, error ) );
      const QString id = QString::fromStdString( up.id );

      write( dir, "a.md", "two" );
      sync::RemoteFile updated;
      QVERIFY( p.upload( dir.filePath( "a.md" ), "a.md", id, updated, error ) );
      QCOMPARE( updated.id, up.id );
      QCOMPARE( drive->files[id].content, QByteArray( "two" ) );

      QVERIFY( p.download( id, dir.filePath( "copy.md" ), error ) );
      QFile copy( dir.filePath( "copy.md" ) );
      QVERIFY( copy.open( QIODevice::ReadOnly ) );
      QCOMPARE( copy.readAll(), QByteArray( "two" ) );

      QVERIFY( p.trash( id, error ) );
      QVERIFY( drive->files[id].trashed );
    }

    void expiredTokenIsRefreshedOnce()
    {
      currentToken = "stale";
      GoogleDriveProvider p = provider();
      sync::RemoteFiles files;
      QString error;
      QVERIFY2( p.list( files, error ), qPrintable( error ) );
      QCOMPARE( tokenCalls, 2 );
    }

    void revokedAccessFailsWithoutLeakingToken()
    {
      drive->validToken = "something-else";
      currentToken = "secret-token-value";
      GoogleDriveProvider p( "root-project", [this]( bool, QString & ) { return currentToken; }, drive->base() );
      sync::RemoteFiles files;
      QString error;
      QVERIFY( !p.list( files, error ) );
      QVERIFY( error.contains( "sign-in has expired" ) );
      QVERIFY( !error.contains( "secret-token-value" ) );
      QVERIFY( !error.contains( "127.0.0.1" ) ); // no URLs in errors
    }

    void notSignedIn()
    {
      GoogleDriveProvider p( "root-project", []( bool, QString & ) { return QString(); }, drive->base() );
      sync::RemoteFiles files;
      QString error;
      QVERIFY( !p.list( files, error ) );
      QCOMPARE( error, QStringLiteral( "Not signed in to Google Drive" ) );

      GoogleDriveProvider cancelled( "root-project", []( bool, QString &e ) { e = "cancelled"; return QString(); }, drive->base() );
      QVERIFY( !cancelled.list( files, error ) );
      QCOMPARE( error, QStringLiteral( "cancelled" ) );
    }

    void findOrCreateFolderWithQuotes()
    {
      GoogleDriveProvider p = provider();
      QString id, again, error;
      QVERIFY2( p.findOrCreateFolder( "root", "Pekan 'pellot'", id, error ), qPrintable( error ) );
      QVERIFY( p.findOrCreateFolder( "root", "Pekan 'pellot'", again, error ) );
      QCOMPARE( again, id );
      QVERIFY( p.findOrCreateFolder( "root", "μField", again, error ) );
      QVERIFY( again != id );
    }

    void syncEngineRoundTrip()
    {
      QTemporaryDir a, b;
      write( a, "project.qgz", "qgz" );
      write( a, "data/obs.gpkg", "gpkg" );
      write( a, "notes/u1/given/n.md", "note" );
      {
        GoogleDriveProvider p = provider();
        const SyncResult r = SyncEngine( a.path(), p ).sync( "conflict" );
        QVERIFY2( r.ok, qPrintable( r.error ) );
        QCOMPARE( r.uploaded, 3 );
      }
      {
        GoogleDriveProvider p = provider();
        const SyncResult r = SyncEngine( b.path(), p ).sync( "conflict" );
        QVERIFY2( r.ok, qPrintable( r.error ) );
        QCOMPARE( r.downloaded, 3 );
      }
      QFile note( b.filePath( "notes/u1/given/n.md" ) );
      QVERIFY( note.open( QIODevice::ReadOnly ) );
      QCOMPARE( note.readAll(), QByteArray( "note" ) );
    }
};

QTEST_GUILESS_MAIN( TestGoogleDrive )
#include "test_googledrive.moc"
