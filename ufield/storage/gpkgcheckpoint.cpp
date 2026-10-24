/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "gpkgcheckpoint.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <sqlite3.h>

#include <cstdlib>

namespace ufield
{

  namespace
  {
    const int kBusyTimeoutMs = 3000;

    // True when the main file now holds everything: the WAL was checkpointed and truncated.
    bool checkpoint( const QString &dbPath )
    {
      sqlite3 *db = nullptr;
      if ( sqlite3_open_v2( QFile::encodeName( dbPath ).constData(), &db, SQLITE_OPEN_READWRITE, nullptr ) != SQLITE_OK )
      {
        sqlite3_close( db );
        return false;
      }
      sqlite3_busy_timeout( db, kBusyTimeoutMs );
      // As SQL, not sqlite3_wal_checkpoint_v2(): a fresh connection opens the WAL only when it
      // reads the database, and the C call would report "not in WAL mode" and do nothing.
      // TRUNCATE blocks new writers, waits for readers, copies every frame and empties the -wal.
      // The row is (busy, frames in the log, frames checkpointed).
      int row[3] = { 1, -1, -2 };
      const int rc = sqlite3_exec( db, "PRAGMA wal_checkpoint(TRUNCATE)", []( void *out, int columns, char **values, char ** ) {
        for ( int i = 0; i < columns && i < 3; ++i )
          static_cast<int *>( out )[i] = values[i] ? atoi( values[i] ) : -1;
        return 0; }, row, nullptr );
      sqlite3_close( db );
      if ( rc == SQLITE_NOTADB )
        return true; // not an SQLite file, despite the -wal next to it: nothing to protect
      return rc == SQLITE_OK && row[0] == 0 && row[1] == row[2];
    }
  } // namespace

  void checkpointDatabases( const QString &projectDir, QStringList &busy )
  {
    busy.clear();
    const QDir root( projectDir );
    QDirIterator it( projectDir, { QStringLiteral( "*-wal" ) }, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories );
    while ( it.hasNext() )
    {
      const QFileInfo wal( it.next() );
      const QString dbPath = wal.filePath().chopped( 4 );
      if ( !QFileInfo( dbPath ).isFile() )
        continue; // a stray -wal without its database
      if ( wal.size() == 0 )
        continue; // nothing pending
      if ( !checkpoint( dbPath ) )
        busy << root.relativeFilePath( dbPath );
    }
    busy.sort();
  }

} // namespace ufield
