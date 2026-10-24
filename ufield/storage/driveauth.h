/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_DRIVEAUTH_H
#define UFIELD_DRIVEAUTH_H

#include "googledriveprovider.h"

namespace ufield
{

  /**
   * Drive access tokens from Play services through fi.biomitta.ufield.DriveAuth (ADR 0004 §2).
   * Call the returned provider on the sync worker thread only; it blocks, and with
   * \a interactive it may show Google's consent screen and wait for the user.
   *
   * The last token is kept in the provider (memory only) so a 401 can clear it from
   * Play services' cache before asking for a new one. On other platforms the provider
   * always fails with "Google Drive sign-in is only available on Android".
   */
  GoogleDriveProvider::TokenProvider driveTokenProvider( bool interactive );

  //! Error text DriveAuth returns when access was never granted and \a interactive was false.
  inline constexpr const char *kDriveConsentRequired = "consent_required";

} // namespace ufield

#endif // UFIELD_DRIVEAUTH_H
