/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "driveauth.h"

#include <QCoreApplication>
#include <memory>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace ufield
{

  QString driveAuthMessage( const QString &code )
  {
    if ( code == QLatin1String( "cancelled" ) )
      return QCoreApplication::translate( "ufield", "Google sign-in was cancelled" );
    if ( code == QLatin1String( kDriveConsentRequired ) )
      return QCoreApplication::translate( "ufield", "Sign in to Google Drive first" );
    if ( code == QLatin1String( "in_progress" ) )
      return QCoreApplication::translate( "ufield", "Google sign-in is already open" );
    if ( code == QLatin1String( "timeout" ) )
      return QCoreApplication::translate( "ufield", "Google sign-in timed out" );
    if ( code == QLatin1String( "not_granted" ) )
      return QCoreApplication::translate( "ufield", "Google Drive access was not granted" );
    if ( code.startsWith( QLatin1String( "failed:" ) ) )
      return QCoreApplication::translate( "ufield", "Google sign-in failed (%1)" ).arg( code.mid( 7 ) );
    if ( code.isEmpty() )
      return QCoreApplication::translate( "ufield", "Not signed in to Google Drive" );
    return QCoreApplication::translate( "ufield", "Google sign-in failed (%1)" ).arg( code ); // no_token, main_thread
  }

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
        error = QCoreApplication::translate( "ufield", "Google sign-in is not available on this device" );
        return QString();
      }
      const jobjectArray pair = result.object<jobjectArray>();
      const QString token = QJniObject::fromLocalRef( env->GetObjectArrayElement( pair, 0 ) ).toString();
      const QString message = QJniObject::fromLocalRef( env->GetObjectArrayElement( pair, 1 ) ).toString();
      if ( token.isEmpty() )
      {
        lastToken->clear();
        error = driveAuthMessage( message );
        return QString();
      }
      *lastToken = token;
      return token;
    };
#else
    Q_UNUSED( interactive )
    return []( bool, QString &error ) -> QString {
      error = QCoreApplication::translate( "ufield", "Google Drive sign-in is only available on Android" );
      return QString();
    };
#endif
  }

} // namespace ufield
