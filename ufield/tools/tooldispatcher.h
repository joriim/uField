/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_TOOLDISPATCHER_H
#define UFIELD_TOOLDISPATCHER_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

#include <functional>

#include "toolschema.h"

namespace ufield
{

  //! Who is calling a tool (as in server/src/ufield/dispatcher.py).
  enum class ToolClient
  {
    Ui,              //!< the app's own screens: the user acting directly
    VoiceTranscript, //!< verbatim speech-to-text of the user
    VoiceAgent,      //!< Claude acting on spoken commands
    Mcp,             //!< an MCP client
  };

  struct ToolContext
  {
    ToolClient client = ToolClient::Ui;
    QString clientName;
    QString model;
  };

  /**
   * The app's one entry point for tool calls (CLAUDE.md rule 1): the UI, and later the voice
   * agent, call tools here by name with JSON arguments. Input is validated against
   * schema/tools.json (ToolSchema) before the handler runs.
   *
   * Handlers may finish later (sync runs on a worker thread): they get a Reply to call
   * exactly once, on this object's thread. A reply is the tool's result object, or an error
   * object { error, message, details? } with a stable `error` code: unknown_tool,
   * invalid_input, not_implemented, or one the handler defines.
   */
  class ToolDispatcher : public QObject
  {
      Q_OBJECT

    public:
      using Reply = std::function<void( const QJsonObject &result )>;
      using Handler = std::function<void( const QJsonObject &args, const ToolContext &context, Reply reply )>;

      //! \a schemaJson is the content of schema/tools.json.
      explicit ToolDispatcher( const QByteArray &schemaJson, QObject *parent = nullptr );

      bool isValid() const { return mValid; }
      const ToolSchema &schema() const { return mSchema; }

      //! Registers the handler for \a tool; false if the schema has no such tool.
      bool registerTool( const QString &tool, Handler handler );
      bool isImplemented( const QString &tool ) const { return mHandlers.contains( tool ); }

      //! Validates and runs \a tool; \a reply gets the result or an error object.
      void call( const QString &tool, const QJsonObject &args, const ToolContext &context, Reply reply );

      /**
       * For QML: calls \a tool as the UI and returns a call id; toolFinished( id, result ) follows,
       * possibly before this returns to QML's next frame but never synchronously inside it.
       */
      Q_INVOKABLE int callFromUi( const QString &tool, const QVariantMap &args );

      static QJsonObject error( const QString &code, const QString &message, const QJsonValue &details = QJsonValue() );
      static bool isError( const QJsonObject &result ) { return result.contains( QStringLiteral( "error" ) ); }

    signals:
      void toolFinished( int callId, const QVariantMap &result );

    private:
      ToolSchema mSchema;
      bool mValid = false;
      QString mLoadError;
      QHash<QString, Handler> mHandlers;
      int mNextCallId = 1;
  };

} // namespace ufield

#endif // UFIELD_TOOLDISPATCHER_H
