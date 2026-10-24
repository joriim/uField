/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "tooldispatcher.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace ufield
{

  ToolDispatcher::ToolDispatcher( const QByteArray &schemaJson, QObject *parent )
    : QObject( parent )
  {
    mValid = mSchema.load( schemaJson, mLoadError );
  }

  QJsonObject ToolDispatcher::error( const QString &code, const QString &message, const QJsonValue &details )
  {
    QJsonObject out { { QStringLiteral( "error" ), code }, { QStringLiteral( "message" ), message } };
    if ( !details.isUndefined() && !details.isNull() )
      out.insert( QStringLiteral( "details" ), details );
    return out;
  }

  bool ToolDispatcher::registerTool( const QString &tool, Handler handler )
  {
    if ( !mSchema.hasTool( tool ) || !handler )
      return false;
    mHandlers.insert( tool, std::move( handler ) );
    return true;
  }

  void ToolDispatcher::call( const QString &tool, const QJsonObject &args, const ToolContext &context, Reply reply )
  {
    if ( !mValid )
    {
      reply( error( QStringLiteral( "not_implemented" ), mLoadError ) );
      return;
    }
    if ( !mSchema.hasTool( tool ) )
    {
      reply( error( QStringLiteral( "unknown_tool" ), QStringLiteral( "no tool named %1" ).arg( tool ) ) );
      return;
    }
    const QStringList errors = mSchema.validate( tool, args );
    if ( !errors.isEmpty() )
    {
      reply( error( QStringLiteral( "invalid_input" ), errors.join( QStringLiteral( "; " ) ), QJsonArray::fromStringList( errors ) ) );
      return;
    }
    const auto handler = mHandlers.constFind( tool );
    if ( handler == mHandlers.constEnd() )
    {
      reply( error( QStringLiteral( "not_implemented" ), QStringLiteral( "%1 is not available here yet" ).arg( tool ) ) );
      return;
    }
    // A handler that replies twice is a bug; only the first reply counts.
    auto replied = std::make_shared<bool>( false );
    ( *handler )( args, context, [reply, replied]( const QJsonObject &result ) {
      if ( *replied )
        return;
      *replied = true;
      reply( result );
    } );
  }

  int ToolDispatcher::callFromUi( const QString &tool, const QVariantMap &args )
  {
    const int id = mNextCallId++;
    const QJsonObject jsonArgs = QJsonObject::fromVariantMap( args );
    QPointer<ToolDispatcher> self( this );
    // Deferred, so QML has the id before the result can arrive.
    QTimer::singleShot( 0, this, [self, id, tool, jsonArgs] {
      if ( !self )
        return;
      self->call( tool, jsonArgs, ToolContext { ToolClient::Ui, QStringLiteral( "ufield-app" ), QString() }, [self, id]( const QJsonObject &result ) {
        if ( self )
          emit self->toolFinished( id, result.toVariantMap() );
      } );
    } );
    return id;
  }

} // namespace ufield
