/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_GOOGLEDRIVEPROVIDER_H
#define UFIELD_GOOGLEDRIVEPROVIDER_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

#include "remotestore.h"

class QIODevice;
class QNetworkReply;
class QNetworkRequest;

namespace ufield
{

  /**
   * One project folder in Google Drive, through the Drive REST API v3 with the
   * `drive.file` scope (ADR 0004).
   *
   * Blocking: create and use it on the sync worker thread. The access token comes from
   * \a token, called before each request and again once after a 401; on Android it asks
   * Play services (DriveAuth), which refreshes as needed. The token is kept only for the
   * request and never appears in errors or logs (CLAUDE.md rule 7).
   */
  class GoogleDriveProvider : public RemoteStore
  {
    public:
      //! Returns an access token, or an empty string and sets \a error (which must not contain a token).
      using TokenProvider = std::function<QString( bool forceRefresh, QString &error )>;

      static constexpr const char *kFolderMimeType = "application/vnd.google-apps.folder";

      GoogleDriveProvider( const QString &projectFolderId, TokenProvider token,
                           const QUrl &apiBase = QUrl( QStringLiteral( "https://www.googleapis.com" ) ) );
      ~GoogleDriveProvider() override;

      bool list( sync::RemoteFiles &files, QString &error ) override;
      bool download( const QString &remoteId, const QString &localFile, QString &error ) override;
      bool upload( const QString &localFile, const QString &path, const QString &remoteId, sync::RemoteFile &uploaded, QString &error ) override;
      bool trash( const QString &remoteId, QString &error ) override;

      /**
       * The id of the folder \a name in \a parentId ("root" for My Drive), created if missing.
       * With drive.file only folders this app (or its desktop client) created are found.
       */
      bool findOrCreateFolder( const QString &parentId, const QString &name, QString &folderId, QString &error );

      //! The folders directly in \a parentId as (name, id), oldest first.
      bool listFolders( const QString &parentId, QList<QPair<QString, QString>> &folders, QString &error );

    private:
      struct Response
      {
        int status = 0;
        QByteArray body;
        QByteArray location;
      };

      //! Sends a request with the bearer token and waits. \a body may be null; \a sink receives the body instead of Response::body.
      bool send( const QByteArray &verb, const QUrl &url, const QByteArray &contentType, QIODevice *body, Response &response,
                 QString &error, QIODevice *sink = nullptr );
      bool sendJson( const QByteArray &verb, const QUrl &url, const QJsonObject &json, Response &response, QString &error );
      bool ok( const Response &response, const QString &what, QString &error ) const;
      QUrl api( const QString &path, const QList<QPair<QString, QString>> &query = {} ) const;
      bool listFolder( const QString &folderId, const QString &prefix, sync::RemoteFiles &files, QString &error );
      bool folderFor( const QString &dirPath, QString &folderId, QString &error );

      QString mRootId;
      TokenProvider mToken;
      QUrl mApiBase;
      std::unique_ptr<QNetworkAccessManager> mNam;
      QHash<QString, QString> mFolders; //!< project-relative folder path -> Drive folder id; "" is the project folder
  };

} // namespace ufield

#endif // UFIELD_GOOGLEDRIVEPROVIDER_H
