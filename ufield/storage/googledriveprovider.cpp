/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "googledriveprovider.h"

#include <QBuffer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>

namespace ufield
{

  namespace
  {
    const QString kFileFields = QStringLiteral( "id,name,mimeType,md5Checksum,size" );

    // Drive query string literal: backslash and single quote are escaped.
    QString quoted( QString value )
    {
      value.replace( QLatin1Char( '\\' ), QStringLiteral( "\\\\" ) ).replace( QLatin1Char( '\'' ), QStringLiteral( "\\'" ) );
      return QLatin1Char( '\'' ) + value + QLatin1Char( '\'' );
    }

    QJsonObject jsonOf( const QByteArray &body ) { return QJsonDocument::fromJson( body ).object(); }
  } // namespace

  GoogleDriveProvider::GoogleDriveProvider( const QString &projectFolderId, TokenProvider token, const QUrl &apiBase )
    : mRootId( projectFolderId )
    , mToken( std::move( token ) )
    , mApiBase( apiBase )
    , mNam( std::make_unique<QNetworkAccessManager>() )
  {
    mFolders.insert( QString(), mRootId );
  }

  GoogleDriveProvider::~GoogleDriveProvider() = default;

  QUrl GoogleDriveProvider::api( const QString &path, const QList<QPair<QString, QString>> &query ) const
  {
    // Encoded by hand: QUrlQuery leaves '+' as is, which servers read as a space.
    QStringList items;
    for ( const auto &item : query )
      items << item.first + QLatin1Char( '=' ) + QString::fromLatin1( QUrl::toPercentEncoding( item.second ) );
    QUrl url = mApiBase;
    url.setPath( path );
    url.setQuery( items.join( QLatin1Char( '&' ) ), QUrl::StrictMode );
    return url;
  }

  bool GoogleDriveProvider::send( const QByteArray &verb, const QUrl &url, const QByteArray &contentType, QIODevice *body,
                                  Response &response, QString &error, QIODevice *sink )
  {
    for ( int attempt = 0; attempt < 2; ++attempt )
    {
      QString tokenError;
      const QString token = mToken( attempt > 0, tokenError );
      if ( token.isEmpty() )
      {
        error = tokenError.isEmpty() ? QStringLiteral( "Not signed in to Google Drive" ) : tokenError;
        return false;
      }
      QNetworkRequest request( url );
      request.setRawHeader( "Authorization", "Bearer " + token.toUtf8() );
      if ( !contentType.isEmpty() )
        request.setHeader( QNetworkRequest::ContentTypeHeader, contentType );
      if ( body )
      {
        body->seek( 0 );
        request.setHeader( QNetworkRequest::ContentLengthHeader, body->size() );
      }
      // Redirects would resend the bearer token to another host.
      request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy );

      QNetworkReply *reply = mNam->sendCustomRequest( request, verb, body );
      QEventLoop loop;
      if ( sink )
      {
        QObject::connect( reply, &QNetworkReply::readyRead, &loop, [reply, sink] {
          // Only a successful body goes to the sink; an error body is kept for the message.
          if ( reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt() / 100 == 2 )
            sink->write( reply->readAll() );
        } );
      }
      QObject::connect( reply, &QNetworkReply::finished, &loop, &QEventLoop::quit );
      loop.exec();

      response.status = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
      if ( sink && response.status / 100 == 2 )
        sink->write( reply->readAll() );
      else
        response.body = reply->readAll();
      response.location = reply->rawHeader( "Location" );
      const QNetworkReply::NetworkError networkError = reply->error();
      reply->deleteLater();

      if ( response.status == 0 )
      {
        // No HTTP response at all. QNetworkReply::errorString() can contain the URL; keep it out.
        error = QStringLiteral( "Google Drive is unreachable (network error %1)" ).arg( static_cast<int>( networkError ) );
        return false;
      }
      if ( response.status != 401 ) // a 401 wrote nothing to the sink, so a retry is safe
        return true;
    }
    return true;
  }

  bool GoogleDriveProvider::sendJson( const QByteArray &verb, const QUrl &url, const QJsonObject &json, Response &response, QString &error )
  {
    QByteArray data = QJsonDocument( json ).toJson( QJsonDocument::Compact );
    QBuffer buffer( &data );
    buffer.open( QIODevice::ReadOnly );
    return send( verb, url, "application/json; charset=UTF-8", &buffer, response, error );
  }

  bool GoogleDriveProvider::ok( const Response &response, const QString &what, QString &error ) const
  {
    if ( response.status / 100 == 2 )
      return true;
    const QString message = jsonOf( response.body ).value( QStringLiteral( "error" ) ).toObject().value( QStringLiteral( "message" ) ).toString();
    if ( response.status == 401 )
      error = QStringLiteral( "Google Drive sign-in has expired (%1)" ).arg( what );
    else
      error = QStringLiteral( "Google Drive: %1 failed with HTTP %2%3" ).arg( what ).arg( response.status ).arg( message.isEmpty() ? QString() : QStringLiteral( ": " ) + message );
    return false;
  }

  bool GoogleDriveProvider::listFolder( const QString &folderId, const QString &prefix, sync::RemoteFiles &files, QString &error )
  {
    QString pageToken;
    do
    {
      QList<QPair<QString, QString>> query {
        { QStringLiteral( "q" ), QStringLiteral( "%1 in parents and trashed = false" ).arg( quoted( folderId ) ) },
        { QStringLiteral( "fields" ), QStringLiteral( "nextPageToken,files(%1)" ).arg( kFileFields ) },
        { QStringLiteral( "pageSize" ), QStringLiteral( "1000" ) },
        { QStringLiteral( "orderBy" ), QStringLiteral( "createdTime" ) },
      };
      if ( !pageToken.isEmpty() )
        query.append( { QStringLiteral( "pageToken" ), pageToken } );

      Response response;
      if ( !send( "GET", api( QStringLiteral( "/drive/v3/files" ), query ), {}, nullptr, response, error ) || !ok( response, QStringLiteral( "listing" ), error ) )
        return false;

      const QJsonObject json = jsonOf( response.body );
      std::vector<std::pair<QString, QString>> subfolders;
      for ( const QJsonValue &value : json.value( QStringLiteral( "files" ) ).toArray() )
      {
        const QJsonObject f = value.toObject();
        const QString name = f.value( QStringLiteral( "name" ) ).toString();
        if ( name.isEmpty() || name.contains( QLatin1Char( '/' ) ) || name == QLatin1String( "." ) || name == QLatin1String( ".." ) )
          continue; // not representable as a local path
        const QString path = prefix + name;
        if ( f.value( QStringLiteral( "mimeType" ) ).toString() == QLatin1String( kFolderMimeType ) )
        {
          if ( !mFolders.contains( path ) )
          {
            mFolders.insert( path, f.value( QStringLiteral( "id" ) ).toString() );
            subfolders.emplace_back( f.value( QStringLiteral( "id" ) ).toString(), path );
          }
          continue;
        }
        // Drive allows several files with one name in a folder; the oldest one (orderBy createdTime) is synced.
        const std::string key = path.toStdString();
        if ( files.count( key ) )
          continue;
        sync::RemoteFile file;
        file.id = f.value( QStringLiteral( "id" ) ).toString().toStdString();
        file.md5 = f.value( QStringLiteral( "md5Checksum" ) ).toString().toStdString();
        file.size = f.value( QStringLiteral( "size" ) ).toString().toLongLong();
        files[key] = file;
      }
      for ( const auto &sub : subfolders )
      {
        if ( !listFolder( sub.first, sub.second + QLatin1Char( '/' ), files, error ) )
          return false;
      }
      pageToken = json.value( QStringLiteral( "nextPageToken" ) ).toString();
    } while ( !pageToken.isEmpty() );
    return true;
  }

  bool GoogleDriveProvider::list( sync::RemoteFiles &files, QString &error )
  {
    files.clear();
    mFolders.clear();
    mFolders.insert( QString(), mRootId );
    return listFolder( mRootId, QString(), files, error );
  }

  bool GoogleDriveProvider::download( const QString &remoteId, const QString &localFile, QString &error )
  {
    QFile file( localFile );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
      error = QStringLiteral( "Cannot write %1" ).arg( localFile );
      return false;
    }
    Response response;
    const QUrl url = api( QStringLiteral( "/drive/v3/files/%1" ).arg( remoteId ), { { QStringLiteral( "alt" ), QStringLiteral( "media" ) } } );
    if ( !send( "GET", url, {}, nullptr, response, error, &file ) )
      return false;
    file.close();
    return ok( response, QStringLiteral( "download" ), error );
  }

  bool GoogleDriveProvider::folderFor( const QString &dirPath, QString &folderId, QString &error )
  {
    if ( mFolders.contains( dirPath ) )
    {
      folderId = mFolders.value( dirPath );
      return true;
    }
    const int slash = dirPath.lastIndexOf( QLatin1Char( '/' ) );
    QString parentId;
    if ( !folderFor( slash < 0 ? QString() : dirPath.left( slash ), parentId, error ) )
      return false;

    QJsonObject metadata;
    metadata[QStringLiteral( "name" )] = dirPath.mid( slash + 1 );
    metadata[QStringLiteral( "mimeType" )] = QLatin1String( kFolderMimeType );
    metadata[QStringLiteral( "parents" )] = QJsonArray { parentId };
    Response response;
    if ( !sendJson( "POST", api( QStringLiteral( "/drive/v3/files" ), { { QStringLiteral( "fields" ), QStringLiteral( "id" ) } } ), metadata, response, error ) ||
         !ok( response, QStringLiteral( "creating a folder" ), error ) )
      return false;
    folderId = jsonOf( response.body ).value( QStringLiteral( "id" ) ).toString();
    mFolders.insert( dirPath, folderId );
    return true;
  }

  bool GoogleDriveProvider::upload( const QString &localFile, const QString &path, const QString &remoteId, sync::RemoteFile &uploaded, QString &error )
  {
    QFile file( localFile );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
      error = QStringLiteral( "Cannot read %1" ).arg( localFile );
      return false;
    }

    // Resumable upload (works for any size, streams from the file): start a session, then send the bytes.
    const QList<QPair<QString, QString>> query { { QStringLiteral( "uploadType" ), QStringLiteral( "resumable" ) }, { QStringLiteral( "fields" ), kFileFields } };
    QJsonObject metadata;
    QByteArray verb;
    QUrl url;
    if ( remoteId.isEmpty() )
    {
      const int slash = path.lastIndexOf( QLatin1Char( '/' ) );
      QString parentId;
      if ( !folderFor( slash < 0 ? QString() : path.left( slash ), parentId, error ) )
        return false;
      metadata[QStringLiteral( "name" )] = path.mid( slash + 1 );
      metadata[QStringLiteral( "parents" )] = QJsonArray { parentId };
      verb = "POST";
      url = api( QStringLiteral( "/upload/drive/v3/files" ), query );
    }
    else
    {
      verb = "PATCH";
      url = api( QStringLiteral( "/upload/drive/v3/files/%1" ).arg( remoteId ), query );
    }

    Response session;
    if ( !sendJson( verb, url, metadata, session, error ) || !ok( session, QStringLiteral( "upload" ), error ) )
      return false;
    const QUrl sessionUrl( QString::fromUtf8( session.location ) );
    if ( !sessionUrl.isValid() || sessionUrl.host() != mApiBase.host() )
    {
      error = QStringLiteral( "Google Drive: upload failed (no upload session)" );
      return false;
    }

    Response response;
    if ( !send( "PUT", sessionUrl, "application/octet-stream", &file, response, error ) || !ok( response, QStringLiteral( "upload" ), error ) )
      return false;
    const QJsonObject json = jsonOf( response.body );
    uploaded.id = json.value( QStringLiteral( "id" ) ).toString().toStdString();
    uploaded.md5 = json.value( QStringLiteral( "md5Checksum" ) ).toString().toStdString();
    uploaded.size = json.value( QStringLiteral( "size" ) ).toString().toLongLong();
    return true;
  }

  bool GoogleDriveProvider::trash( const QString &remoteId, QString &error )
  {
    Response response;
    return sendJson( "PATCH", api( QStringLiteral( "/drive/v3/files/%1" ).arg( remoteId ) ), QJsonObject { { QStringLiteral( "trashed" ), true } }, response, error ) &&
           ok( response, QStringLiteral( "moving to trash" ), error );
  }

  bool GoogleDriveProvider::findOrCreateFolder( const QString &parentId, const QString &name, QString &folderId, QString &error )
  {
    const QList<QPair<QString, QString>> query {
      { QStringLiteral( "q" ), QStringLiteral( "name = %1 and mimeType = %2 and %3 in parents and trashed = false" )
                                 .arg( quoted( name ), quoted( QLatin1String( kFolderMimeType ) ), quoted( parentId ) ) },
      { QStringLiteral( "fields" ), QStringLiteral( "files(id)" ) },
      { QStringLiteral( "orderBy" ), QStringLiteral( "createdTime" ) },
    };
    Response response;
    if ( !send( "GET", api( QStringLiteral( "/drive/v3/files" ), query ), {}, nullptr, response, error ) || !ok( response, QStringLiteral( "finding a folder" ), error ) )
      return false;
    const QJsonArray found = jsonOf( response.body ).value( QStringLiteral( "files" ) ).toArray();
    if ( !found.isEmpty() )
    {
      folderId = found.first().toObject().value( QStringLiteral( "id" ) ).toString();
      return true;
    }

    QJsonObject metadata;
    metadata[QStringLiteral( "name" )] = name;
    metadata[QStringLiteral( "mimeType" )] = QLatin1String( kFolderMimeType );
    metadata[QStringLiteral( "parents" )] = QJsonArray { parentId };
    if ( !sendJson( "POST", api( QStringLiteral( "/drive/v3/files" ), { { QStringLiteral( "fields" ), QStringLiteral( "id" ) } } ), metadata, response, error ) ||
         !ok( response, QStringLiteral( "creating a folder" ), error ) )
      return false;
    folderId = jsonOf( response.body ).value( QStringLiteral( "id" ) ).toString();
    return true;
  }

} // namespace ufield
