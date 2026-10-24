/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

package fi.biomitta.ufield;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.IntentSender;
import android.os.Build;
import android.os.Bundle;

/**
 * Transparent activity that shows Google's account and consent screen for Drive
 * access and hands the result to DriveAuth. Keeps MMActivity unchanged.
 */
public class DriveAuthActivity extends Activity
{
    static final String EXTRA_PENDING_INTENT = "fi.biomitta.ufield.DRIVE_AUTH_PENDING_INTENT";
    private static final int REQUEST_CONSENT = 4801;
    private static final String STATE_STARTED = "started";

    private boolean mStarted = false;
    private boolean mReported = false;

    @Override
    protected void onCreate( Bundle savedInstanceState )
    {
        super.onCreate( savedInstanceState );
        mStarted = savedInstanceState != null && savedInstanceState.getBoolean( STATE_STARTED, false );
        if ( mStarted )
            return; // recreated (e.g. rotation) while Google's screen is open; wait for its result

        PendingIntent pendingIntent;
        if ( Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU )
            pendingIntent = getIntent().getParcelableExtra( EXTRA_PENDING_INTENT, PendingIntent.class );
        else
            pendingIntent = getIntent().getParcelableExtra( EXTRA_PENDING_INTENT );
        if ( pendingIntent == null )
        {
            report( null, false );
            return;
        }
        try
        {
            startIntentSenderForResult( pendingIntent.getIntentSender(), REQUEST_CONSENT, null, 0, 0, 0 );
            mStarted = true;
        }
        catch ( IntentSender.SendIntentException e )
        {
            report( null, false );
        }
    }

    @Override
    protected void onSaveInstanceState( Bundle outState )
    {
        super.onSaveInstanceState( outState );
        outState.putBoolean( STATE_STARTED, mStarted );
    }

    @Override
    protected void onActivityResult( int requestCode, int resultCode, Intent data )
    {
        super.onActivityResult( requestCode, resultCode, data );
        if ( requestCode == REQUEST_CONSENT )
            report( data, resultCode == RESULT_OK );
    }

    @Override
    protected void onDestroy()
    {
        // Closed without a result (e.g. the system killed it): unblock the waiting sync.
        if ( isFinishing() && !mReported )
            DriveAuth.consentFinished( this, null, false );
        super.onDestroy();
    }

    private void report( Intent data, boolean ok )
    {
        mReported = true;
        DriveAuth.consentFinished( getApplicationContext(), data, ok );
        finish();
    }
}
