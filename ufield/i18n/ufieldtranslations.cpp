/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "ufieldtranslations.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>

namespace ufield
{
  bool installTranslations( QCoreApplication &app, const QLocale &locale )
  {
    auto *translator = new QTranslator( &app );
    if ( !translator->load( locale, QStringLiteral( "ufield" ), QStringLiteral( "_" ), QStringLiteral( ":/ufield/i18n" ) ) )
    {
      delete translator;
      return false;
    }
    return app.installTranslator( translator );
  }
} // namespace ufield
