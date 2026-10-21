/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "syncplan.h"

#include <set>

namespace ufield::sync
{

  bool Action::operator==( const Action &other ) const
  {
    return type == other.type && path == other.path && remoteId == other.remoteId && conflictPath == other.conflictPath;
  }

  static bool endsWith( const std::string &s, const std::string &suffix )
  {
    return s.size() >= suffix.size() && s.compare( s.size() - suffix.size(), suffix.size(), suffix ) == 0;
  }

  bool isExcluded( const std::string &path )
  {
    if ( path.rfind( ".ufield/", 0 ) == 0 || path == ".ufield" )
      return true;
    return endsWith( path, "-wal" ) || endsWith( path, "-shm" ) || endsWith( path, "-journal" );
  }

  std::string conflictName( const std::string &path, const std::string &suffix )
  {
    const std::size_t slash = path.rfind( '/' );
    const std::size_t nameStart = slash == std::string::npos ? 0 : slash + 1;
    std::size_t dot = path.rfind( '.' );
    // No extension when there is no dot in the file name, or the name only starts with one (".env").
    if ( dot == std::string::npos || dot <= nameStart )
      dot = path.size();
    return path.substr( 0, dot ) + " (" + suffix + ")" + path.substr( dot );
  }

  namespace
  {
    // A conflict copy name that no existing or planned file uses: "name (suffix).ext",
    // then "name (suffix 2).ext", ...
    std::string uniqueConflictName( const std::string &path, const std::string &suffix, std::set<std::string> &taken )
    {
      std::string candidate = conflictName( path, suffix );
      for ( int n = 2; taken.count( candidate ); ++n )
        candidate = conflictName( path, suffix + " " + std::to_string( n ) );
      taken.insert( candidate );
      return candidate;
    }
  } // namespace

  std::vector<Action> planSync( const LocalFiles &local, const RemoteFiles &remote, const SyncedFiles &synced,
                                const std::string &conflictSuffix )
  {
    std::set<std::string> paths;
    for ( const auto &entry : local )
      paths.insert( entry.first );
    for ( const auto &entry : remote )
      if ( !entry.second.md5.empty() )
        paths.insert( entry.first );
    for ( const auto &entry : synced )
      paths.insert( entry.first );

    std::set<std::string> taken = paths;
    std::vector<Action> actions;

    for ( const std::string &path : paths )
    {
      if ( isExcluded( path ) )
        continue;

      const auto l = local.find( path );
      const auto r = remote.find( path );
      const auto s = synced.find( path );
      const bool hasL = l != local.end();
      const bool hasR = r != remote.end() && !r->second.md5.empty();
      const bool hasS = s != synced.end();

      const bool localChanged = hasL && ( !hasS || l->second.md5 != s->second.md5 );
      const bool remoteChanged = hasR && ( !hasS || r->second.md5 != s->second.md5 );

      if ( hasL && hasR )
      {
        if ( l->second.md5 == r->second.md5 )
        {
          // Same content on both sides: nothing to transfer.
          if ( !hasS || s->second.md5 != l->second.md5 || s->second.remoteId != r->second.id )
            actions.push_back( { ActionType::RecordOnly, path, r->second.id, {} } );
        }
        else if ( localChanged && remoteChanged )
          actions.push_back( { ActionType::Conflict, path, r->second.id, uniqueConflictName( path, conflictSuffix, taken ) } );
        else if ( localChanged )
          actions.push_back( { ActionType::Upload, path, r->second.id, {} } );
        else
          actions.push_back( { ActionType::Download, path, r->second.id, {} } );
      }
      else if ( hasL ) // gone or never on the remote
      {
        if ( hasS && !localChanged )
          actions.push_back( { ActionType::DeleteLocal, path, {}, {} } );
        else
          actions.push_back( { ActionType::UploadNew, path, {}, {} } ); // new, or changed after a remote delete
      }
      else if ( hasR ) // gone or never local
      {
        if ( hasS && !remoteChanged )
          actions.push_back( { ActionType::DeleteRemote, path, r->second.id, {} } );
        else
          actions.push_back( { ActionType::Download, path, r->second.id, {} } ); // new, or changed after a local delete
      }
      else
        actions.push_back( { ActionType::ForgetRecord, path, {}, {} } );
    }
    return actions;
  }

} // namespace ufield::sync
