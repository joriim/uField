/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_DRIVESYNC_H
#define UFIELD_DRIVESYNC_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "googledriveprovider.h"

namespace ufield
{

  /**
   * Google Drive sync for the app (ADR 0004), behind the `sync_project` and
   * `list_projects(storage: google_drive)` tools and the Drive UI.
   *
   * Every project lives in the app folder `μField/` in My Drive, one subfolder per
   * project. A local project is linked to its folder by `.ufield/drive.json` (folder id
   * only, no credentials; `.ufield/` is never synced).
   *
   * Each call runs on its own worker thread and reports back with a signal on the
   * object's thread; one call at a time (`busy`).
   */
  class DriveSync : public QObject
  {
      Q_OBJECT
      Q_PROPERTY( bool busy READ busy NOTIFY busyChanged )
      Q_PROPERTY( QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged )

    public:
      //! Name of the app folder in My Drive.
      static QString appFolderName();

      using TokenProviderFactory = std::function<GoogleDriveProvider::TokenProvider()>;

      //! \a tokens makes a token provider for each call; by default Play services, interactive (driveTokenProvider).
      explicit DriveSync( QObject *parent = nullptr, TokenProviderFactory tokens = {},
                          const QUrl &apiBase = QUrl( QStringLiteral( "https://www.googleapis.com" ) ) );

      bool busy() const { return mBusy; }
      QString deviceName() const { return mDeviceName; }
      void setDeviceName( const QString &name );

      //! The Drive folder id the project is linked to, or empty.
      static QString linkedFolderId( const QString &projectDir );

      /**
       * Syncs \a projectDir with Drive, linking it to `μField/<folder name>` first if it is not
       * linked yet (the folder is created, or an existing one with that name is used).
       * \a direction is "both", "push" or "pull". Emits syncFinished. Returns false if busy or
       * the arguments are invalid (then nothing is emitted).
       */
      Q_INVOKABLE bool syncProject( const QString &projectDir, const QString &direction = QStringLiteral( "both" ) );

      //! Lists the project folders in `μField/`. Emits projectsListed with [{ name, folder_id }].
      Q_INVOKABLE bool listDriveProjects();

      /**
       * Downloads the Drive project \a folderId into a new folder \a name under \a parentDir
       * and links it. Emits syncFinished. Fails if the target folder already exists.
       */
      Q_INVOKABLE bool downloadProject( const QString &folderId, const QString &name, const QString &parentDir );

    signals:
      void busyChanged();
      void deviceNameChanged();
      /**
       * \a result: ok, error, project_dir, uploaded, downloaded, deleted, skipped, conflicts (list of
       * conflict copy paths), matching the `sync_project` tool's result.
       */
      void syncFinished( const QVariantMap &result );
      //! \a projects: [{ name, folder_id }]; empty with a non-empty \a error on failure.
      void projectsListed( const QVariantList &projects, const QString &error );

    private:
      bool start( std::function<void()> work );
      void finish();
      QString conflictSuffix() const;
      GoogleDriveProvider::TokenProvider tokens() const;

      TokenProviderFactory mTokens;
      QUrl mApiBase;
      QString mDeviceName;
      bool mBusy = false;
  };

} // namespace ufield

#endif // UFIELD_DRIVESYNC_H
