/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "toolschema.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>

namespace ufield
{

  namespace
  {
    const int kMaxDepth = 64; // $ref cycles in a broken schema must not recurse forever

    bool isInteger( const QJsonValue &v )
    {
      if ( !v.isDouble() )
        return false;
      const double d = v.toDouble();
      return std::isfinite( d ) && std::floor( d ) == d;
    }

    bool hasType( const QJsonValue &v, const QString &type )
    {
      if ( type == QLatin1String( "object" ) )
        return v.isObject();
      if ( type == QLatin1String( "array" ) )
        return v.isArray();
      if ( type == QLatin1String( "string" ) )
        return v.isString();
      if ( type == QLatin1String( "number" ) )
        return v.isDouble();
      if ( type == QLatin1String( "integer" ) )
        return isInteger( v );
      if ( type == QLatin1String( "boolean" ) )
        return v.isBool();
      if ( type == QLatin1String( "null" ) )
        return v.isNull();
      return false;
    }

    // JSON equality as JSON Schema defines it: 1 and 1.0 are equal; key order does not matter.
    bool jsonEqual( const QJsonValue &a, const QJsonValue &b )
    {
      if ( a.isDouble() && b.isDouble() )
        return a.toDouble() == b.toDouble();
      return a == b;
    }

    // Length in Unicode code points, as JSON Schema (and Python) count it, not UTF-16 units.
    qsizetype codePoints( const QString &s ) { return s.toUcs4().size(); }

    QString at( const QString &path ) { return path.isEmpty() ? QString() : path + QStringLiteral( ": " ); }

    QString join( const QJsonArray &groups )
    {
      QStringList out;
      for ( const QJsonValue &g : groups )
      {
        QStringList names;
        for ( const QJsonValue &n : g.toArray() )
          names << n.toString();
        out << names.join( QLatin1Char( '+' ) );
      }
      return out.join( QStringLiteral( " | " ) );
    }
  } // namespace

  bool ToolSchema::load( const QByteArray &json, QString &error )
  {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( json, &parseError );
    if ( !doc.isObject() || !doc.object().value( QStringLiteral( "tools" ) ).isArray() )
    {
      error = QStringLiteral( "tools.json: %1" ).arg( parseError.error ? parseError.errorString() : QStringLiteral( "no tools array" ) );
      return false;
    }
    mDefs = doc.object().value( QStringLiteral( "$defs" ) ).toObject();
    mTools.clear();
    for ( const QJsonValue &t : doc.object().value( QStringLiteral( "tools" ) ).toArray() )
    {
      const QJsonObject tool = t.toObject();
      mTools.insert( tool.value( QStringLiteral( "name" ) ).toString(), tool.value( QStringLiteral( "inputSchema" ) ).toObject() );
    }
    return true;
  }

  QStringList ToolSchema::toolNames() const
  {
    QStringList names = mTools.keys();
    names.sort();
    return names;
  }

  QJsonObject ToolSchema::resolve( const QJsonObject &schema ) const
  {
    const QString ref = schema.value( QStringLiteral( "$ref" ) ).toString();
    QJsonObject target = mDefs.value( ref.mid( 8 ) ).toObject();
    // Keywords next to $ref apply too (2020-12); the def's own keywords come first.
    for ( auto it = schema.begin(); it != schema.end(); ++it )
      if ( it.key() != QLatin1String( "$ref" ) )
        target.insert( it.key(), it.value() );
    return target;
  }

  void ToolSchema::check( const QJsonObject &rawSchema, const QJsonValue &value, const QString &path, QStringList &errors, int depth ) const
  {
    if ( depth > kMaxDepth )
    {
      errors << at( path ) + QStringLiteral( "schema is nested too deeply" );
      return;
    }
    QJsonObject schema = rawSchema;
    for ( int hops = 0; schema.contains( QStringLiteral( "$ref" ) ); ++hops )
    {
      const QString ref = schema.value( QStringLiteral( "$ref" ) ).toString();
      if ( hops >= kMaxDepth || !ref.startsWith( QLatin1String( "#/$defs/" ) ) || !mDefs.contains( ref.mid( 8 ) ) )
      {
        errors << at( path ) + QStringLiteral( "schema reference %1 cannot be resolved" ).arg( ref );
        return;
      }
      schema = resolve( schema );
    }

    // type
    const QJsonValue type = schema.value( QStringLiteral( "type" ) );
    if ( type.isString() || type.isArray() )
    {
      QStringList types;
      if ( type.isString() )
        types << type.toString();
      for ( const QJsonValue &t : type.toArray() )
        types << t.toString();
      bool ok = false;
      for ( const QString &t : types )
        ok = ok || hasType( value, t );
      if ( !ok )
      {
        errors << at( path ) + QStringLiteral( "expected %1" ).arg( types.join( QStringLiteral( " or " ) ) );
        return; // the other keywords would only repeat the type error
      }
    }

    if ( schema.contains( QStringLiteral( "const" ) ) && !jsonEqual( value, schema.value( QStringLiteral( "const" ) ) ) )
      errors << at( path ) + QStringLiteral( "must be %1" ).arg( QString::fromUtf8( QJsonDocument( QJsonArray { schema.value( QStringLiteral( "const" ) ) } ).toJson( QJsonDocument::Compact ) ).mid( 1 ).chopped( 1 ) );

    if ( schema.value( QStringLiteral( "enum" ) ).isArray() )
    {
      bool found = false;
      for ( const QJsonValue &option : schema.value( QStringLiteral( "enum" ) ).toArray() )
        found = found || jsonEqual( value, option );
      if ( !found )
        errors << at( path ) + QStringLiteral( "is not one of the allowed values" );
    }

    if ( schema.value( QStringLiteral( "not" ) ).isObject() )
    {
      QStringList inner;
      check( schema.value( QStringLiteral( "not" ) ).toObject(), value, path, inner, depth + 1 );
      if ( inner.isEmpty() )
        errors << at( path ) + QStringLiteral( "is not allowed" );
    }

    if ( value.isDouble() )
    {
      const double d = value.toDouble();
      if ( schema.contains( QStringLiteral( "minimum" ) ) && d < schema.value( QStringLiteral( "minimum" ) ).toDouble() )
        errors << at( path ) + QStringLiteral( "must be at least %1" ).arg( schema.value( QStringLiteral( "minimum" ) ).toDouble() );
      if ( schema.contains( QStringLiteral( "maximum" ) ) && d > schema.value( QStringLiteral( "maximum" ) ).toDouble() )
        errors << at( path ) + QStringLiteral( "must be at most %1" ).arg( schema.value( QStringLiteral( "maximum" ) ).toDouble() );
    }

    if ( value.isString() )
    {
      const QString s = value.toString();
      if ( schema.contains( QStringLiteral( "minLength" ) ) && codePoints( s ) < schema.value( QStringLiteral( "minLength" ) ).toInt() )
        errors << at( path ) + QStringLiteral( "is too short" );
      if ( schema.contains( QStringLiteral( "maxLength" ) ) && codePoints( s ) > schema.value( QStringLiteral( "maxLength" ) ).toInt() )
        errors << at( path ) + QStringLiteral( "is too long" );
      if ( schema.value( QStringLiteral( "pattern" ) ).isString() )
      {
        // JSON Schema patterns search anywhere in the string unless anchored.
        const QRegularExpression re( schema.value( QStringLiteral( "pattern" ) ).toString() );
        if ( !re.isValid() )
          errors << at( path ) + QStringLiteral( "schema pattern is invalid" );
        else if ( !re.match( s ).hasMatch() )
          errors << at( path ) + QStringLiteral( "does not match the expected format" );
      }
    }

    if ( value.isArray() )
    {
      const QJsonArray a = value.toArray();
      if ( schema.contains( QStringLiteral( "minItems" ) ) && a.size() < schema.value( QStringLiteral( "minItems" ) ).toInt() )
        errors << at( path ) + QStringLiteral( "needs at least %1 item(s)" ).arg( schema.value( QStringLiteral( "minItems" ) ).toInt() );
      if ( schema.contains( QStringLiteral( "maxItems" ) ) && a.size() > schema.value( QStringLiteral( "maxItems" ) ).toInt() )
        errors << at( path ) + QStringLiteral( "allows at most %1 item(s)" ).arg( schema.value( QStringLiteral( "maxItems" ) ).toInt() );
      if ( schema.value( QStringLiteral( "uniqueItems" ) ).toBool() )
      {
        for ( qsizetype i = 0; i < a.size(); ++i )
          for ( qsizetype j = i + 1; j < a.size(); ++j )
            if ( jsonEqual( a.at( i ), a.at( j ) ) )
            {
              errors << at( path ) + QStringLiteral( "has duplicate items" );
              i = j = a.size();
            }
      }
      if ( schema.value( QStringLiteral( "items" ) ).isObject() )
      {
        const QJsonObject items = schema.value( QStringLiteral( "items" ) ).toObject();
        for ( qsizetype i = 0; i < a.size(); ++i )
          check( items, a.at( i ), path.isEmpty() ? QString::number( i ) : path + QLatin1Char( '/' ) + QString::number( i ), errors, depth + 1 );
      }
    }

    if ( value.isObject() )
    {
      const QJsonObject o = value.toObject();
      for ( const QJsonValue &r : schema.value( QStringLiteral( "required" ) ).toArray() )
        if ( !o.contains( r.toString() ) )
          errors << at( path ) + QStringLiteral( "'%1' is required" ).arg( r.toString() );

      const QJsonObject properties = schema.value( QStringLiteral( "properties" ) ).toObject();
      const QJsonValue additional = schema.value( QStringLiteral( "additionalProperties" ) );
      for ( auto it = o.begin(); it != o.end(); ++it )
      {
        const QString childPath = path.isEmpty() ? it.key() : path + QLatin1Char( '/' ) + it.key();
        if ( properties.contains( it.key() ) )
          check( properties.value( it.key() ).toObject(), it.value(), childPath, errors, depth + 1 );
        else if ( additional.isBool() && !additional.toBool() )
          errors << at( path ) + QStringLiteral( "unknown property '%1'" ).arg( it.key() );
        else if ( additional.isObject() )
          check( additional.toObject(), it.value(), childPath, errors, depth + 1 );
      }
    }
  }

  QStringList ToolSchema::checkRules( const QJsonObject &inputSchema, const QJsonObject &args )
  {
    const QJsonObject rules = inputSchema.value( QStringLiteral( "x-ufield-rules" ) ).toObject();
    const QJsonObject props = inputSchema.value( QStringLiteral( "properties" ) ).toObject();
    QStringList errors;

    auto used = [&args]( const QJsonArray &group ) {
      for ( const QJsonValue &p : group )
        if ( args.contains( p.toString() ) )
          return true;
      return false;
    };
    auto complete = [&args]( const QJsonArray &group ) {
      for ( const QJsonValue &p : group )
        if ( !args.contains( p.toString() ) )
          return false;
      return true;
    };

    const QJsonArray exactly = rules.value( QStringLiteral( "exactly_one_of" ) ).toArray();
    if ( !exactly.isEmpty() )
    {
      QJsonArray touched;
      for ( const QJsonValue &g : exactly )
        if ( used( g.toArray() ) )
          touched.append( g );
      if ( touched.size() != 1 || !complete( touched.at( 0 ).toArray() ) )
        errors << QStringLiteral( "give exactly one of: %1" ).arg( join( exactly ) );
    }

    const QJsonArray atLeast = rules.value( QStringLiteral( "at_least_one_of" ) ).toArray();
    if ( !atLeast.isEmpty() )
    {
      bool any = false;
      for ( const QJsonValue &g : atLeast )
        any = any || complete( g.toArray() );
      if ( !any )
        errors << QStringLiteral( "give at least one of: %1" ).arg( join( atLeast ) );
    }

    const QJsonArray atMost = rules.value( QStringLiteral( "at_most_one_of" ) ).toArray();
    if ( !atMost.isEmpty() )
    {
      int count = 0;
      for ( const QJsonValue &g : atMost )
        count += used( g.toArray() ) ? 1 : 0;
      if ( count > 1 )
        errors << QStringLiteral( "give at most one of: %1" ).arg( join( atMost ) );
    }

    for ( const QJsonValue &c : rules.value( QStringLiteral( "required_if" ) ).toArray() )
    {
      const QJsonObject cond = c.toObject();
      const QJsonObject when = cond.value( QStringLiteral( "if" ) ).toObject();
      bool matches = true;
      for ( auto it = when.begin(); it != when.end(); ++it )
      {
        const QJsonValue actual = args.contains( it.key() ) ? args.value( it.key() ) : props.value( it.key() ).toObject().value( QStringLiteral( "default" ) );
        matches = matches && jsonEqual( actual, it.value() );
      }
      if ( !matches )
        continue;
      QStringList missing;
      for ( const QJsonValue &p : cond.value( QStringLiteral( "then_required" ) ).toArray() )
        if ( !args.contains( p.toString() ) )
          missing << p.toString();
      if ( !missing.isEmpty() )
        errors << QStringLiteral( "when %1, also give: %2" ).arg( QString::fromUtf8( QJsonDocument( when ).toJson( QJsonDocument::Compact ) ), missing.join( QStringLiteral( ", " ) ) );
    }
    return errors;
  }

  QString ToolSchema::credentialInUrl( const QString &url )
  {
    static const QSet<QString> secretParams { "apikey", "key", "token", "accesstoken", "password", "passwd", "sig", "signature", "secret" };
    const QUrl parsed( url );
    if ( !parsed.userInfo().isEmpty() || parsed.authority().contains( QLatin1Char( '@' ) ) )
      return QStringLiteral( "the URL contains a user name or password" );
    const QUrlQuery query( parsed );
    for ( const auto &item : query.queryItems( QUrl::FullyDecoded ) )
    {
      QString name = item.first.toLower();
      name.remove( QLatin1Char( '-' ) ).remove( QLatin1Char( '_' ) );
      if ( secretParams.contains( name ) )
        return QStringLiteral( "the URL contains the parameter '%1'" ).arg( item.first );
    }
    return QString();
  }

  QStringList ToolSchema::validate( const QString &tool, const QJsonObject &args ) const
  {
    if ( !mTools.contains( tool ) )
      return { QStringLiteral( "no tool named %1" ).arg( tool ) };
    const QJsonObject schema = mTools.value( tool );
    QStringList errors;
    check( schema, args, QString(), errors, 0 );
    if ( errors.isEmpty() )
      errors << checkRules( schema, args );
    const QJsonValue url = args.value( QStringLiteral( "url" ) );
    if ( url.isString() )
    {
      const QString reason = credentialInUrl( url.toString() );
      if ( !reason.isEmpty() )
        errors << QStringLiteral( "url: %1. Add the credential in the app's settings and pass its auth_config_id instead." ).arg( reason );
    }
    return errors;
  }

} // namespace ufield
