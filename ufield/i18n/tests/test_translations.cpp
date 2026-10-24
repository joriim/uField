/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// The Finnish catalogue is embedded, loads for fi locales, and covers plurals.

#include <QCoreApplication>
#include <QLocale>
#include <QTest>

#include "ufieldtranslations.h"

class TestTranslations : public QObject
{
    Q_OBJECT

  private slots:
    void finnish()
    {
      QVERIFY( ufield::installTranslations( *QCoreApplication::instance(), QLocale( QLocale::Finnish, QLocale::Finland ) ) );
      QCOMPARE( QCoreApplication::translate( "MMDriveProjectsTab", "Download" ), QStringLiteral( "Lataa" ) );
      QCOMPARE( QCoreApplication::translate( "MMDriveProjectsTab", "%n file(s) changed on two devices; both versions were kept", "", 2 ),
                QStringLiteral( "2 tiedostoa muuttui kahdella laitteella; molemmat versiot säilytettiin" ) );
    }

    void noCatalogueForOtherLanguages()
    {
      QVERIFY( !ufield::installTranslations( *QCoreApplication::instance(), QLocale( QLocale::German ) ) );
    }
};

QTEST_GUILESS_MAIN( TestTranslations )
#include "test_translations.moc"
