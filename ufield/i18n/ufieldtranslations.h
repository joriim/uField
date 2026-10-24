/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_UFIELDTRANSLATIONS_H
#define UFIELD_UFIELDTRANSLATIONS_H

class QCoreApplication;
class QLocale;

namespace ufield
{
  //! Installs μField's translations for \a locale on \a app (after upstream's, so they win). Returns false if there are none.
  bool installTranslations( QCoreApplication &app, const QLocale &locale );
} // namespace ufield

#endif // UFIELD_UFIELDTRANSLATIONS_H
