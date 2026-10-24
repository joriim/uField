/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// The app's validator agrees with the Python one on every shared test case
// (schema/tests/cases.json), and the dispatcher's error codes and replies.

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

#include "tooldispatcher.h"

using namespace ufield;

class TestToolSchema : public QObject
{
    Q_OBJECT

  private:
    static QByteArray schemaJson()
    {
      QFile f( QStringLiteral( ":/ufield/schema/tools.json" ) );
      return f.open( QIODevice::ReadOnly ) ? f.readAll() : QByteArray();
    }

  private slots:
    void sharedCases()
    {
      ToolSchema schema;
      QString error;
      QVERIFY2( schema.load( schemaJson(), error ), qPrintable( error ) );

      QFile f( QStringLiteral( UFIELD_CASES_JSON ) );
      QVERIFY( f.open( QIODevice::ReadOnly ) );
      const QJsonObject cases = QJsonDocument::fromJson( f.readAll() ).object();
      QVERIFY( !cases.isEmpty() );

      int checked = 0;
      QStringList failures;
      for ( auto it = cases.begin(); it != cases.end(); ++it )
      {
        QVERIFY2( schema.hasTool( it.key() ), qPrintable( it.key() ) );
        const QJsonObject sets = it.value().toObject();
        for ( const QJsonValue &v : sets.value( "valid" ).toArray() )
        {
          const QStringList errors = schema.validate( it.key(), v.toObject() );
          if ( !errors.isEmpty() )
            failures << QStringLiteral( "%1 valid %2 rejected: %3" ).arg( it.key(), QString::fromUtf8( QJsonDocument( v.toObject() ).toJson( QJsonDocument::Compact ) ), errors.first() );
          ++checked;
        }
        for ( const QJsonValue &v : sets.value( "invalid" ).toArray() )
        {
          // Cases are JSON objects except where a test feeds a non-object; those are invalid by type.
          if ( v.isObject() && schema.validate( it.key(), v.toObject() ).isEmpty() )
            failures << QStringLiteral( "%1 invalid %2 accepted" ).arg( it.key(), QString::fromUtf8( QJsonDocument( v.toObject() ).toJson( QJsonDocument::Compact ) ) );
          ++checked;
        }
      }
      QVERIFY2( failures.isEmpty(), qPrintable( failures.join( "\n" ) ) );
      QVERIFY( checked >= 120 );
      // every tool has cases (validate_schema checks this too)
      for ( const QString &tool : schema.toolNames() )
        QVERIFY2( cases.contains( tool ), qPrintable( tool ) );
    }

    void keywordDetails()
    {
      ToolSchema schema;
      QString error;
      const QByteArray doc = R"({
        "$defs": { "id": { "type": "string", "pattern": "^[a-z]{1,3}$" }, "loop": { "$ref": "#/$defs/loop" } },
        "tools": [ { "name": "t", "description": "", "inputSchema": { "type": "object", "additionalProperties": false, "properties": {
          "id": { "$ref": "#/$defs/id" }, "n": { "type": "integer", "minimum": 1 }, "s": { "type": "string", "maxLength": 2 },
          "a": { "type": "array", "uniqueItems": true, "items": { "type": "number" } }, "c": { "const": 1 }, "r": { "$ref": "#/$defs/loop" },
          "missing": { "$ref": "#/$defs/nope" } } } } ] })";
      QVERIFY2( schema.load( doc, error ), qPrintable( error ) );
      auto valid = [&]( const char *json ) { return schema.validate( "t", QJsonDocument::fromJson( json ).object() ).isEmpty(); };
      QVERIFY( valid( R"({"id":"abc","n":2.0,"s":"μä","a":[1,2],"c":1.0})" ) ); // 2.0 is an integer; length counts code points
      QVERIFY( !valid( R"({"id":"abcd"})" ) );
      QVERIFY( !valid( R"({"n":1.5})" ) );
      QVERIFY( !valid( R"({"n":0})" ) );
      QVERIFY( !valid( R"({"s":"abc"})" ) );
      QVERIFY( !valid( R"({"a":[1,1.0]})" ) );
      QVERIFY( !valid( R"({"c":2})" ) );
      QVERIFY( !valid( R"({"other":1})" ) );
      QVERIFY( !valid( R"({"r":1})" ) );       // a $ref cycle ends in an error, not a hang
      QVERIFY( !valid( R"({"missing":1})" ) ); // an unknown $ref is an error
    }

    void credentialsInUrls()
    {
      QVERIFY( !ToolSchema::credentialInUrl( "https://user:pw@example.com/wms" ).isEmpty() );
      QVERIFY( !ToolSchema::credentialInUrl( "https://example.com/wms?API-Key=1" ).isEmpty() );
      QVERIFY( !ToolSchema::credentialInUrl( "https://example.com/wms?access_token=1" ).isEmpty() );
      QVERIFY( ToolSchema::credentialInUrl( "https://example.com/wms?service=WMS&layers=a" ).isEmpty() );
    }

    void dispatcherReplies()
    {
      ToolDispatcher dispatcher( schemaJson() );
      QVERIFY( dispatcher.isValid() );
      QVERIFY( !dispatcher.registerTool( "no_such_tool", []( const QJsonObject &, const ToolContext &, ToolDispatcher::Reply ) {} ) );

      QJsonObject result;
      auto keep = [&result]( const QJsonObject &r ) { result = r; };
      dispatcher.call( "no_such_tool", {}, {}, keep );
      QCOMPARE( result.value( "error" ).toString(), QStringLiteral( "unknown_tool" ) );
      dispatcher.call( "sync_project", { { "direction", "sideways" } }, {}, keep );
      QCOMPARE( result.value( "error" ).toString(), QStringLiteral( "invalid_input" ) );
      QVERIFY( result.value( "details" ).isArray() );
      dispatcher.call( "sync_project", {}, {}, keep );
      QCOMPARE( result.value( "error" ).toString(), QStringLiteral( "not_implemented" ) );

      int calls = 0;
      QVERIFY( dispatcher.registerTool( "sync_project", [&calls]( const QJsonObject &args, const ToolContext &ctx, ToolDispatcher::Reply reply ) {
        ++calls;
        reply( { { "direction", args.value( "direction" ).toString( "both" ) }, { "ui", ctx.client == ToolClient::Ui } } );
        reply( { { "second", true } } ); // ignored
      } ) );
      int replies = 0;
      dispatcher.call( "sync_project", { { "direction", "pull" } }, {}, [&]( const QJsonObject &r ) { ++replies; result = r; } );
      QCOMPARE( calls, 1 );
      QCOMPARE( replies, 1 );
      QCOMPARE( result.value( "direction" ).toString(), QStringLiteral( "pull" ) );

      // from QML: an id first, then the signal
      QSignalSpy finished( &dispatcher, &ToolDispatcher::toolFinished );
      const int id = dispatcher.callFromUi( "sync_project", { { "direction", "push" } } );
      QCOMPARE( finished.size(), 0 );
      QVERIFY( finished.wait( 1000 ) );
      QCOMPARE( finished.at( 0 ).at( 0 ).toInt(), id );
      QCOMPARE( finished.at( 0 ).at( 1 ).toMap().value( "direction" ).toString(), QStringLiteral( "push" ) );
      QVERIFY( finished.at( 0 ).at( 1 ).toMap().value( "ui" ).toBool() );
    }
};

QTEST_GUILESS_MAIN( TestToolSchema )
#include "test_toolschema.moc"
