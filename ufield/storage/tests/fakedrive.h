/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// A minimal fake of the Google Drive REST API v3 on localhost, for tests: the
// calls GoogleDriveProvider makes (listing with paging, media download, folder
// create, resumable upload, trash), with bearer-token checks.

#ifndef UFIELD_TESTS_FAKEDRIVE_H
#define UFIELD_TESTS_FAKEDRIVE_H

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <map>

#include "googledriveprovider.h"

using ufield::GoogleDriveProvider;

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
        const bool foldersOnly = q.contains( QStringLiteral( "mimeType = '%1'" ).arg( GoogleDriveProvider::kFolderMimeType ) );
        for ( const auto &f : files )
          if ( f.second.parent == parent && !f.second.trashed && ( name.isEmpty() || f.second.name == name ) &&
               ( !foldersOnly || f.second.mimeType == GoogleDriveProvider::kFolderMimeType ) )
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

#endif // UFIELD_TESTS_FAKEDRIVE_H
