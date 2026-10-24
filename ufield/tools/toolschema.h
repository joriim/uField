/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_TOOLSCHEMA_H
#define UFIELD_TOOLSCHEMA_H

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace ufield
{

  /**
   * schema/tools.json in the app: tool input validation with the same results as the
   * Python ToolDispatcher (server/src/ufield/dispatcher.py), checked against the shared
   * cases in schema/tests/cases.json.
   *
   * Supports the JSON Schema 2020-12 keywords tools.json uses: type, enum, const,
   * properties, required, additionalProperties, items, minItems, maxItems, uniqueItems,
   * minimum, maximum, minLength, maxLength, pattern, not, and local $ref to #/$defs.
   * "format" is an annotation, as in the Python validator. Then the tool's
   * x-ufield-rules (exactly_one_of, at_least_one_of, at_most_one_of, required_if) and
   * the credential-in-URL check on a "url" argument (ADR 0002 §5).
   */
  class ToolSchema
  {
    public:
      //! Loads the tools document; returns false and sets \a error if it is not a valid tools.json.
      bool load( const QByteArray &json, QString &error );

      bool hasTool( const QString &name ) const { return mTools.contains( name ); }
      QStringList toolNames() const;

      //! Every reason \a args is not a valid input for \a tool (empty = valid).
      QStringList validate( const QString &tool, const QJsonObject &args ) const;

      //! Why \a url carries a credential, or empty if it does not.
      static QString credentialInUrl( const QString &url );

    private:
      void check( const QJsonObject &schema, const QJsonValue &value, const QString &path, QStringList &errors, int depth ) const;
      QJsonObject resolve( const QJsonObject &schema ) const;
      static QStringList checkRules( const QJsonObject &inputSchema, const QJsonObject &args );

      QJsonObject mDefs;
      QHash<QString, QJsonObject> mTools; //!< name -> inputSchema
  };

} // namespace ufield

#endif // UFIELD_TOOLSCHEMA_H
