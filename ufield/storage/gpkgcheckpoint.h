/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_GPKGCHECKPOINT_H
#define UFIELD_GPKGCHECKPOINT_H

#include <QString>
#include <QStringList>

namespace ufield
{

  /**
   * Makes every SQLite database in a project (GeoPackages and other .sqlite/.db files)
   * complete in its main file before a sync (ADR 0004 §3): a database with a non-empty
   * -wal file is checkpointed with PRAGMA wal_checkpoint(TRUNCATE), which also empties the
   * -wal, so replacing the file after a download cannot mix it with an old WAL.
   *
   * QGIS may have the database open; that is fine while it is not in a write transaction.
   * Databases that could not be checkpointed (busy) are returned in \a busy as
   * project-relative paths; the sync leaves them alone this time.
   */
  void checkpointDatabases( const QString &projectDir, QStringList &busy );

} // namespace ufield

#endif // UFIELD_GPKGCHECKPOINT_H
