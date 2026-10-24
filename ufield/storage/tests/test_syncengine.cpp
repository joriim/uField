/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// SyncEngine against an in-memory remote: two "devices" (temporary project
// folders) sync through the same remote, as two phones share one Drive folder.

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <map>

#include "syncengine.h"

using namespace ufield;

class FakeRemote : public RemoteStore
{
  public:
    struct File
    {
      QString path;
      QByteArray content;
      bool trashed = false;
    };
    std::map<QString, File> files; // by id
    int nextId = 1;
    int uploads = 0;
    int failAfterUploads = -1; //!< fail the upload after this many have succeeded

    static std::string md5( const QByteArray &data ) { return QCryptographicHash::hash( data, QCryptographicHash::Md5 ).toHex().toStdString(); }

    QString idOf( const QString &path ) const
    {
      for ( const auto &f : files )
        if ( f.second.path == path && !f.second.trashed )
          return f.first;
      return QString();
    }
    QByteArray content( const QString &path ) const { return files.at( idOf( path ) ).content; }
    void put( const QString &path, const QByteArray &data )
    {
      const QString id = idOf( path );
      if ( id.isEmpty() )
        files[QStringLiteral( "id%1" ).arg( nextId++ )] = { path, data, false };
      else
        files[id].content = data;
    }

    bool list( sync::RemoteFiles &out, QString & ) override
    {
      out.clear();
      for ( const auto &f : files )
        if ( !f.second.trashed )
          out[f.second.path.toStdString()] = { f.first.toStdString(), md5( f.second.content ), f.second.content.size() };
      return true;
    }

    bool download( const QString &remoteId, const QString &localFile, QString &error ) override
    {
      QFile f( localFile );
      if ( !files.count( remoteId ) || !f.open( QIODevice::WriteOnly ) )
      {
        error = QStringLiteral( "download failed" );
        return false;
      }
      f.write( files[remoteId].content );
      return true;
    }

    bool upload( const QString &localFile, const QString &path, const QString &remoteId, sync::RemoteFile &uploaded, QString &error ) override
    {
      if ( failAfterUploads >= 0 && uploads >= failAfterUploads )
      {
        error = QStringLiteral( "connection lost" );
        return false;
      }
      QFile f( localFile );
      if ( !f.open( QIODevice::ReadOnly ) )
        return false;
      QString id = remoteId;
      if ( id.isEmpty() )
        id = QStringLiteral( "id%1" ).arg( nextId++ );
      files[id] = { path, f.readAll(), false };
      uploaded = { id.toStdString(), md5( files[id].content ), files[id].content.size() };
      ++uploads;
      return true;
    }

    bool trash( const QString &remoteId, QString & ) override
    {
      files[remoteId].trashed = true;
      return true;
    }
};

class TestSyncEngine : public QObject
{
    Q_OBJECT

  private:
    static void write( const QTemporaryDir &dir, const QString &path, const QByteArray &data )
    {
      const QString abs = dir.filePath( path );
      QDir().mkpath( QFileInfo( abs ).path() );
      QFile f( abs );
      QVERIFY( f.open( QIODevice::WriteOnly ) );
      f.write( data );
    }
    static QByteArray read( const QTemporaryDir &dir, const QString &path )
    {
      QFile f( dir.filePath( path ) );
      return f.open( QIODevice::ReadOnly ) ? f.readAll() : QByteArray();
    }
    static SyncResult run( const QTemporaryDir &dir, FakeRemote &remote, SyncDirection direction = SyncDirection::Both )
    {
      SyncEngine engine( dir.path(), remote );
      return engine.sync( QStringLiteral( "conflict 2026-10-21 Pixel" ), direction );
    }

  private slots:
    void firstSyncUploadsAndSkipsSideFiles()
    {
      QTemporaryDir a;
      FakeRemote remote;
      write( a, "project.qgz", "qgz" );
      write( a, "data/obs.gpkg", "gpkg" );
      write( a, "data/obs.gpkg-wal", "wal" );
      write( a, "media/p1.jpg", "jpeg" );

      const SyncResult r = run( a, remote );
      QVERIFY2( r.ok, qPrintable( r.error ) );
      QCOMPARE( r.uploaded, 3 );
      QCOMPARE( remote.content( "data/obs.gpkg" ), QByteArray( "gpkg" ) );
      QVERIFY( remote.idOf( "data/obs.gpkg-wal" ).isEmpty() );
      QVERIFY( remote.idOf( ".ufield/sync.json" ).isEmpty() );
      QVERIFY( QFile::exists( SyncState::filePath( a.path() ) ) );

      // nothing changed: nothing to do
      const SyncResult again = run( a, remote );
      QVERIFY( again.ok );
      QCOMPARE( again.uploaded + again.downloaded + again.deleted, 0 );
    }

    void secondDeviceDownloadsAndEditsFlowBack()
    {
      QTemporaryDir a, b;
      FakeRemote remote;
      write( a, "notes/u1/given/n.md", "first" );
      QVERIFY( run( a, remote ).ok );

      const SyncResult rb = run( b, remote );
      QVERIFY( rb.ok );
      QCOMPARE( rb.downloaded, 1 );
      QCOMPARE( read( b, "notes/u1/given/n.md" ), QByteArray( "first" ) );
      QVERIFY( !QDir( b.filePath( ".ufield/tmp" ) ).entryList( QDir::Files ).contains( "download" ) );

      write( b, "notes/u1/given/n.md", "second" );
      QCOMPARE( run( b, remote ).uploaded, 1 );
      QCOMPARE( run( a, remote ).downloaded, 1 );
      QCOMPARE( read( a, "notes/u1/given/n.md" ), QByteArray( "second" ) );
    }

    void conflictKeepsBothVersions()
    {
      QTemporaryDir a, b;
      FakeRemote remote;
      write( a, "data/obs.gpkg", "base" );
      QVERIFY( run( a, remote ).ok );
      QVERIFY( run( b, remote ).ok );

      write( a, "data/obs.gpkg", "edit on a" );
      QVERIFY( run( a, remote ).ok );
      write( b, "data/obs.gpkg", "edit on b" );

      const SyncResult r = run( b, remote );
      QVERIFY2( r.ok, qPrintable( r.error ) );
      const QString copy = "data/obs (conflict 2026-10-21 Pixel).gpkg";
      QCOMPARE( r.conflictCopies, QStringList { copy } );
      QCOMPARE( read( b, "data/obs.gpkg" ), QByteArray( "edit on a" ) );
      QCOMPARE( read( b, copy ), QByteArray( "edit on b" ) );
      QCOMPARE( remote.content( copy ), QByteArray( "edit on b" ) );

      // a receives the conflict copy too
      QVERIFY( run( a, remote ).ok );
      QCOMPARE( read( a, copy ), QByteArray( "edit on b" ) );
    }

    void deletesPropagateToTrash()
    {
      QTemporaryDir a, b;
      FakeRemote remote;
      write( a, "media/p1.jpg", "jpeg" );
      QVERIFY( run( a, remote ).ok );
      QVERIFY( run( b, remote ).ok );
      const QString id = remote.idOf( "media/p1.jpg" );

      QVERIFY( QFile::remove( a.filePath( "media/p1.jpg" ) ) );
      QCOMPARE( run( a, remote ).deleted, 1 );
      QVERIFY( remote.files[id].trashed );

      QCOMPARE( run( b, remote ).deleted, 1 );
      QVERIFY( !QFile::exists( b.filePath( "media/p1.jpg" ) ) );
    }

    void remoteChangeWinsOverLocalDelete()
    {
      QTemporaryDir a;
      FakeRemote remote;
      write( a, "x.md", "v1" );
      QVERIFY( run( a, remote ).ok );
      QVERIFY( QFile::remove( a.filePath( "x.md" ) ) );
      remote.put( "x.md", "v2 from elsewhere" );

      const SyncResult r = run( a, remote );
      QVERIFY( r.ok );
      QCOMPARE( r.downloaded, 1 );
      QCOMPARE( read( a, "x.md" ), QByteArray( "v2 from elsewhere" ) );
    }

    void interruptedSyncResumes()
    {
      QTemporaryDir a;
      FakeRemote remote;
      write( a, "a.jpg", "1" );
      write( a, "b.jpg", "2" );
      write( a, "c.jpg", "3" );

      remote.failAfterUploads = 2;
      const SyncResult r = run( a, remote );
      QVERIFY( !r.ok );
      QCOMPARE( r.error, QStringLiteral( "connection lost" ) );

      remote.failAfterUploads = -1;
      const SyncResult resumed = run( a, remote );
      QVERIFY( resumed.ok );
      QCOMPARE( resumed.uploaded, 1 ); // only the file that did not make it
      QCOMPARE( remote.uploads, 3 );
    }

    void unchangedFilesAreNotRehashed()
    {
      QTemporaryDir a;
      FakeRemote remote;
      write( a, "v.mp4", "video" );
      QVERIFY( run( a, remote ).ok );

      sync::SyncedFiles synced;
      QString error;
      QVERIFY( SyncState::load( a.path(), synced, error ) );
      // A wrong recorded hash with matching size and mtime is reused, proving the file was not read.
      synced["v.mp4"].md5 = "cached";
      QVERIFY( SyncState::save( a.path(), synced, error ) );

      sync::LocalFiles files;
      std::map<std::string, std::int64_t> mtimes;
      QVERIFY( scanProject( a.path(), synced, files, mtimes, error ) );
      QCOMPARE( files["v.mp4"].md5, std::string( "cached" ) );

      synced["v.mp4"].localMtimeMs -= 1000;
      QVERIFY( scanProject( a.path(), synced, files, mtimes, error ) );
      QCOMPARE( files["v.mp4"].md5, FakeRemote::md5( "video" ) );
    }

    void pushAndPullAreOneWay()
    {
      QTemporaryDir a, b;
      FakeRemote remote;
      write( a, "shared.md", "v1" );
      QVERIFY( run( a, remote ).ok );
      QVERIFY( run( b, remote ).ok );

      write( a, "local.md", "from a" );
      remote.put( "shared.md", "v2 from b" );

      // pull: takes the remote change, leaves the local one
      SyncResult r = run( a, remote, SyncDirection::Pull );
      QVERIFY( r.ok );
      QCOMPARE( r.downloaded, 1 );
      QCOMPARE( r.uploaded, 0 );
      QCOMPARE( r.skipped, 1 );
      QCOMPARE( read( a, "shared.md" ), QByteArray( "v2 from b" ) );
      QVERIFY( remote.idOf( "local.md" ).isEmpty() );

      // push: now sends it
      r = run( a, remote, SyncDirection::Push );
      QVERIFY( r.ok );
      QCOMPARE( r.uploaded, 1 );
      QCOMPARE( r.skipped, 0 );
      QCOMPARE( remote.content( "local.md" ), QByteArray( "from a" ) );

      // a conflict is left for a two-way sync, with both versions untouched
      write( a, "shared.md", "edit on a" );
      remote.put( "shared.md", "edit on b" );
      r = run( a, remote, SyncDirection::Push );
      QVERIFY( r.ok );
      QCOMPARE( r.skipped, 1 );
      QVERIFY( r.conflictCopies.isEmpty() );
      QCOMPARE( read( a, "shared.md" ), QByteArray( "edit on a" ) );
      QCOMPARE( remote.content( "shared.md" ), QByteArray( "edit on b" ) );
      QCOMPARE( run( a, remote ).conflictCopies.size(), 1 );
    }

    void invalidStateIsAnError()
    {
      QTemporaryDir a;
      write( a, ".ufield/sync.json", "{not json" );
      FakeRemote remote;
      const SyncResult r = run( a, remote );
      QVERIFY( !r.ok );
      QVERIFY( r.error.contains( "Invalid sync state" ) );
    }
};

QTEST_GUILESS_MAIN( TestSyncEngine )
#include "test_syncengine.moc"
