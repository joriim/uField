/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// Unit tests for the sync planner: the rows of the table in ADR 0004 §3, plus
// the edge cases. Self-contained (no test framework) so it builds anywhere.

#include "syncplan.h"

#include <iostream>
#include <string>
#include <vector>

using namespace ufield::sync;

namespace
{
  int failures = 0;

  const char *name( ActionType t )
  {
    switch ( t )
    {
      case ActionType::UploadNew: return "UploadNew";
      case ActionType::Upload: return "Upload";
      case ActionType::Download: return "Download";
      case ActionType::DeleteRemote: return "DeleteRemote";
      case ActionType::DeleteLocal: return "DeleteLocal";
      case ActionType::Conflict: return "Conflict";
      case ActionType::RecordOnly: return "RecordOnly";
      case ActionType::ForgetRecord: return "ForgetRecord";
    }
    return "?";
  }

  std::string str( const std::vector<Action> &actions )
  {
    std::string out = "[";
    for ( const Action &a : actions )
    {
      out += std::string( " " ) + name( a.type ) + "(" + a.path;
      if ( !a.remoteId.empty() )
        out += ", " + a.remoteId;
      if ( !a.conflictPath.empty() )
        out += ", " + a.conflictPath;
      out += ")";
    }
    return out + " ]";
  }

  void expect( const std::string &test, const std::vector<Action> &actual, const std::vector<Action> &expected )
  {
    if ( actual == expected )
      return;
    ++failures;
    std::cerr << "FAIL " << test << "\n  expected " << str( expected ) << "\n  actual   " << str( actual ) << "\n";
  }

  void expectEq( const std::string &test, const std::string &actual, const std::string &expected )
  {
    if ( actual == expected )
      return;
    ++failures;
    std::cerr << "FAIL " << test << "\n  expected \"" << expected << "\"\n  actual   \"" << actual << "\"\n";
  }

  void expectTrue( const std::string &test, bool value )
  {
    if ( value )
      return;
    ++failures;
    std::cerr << "FAIL " << test << "\n";
  }

  const std::string kSuffix = "conflict 2026-10-21 Pixel";

  LocalFile lf( const std::string &md5 ) { return { md5, 1 }; }
  RemoteFile rf( const std::string &id, const std::string &md5 ) { return { id, md5, 1 }; }
  SyncedFile sf( const std::string &id, const std::string &md5 ) { return { id, md5, 1, 0 }; }

  void tableRows()
  {
    // local changed, remote unchanged → upload to the same Drive file
    expect( "local changed", planSync( { { "a.gpkg", lf( "L2" ) } }, { { "a.gpkg", rf( "d1", "S" ) } }, { { "a.gpkg", sf( "d1", "S" ) } }, kSuffix ),
            { { ActionType::Upload, "a.gpkg", "d1", {} } } );

    // local unchanged, remote changed → download
    expect( "remote changed", planSync( { { "a.gpkg", lf( "S" ) } }, { { "a.gpkg", rf( "d1", "R2" ) } }, { { "a.gpkg", sf( "d1", "S" ) } }, kSuffix ),
            { { ActionType::Download, "a.gpkg", "d1", {} } } );

    // both changed differently → conflict copy of the local file
    expect( "both changed", planSync( { { "data/a.gpkg", lf( "L2" ) } }, { { "data/a.gpkg", rf( "d1", "R2" ) } }, { { "data/a.gpkg", sf( "d1", "S" ) } }, kSuffix ),
            { { ActionType::Conflict, "data/a.gpkg", "d1", "data/a (conflict 2026-10-21 Pixel).gpkg" } } );

    // new local → upload as a new file
    expect( "new local", planSync( { { "media/p.jpg", lf( "L" ) } }, {}, {}, kSuffix ),
            { { ActionType::UploadNew, "media/p.jpg", {}, {} } } );

    // new remote → download
    expect( "new remote", planSync( {}, { { "media/p.jpg", rf( "d2", "R" ) } }, {}, kSuffix ),
            { { ActionType::Download, "media/p.jpg", "d2", {} } } );

    // deleted locally, unchanged in Drive → delete in Drive
    expect( "local deleted", planSync( {}, { { "x.md", rf( "d3", "S" ) } }, { { "x.md", sf( "d3", "S" ) } }, kSuffix ),
            { { ActionType::DeleteRemote, "x.md", "d3", {} } } );

    // unchanged locally, deleted in Drive → delete locally
    expect( "remote deleted", planSync( { { "x.md", lf( "S" ) } }, {}, { { "x.md", sf( "d3", "S" ) } }, kSuffix ),
            { { ActionType::DeleteLocal, "x.md", {}, {} } } );
  }

  void noDataLoss()
  {
    // deleted locally but changed in Drive → keep Drive's version
    expect( "local deleted, remote changed", planSync( {}, { { "x.md", rf( "d3", "R2" ) } }, { { "x.md", sf( "d3", "S" ) } }, kSuffix ),
            { { ActionType::Download, "x.md", "d3", {} } } );

    // deleted in Drive but changed locally → upload again as a new file
    expect( "remote deleted, local changed", planSync( { { "x.md", lf( "L2" ) } }, {}, { { "x.md", sf( "d3", "S" ) } }, kSuffix ),
            { { ActionType::UploadNew, "x.md", {}, {} } } );

    // created on both sides with different content and no record → conflict
    expect( "new on both sides", planSync( { { "n.txt", lf( "L" ) } }, { { "n.txt", rf( "d4", "R" ) } }, {}, kSuffix ),
            { { ActionType::Conflict, "n.txt", "d4", "n (conflict 2026-10-21 Pixel).txt" } } );
  }

  void sameContent()
  {
    // already in sync → nothing
    expect( "in sync", planSync( { { "a", lf( "S" ) } }, { { "a", rf( "d1", "S" ) } }, { { "a", sf( "d1", "S" ) } }, kSuffix ), {} );

    // both changed to the same content → only the record
    expect( "same change", planSync( { { "a", lf( "N" ) } }, { { "a", rf( "d1", "N" ) } }, { { "a", sf( "d1", "S" ) } }, kSuffix ),
            { { ActionType::RecordOnly, "a", "d1", {} } } );

    // same content on both sides with no record (first sync of a copied project)
    expect( "same, no record", planSync( { { "a", lf( "N" ) } }, { { "a", rf( "d1", "N" ) } }, {}, kSuffix ),
            { { ActionType::RecordOnly, "a", "d1", {} } } );

    // Drive id changed (file replaced in Drive with identical content)
    expect( "new remote id", planSync( { { "a", lf( "S" ) } }, { { "a", rf( "d9", "S" ) } }, { { "a", sf( "d1", "S" ) } }, kSuffix ),
            { { ActionType::RecordOnly, "a", "d9", {} } } );

    // gone on both sides → drop the record
    expect( "gone on both", planSync( {}, {}, { { "a", sf( "d1", "S" ) } }, kSuffix ), { { ActionType::ForgetRecord, "a", {}, {} } } );
  }

  void exclusions()
  {
    expectTrue( "wal excluded", isExcluded( "data/a.gpkg-wal" ) );
    expectTrue( "shm excluded", isExcluded( "data/a.gpkg-shm" ) );
    expectTrue( "journal excluded", isExcluded( "a.gpkg-journal" ) );
    expectTrue( "state excluded", isExcluded( ".ufield/sync.json" ) );
    expectTrue( "gpkg synced", !isExcluded( "data/a.gpkg" ) );
    expectTrue( "nested .ufield name synced", !isExcluded( "notes/.ufield/x" ) );

    expect( "excluded files ignored",
            planSync( { { "a.gpkg-wal", lf( "W" ) }, { ".ufield/sync.json", lf( "J" ) } }, { { "b.gpkg-shm", rf( "d5", "M" ) } }, {}, kSuffix ), {} );

    // Google Workspace documents have no md5Checksum and are left alone
    expect( "workspace doc ignored", planSync( {}, { { "Notes", rf( "d6", "" ) } }, {}, kSuffix ), {} );
  }

  void conflictNames()
  {
    expectEq( "with ext", conflictName( "a/b.gpkg", "c" ), "a/b (c).gpkg" );
    expectEq( "no ext", conflictName( "a/README", "c" ), "a/README (c)" );
    expectEq( "dot dir", conflictName( "a.d/README", "c" ), "a.d/README (c)" );
    expectEq( "hidden file", conflictName( ".env", "c" ), ".env (c)" );
    expectEq( "two dots", conflictName( "p.tar.gz", "c" ), "p.tar (c).gz" );

    // a conflict copy name already in use gets a number
    expect( "unique conflict name",
            planSync( { { "a.txt", lf( "L" ) }, { "a (" + kSuffix + ").txt", lf( "X" ) } }, { { "a.txt", rf( "d1", "R" ) }, { "a (" + kSuffix + ").txt", rf( "d2", "X" ) } },
                      { { "a.txt", sf( "d1", "S" ) }, { "a (" + kSuffix + ").txt", sf( "d2", "X" ) } }, kSuffix ),
            { { ActionType::Conflict, "a.txt", "d1", "a (" + kSuffix + " 2).txt" } } );
  }

  void ordering()
  {
    expect( "path order", planSync( { { "b", lf( "B" ) }, { "a", lf( "A" ) } }, { { "c", rf( "d1", "C" ) } }, {}, kSuffix ),
            { { ActionType::UploadNew, "a", {}, {} }, { ActionType::UploadNew, "b", {}, {} }, { ActionType::Download, "c", "d1", {} } } );
  }
} // namespace

int main()
{
  tableRows();
  noDataLoss();
  sameContent();
  exclusions();
  conflictNames();
  ordering();
  if ( failures )
  {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "syncplan: all tests passed\n";
  return 0;
}
