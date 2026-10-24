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

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <map>

#include "googledriveprovider.h"
#include "syncengine.h"

using namespace ufield;

class FakeDrive : public QObject
{
    Q_OBJECT

  public:
    struct File
    {
      QString name;
      QString parent;
      QString mimeType;
      QByteArray content;
      bool trashed = false;
    };
    std::map<QString, File> files;
    std::map<QString, QString> sessions; // upload session -> file id ("" until created)
    std::map<QString, QJsonObject> sessionMetadata;
    QString validToken = "token-1";
    int pageSize = 2;
    int nextId = 1;
    QStringList requests; //!< "VERB path" of each request

    FakeDrive()
    {
      mServer.listen( QHostAddress::LocalHost );
      connect( &mServer, &QTcpServer::newConnection, this, [this] {
        while ( QTcpSocket *socket = mServer.nextPendingConnection() )
          connect( socket, &QTcpSocket::readyRead, this, [this, socket] { onData( socket ); } );
      } );
      files["root-project"] = { "project", "root", GoogleDriveProvider::kFolderMimeType, {}, false };
    }

    QUrl base() const { return QUrl( QStringLiteral( "http://127.0.0.1:%1" ).arg( mServer.serverPort() ) ); }

    QString idOf( const QString &name, const QString &parent ) const
    {
      for ( const auto &f : files )
        if ( f.second.name == name && f.second.parent == parent && !f.second.trashed )
          return f.first;
      return QString();
    }

  private:
    QTcpServer mServer;
    std::map<QTcpSocket *, QByteArray> mBuffers;

    QString newId() { return QStringLiteral( "f%1" ).arg( nextId++ ); }

    QJsonObject fileJson( const QString &id ) const
    {
      const File &f = files.at( id );
      QJsonObject o { { "id", id }, { "name", f.name }, { "mimeType", f.mimeType } };
      if ( f.mimeType != GoogleDriveProvider::kFolderMimeType )
      {
        o["md5Checksum"] = QString( QCryptographicHash::hash( f.content, QCryptographicHash::Md5 ).toHex() );
        o["size"] = QString::number( f.content.size() );
      }
      return o;
    }

    void onData( QTcpSocket *socket )
    {
      QByteArray &buffer = mBuffers[socket];
      buffer += socket->readAll();
      const int headerEnd = buffer.indexOf( "\r\n\r\n" );
      if ( headerEnd < 0 )
        return;
      const QList<QByteArray> lines = buffer.left( headerEnd ).split( '\n' );
      QHash<QByteArray, QByteArray> headers;
      for ( int i = 1; i < lines.size(); ++i )
      {
        const int colon = lines[i].indexOf( ':' );
        if ( colon > 0 )
          headers.insert( lines[i].left( colon ).trimmed().toLower(), lines[i].mid( colon + 1 ).trimmed() );
      }
      const int length = headers.value( "content-length", "0" ).toInt();
      if ( buffer.size() < headerEnd + 4 + length )
        return;
      const QByteArray body = buffer.mid( headerEnd + 4, length );
      const QList<QByteArray> requestLine = lines[0].trimmed().split( ' ' );
      buffer.clear();

      int status = 200;
      QByteArray responseBody;
      QByteArray extraHeaders;
      if ( headers.value( "authorization" ) != "Bearer " + validToken.toUtf8() )
      {
        status = 401;
        responseBody = R"({"error":{"code":401,"message":"Invalid Credentials"}})";
      }
      else
        handle( requestLine[0], QUrl( QString::fromUtf8( requestLine[1] ) ), body, status, responseBody, extraHeaders );

      QByteArray response = "HTTP/1.1 " + QByteArray::number( status ) + " X\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " +
                            QByteArray::number( responseBody.size() ) + "\r\n" + extraHeaders + "\r\n" + responseBody;
      socket->write( response );
      socket->disconnectFromHost();
      mBuffers.erase( socket );
    }

    void handle( const QByteArray &verb, const QUrl &url, const QByteArray &body, int &status, QByteArray &out, QByteArray &extraHeaders )
    {
      const QString path = url.path();
      const QUrlQuery query( url );
      requests << QString::fromUtf8( verb ) + " " + path;
      static const QRegularExpression fileRe( "^/drive/v3/files/([^/]+)$" );
      static const QRegularExpression uploadRe( "^/upload/drive/v3/files(?:/([^/]+))?$" );

      if ( verb == "GET" && path == "/drive/v3/files" )
      {
        const QString q = query.queryItemValue( "q", QUrl::FullyDecoded );
        static const QRegularExpression parentRe( "'([^']+)' in parents" );
        static const QRegularExpression nameRe( "name = '((?:[^'\\\\]|\\\\.)*)'" );
        const QString parent = parentRe.match( q ).captured( 1 );
        QString name = nameRe.match( q ).captured( 1 );
        name.replace( "\\'", "'" ).replace( "\\\\", "\\" );
        QStringList ids;
        for ( const auto &f : files )
          if ( f.second.parent == parent && !f.second.trashed && ( name.isEmpty() || f.second.name == name ) )
            ids << f.first;
        const int start = query.queryItemValue( "pageToken" ).toInt();
        QJsonArray page;
        for ( int i = start; i < ids.size() && i < start + pageSize; ++i )
          page.append( fileJson( ids[i] ) );
        QJsonObject result { { "files", page } };
        if ( start + pageSize < ids.size() )
          result["nextPageToken"] = QString::number( start + pageSize );
        out = QJsonDocument( result ).toJson();
      }
      else if ( verb == "GET" && fileRe.match( path ).hasMatch() && query.queryItemValue( "alt" ) == "media" )
      {
        const QString id = fileRe.match( path ).captured( 1 );
        if ( !files.count( id ) )
          status = 404;
        else
          out = files[id].content;
      }
      else if ( verb == "POST" && path == "/drive/v3/files" )
      {
        const QJsonObject m = QJsonDocument::fromJson( body ).object();
        const QString id = newId();
        files[id] = { m["name"].toString(), m["parents"].toArray().at( 0 ).toString(), m["mimeType"].toString(), {}, false };
        out = QJsonDocument( QJsonObject { { "id", id } } ).toJson();
      }
      else if ( verb == "PATCH" && fileRe.match( path ).hasMatch() )
      {
        const QString id = fileRe.match( path ).captured( 1 );
        files[id].trashed = QJsonDocument::fromJson( body ).object()["trashed"].toBool();
        out = "{}";
      }
      else if ( ( verb == "POST" || verb == "PATCH" ) && uploadRe.match( path ).hasMatch() && query.queryItemValue( "uploadType" ) == "resumable" )
      {
        const QString session = QStringLiteral( "s%1" ).arg( sessions.size() + 1 );
        sessions[session] = uploadRe.match( path ).captured( 1 );
        sessionMetadata[session] = QJsonDocument::fromJson( body ).object();
        extraHeaders = "Location: " + base().toString().toUtf8() + "/upload/drive/v3/files?upload_id=" + session.toUtf8() + "\r\n";
      }
      else if ( verb == "PUT" && path == "/upload/drive/v3/files" )
      {
        const QString session = query.queryItemValue( "upload_id" );
        QString id = sessions.at( session );
        if ( id.isEmpty() )
        {
          const QJsonObject m = sessionMetadata[session];
          id = newId();
          files[id] = { m["name"].toString(), m["parents"].toArray().at( 0 ).toString(), "application/octet-stream", {}, false };
        }
        files[id].content = body;
        out = QJsonDocument( fileJson( id ) ).toJson();
      }
      else
      {
        status = 400;
        out = R"({"error":{"message":"unexpected request"}})";
      }
    }
};

class TestGoogleDrive : public QObject
{
    Q_OBJECT

  private:
    std::unique_ptr<FakeDrive> drive;
    QString currentToken;
    int tokenCalls = 0;

    GoogleDriveProvider provider()
    {
      return GoogleDriveProvider( "root-project", [this]( bool forceRefresh ) {
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
      GoogleDriveProvider p( "root-project", [this]( bool ) { return currentToken; }, drive->base() );
      sync::RemoteFiles files;
      QString error;
      QVERIFY( !p.list( files, error ) );
      QVERIFY( error.contains( "sign-in has expired" ) );
      QVERIFY( !error.contains( "secret-token-value" ) );
      QVERIFY( !error.contains( "127.0.0.1" ) ); // no URLs in errors
    }

    void notSignedIn()
    {
      GoogleDriveProvider p( "root-project", []( bool ) { return QString(); }, drive->base() );
      sync::RemoteFiles files;
      QString error;
      QVERIFY( !p.list( files, error ) );
      QCOMPARE( error, QStringLiteral( "Not signed in to Google Drive" ) );
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
