/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "driveauth.h"

#include <memory>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace ufield
{

  GoogleDriveProvider::TokenProvider driveTokenProvider( bool interactive )
  {
#ifdef Q_OS_ANDROID
    auto lastToken = std::make_shared<QString>();
    return [lastToken, interactive]( bool forceRefresh, QString &error ) -> QString {
      const QJniObject context( QNativeInterface::QAndroidApplication::context() );
      const QJniObject stale = QJniObject::fromString( forceRefresh ? *lastToken : QString() );
      const QJniObject result = QJniObject::callStaticObjectMethod(
        "fi/biomitta/ufield/DriveAuth", "accessToken", "(Landroid/content/Context;Ljava/lang/String;Z)[Ljava/lang/String;",
        context.object(), stale.object<jstring>(), static_cast<jboolean>( interactive ) );

      QJniEnvironment env;
      if ( env.checkAndClearExceptions() || !result.isValid() )
      {
        error = QStringLiteral( "Google sign-in is not available on this device" );
        return QString();
      }
      const jobjectArray pair = result.object<jobjectArray>();
      const QString token = QJniObject::fromLocalRef( env->GetObjectArrayElement( pair, 0 ) ).toString();
      const QString message = QJniObject::fromLocalRef( env->GetObjectArrayElement( pair, 1 ) ).toString();
      if ( token.isEmpty() )
      {
        lastToken->clear();
        error = message.isEmpty() ? QStringLiteral( "Not signed in to Google Drive" ) : message;
        return QString();
      }
      *lastToken = token;
      return token;
    };
#else
    Q_UNUSED( interactive )
    return []( bool, QString &error ) -> QString {
      error = QStringLiteral( "Google Drive sign-in is only available on Android" );
      return QString();
    };
#endif
  }

} // namespace ufield
