/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

package fi.biomitta.ufield;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Looper;

import com.google.android.gms.auth.GoogleAuthUtil;
import com.google.android.gms.auth.api.identity.AuthorizationRequest;
import com.google.android.gms.auth.api.identity.AuthorizationResult;
import com.google.android.gms.auth.api.identity.Identity;
import com.google.android.gms.common.api.ApiException;
import com.google.android.gms.common.api.Scope;
import com.google.android.gms.tasks.Tasks;

import java.util.Collections;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Google Drive access tokens for Drive sync (ADR 0004 §2), from Play services'
 * AuthorizationClient with the drive.file scope only (CLAUDE.md rule 8).
 *
 * Play services keeps the grant and refreshes tokens; this class stores nothing.
 * Tokens are returned to the caller (the sync worker thread) and never logged.
 */
public final class DriveAuth
{
    public static final String DRIVE_FILE_SCOPE = "https://www.googleapis.com/auth/drive.file";

    private static final long AUTHORIZE_TIMEOUT_S = 60;
    private static final long CONSENT_TIMEOUT_S = 600;

    private static final Object sLock = new Object();
    private static CountDownLatch sConsentDone;
    private static String[] sConsentResult;

    private DriveAuth() {}

    private static AuthorizationRequest request()
    {
        return AuthorizationRequest.builder()
            .setRequestedScopes( Collections.singletonList( new Scope( DRIVE_FILE_SCOPE ) ) )
            .build();
    }

    /**
     * Blocks until a token is available; never call on the main thread.
     *
     * @param staleToken  a token Drive rejected (401), cleared from Play services' cache first; or null
     * @param interactive whether the consent screen may be shown when the user has not granted access yet
     * @return { token, error }: exactly one is non-null. Errors never contain the token.
     */
    public static String[] accessToken( Context context, String staleToken, boolean interactive )
    {
        if ( Looper.myLooper() == Looper.getMainLooper() )
            return new String[] { null, "DriveAuth.accessToken called on the main thread" };
        try
        {
            if ( staleToken != null && !staleToken.isEmpty() )
                GoogleAuthUtil.clearToken( context, staleToken );

            AuthorizationResult result = Tasks.await( Identity.getAuthorizationClient( context ).authorize( request() ),
                                                      AUTHORIZE_TIMEOUT_S, TimeUnit.SECONDS );
            if ( !result.hasResolution() )
                return tokenOf( result );
            if ( !interactive )
                return new String[] { null, "consent_required" };
            return askConsent( context, result.getPendingIntent() );
        }
        catch ( Exception e )
        {
            return new String[] { null, describe( e ) };
        }
    }

    private static String[] askConsent( Context context, PendingIntent pendingIntent ) throws InterruptedException
    {
        CountDownLatch done;
        synchronized ( sLock )
        {
            if ( sConsentDone != null )
                return new String[] { null, "Google sign-in is already in progress" };
            done = new CountDownLatch( 1 );
            sConsentDone = done;
            sConsentResult = null;
        }
        Intent intent = new Intent( context, DriveAuthActivity.class );
        intent.putExtra( DriveAuthActivity.EXTRA_PENDING_INTENT, pendingIntent );
        intent.addFlags( Intent.FLAG_ACTIVITY_NEW_TASK );
        context.startActivity( intent );

        boolean finished = done.await( CONSENT_TIMEOUT_S, TimeUnit.SECONDS );
        synchronized ( sLock )
        {
            String[] result = sConsentResult;
            sConsentDone = null;
            sConsentResult = null;
            if ( !finished || result == null )
                return new String[] { null, "Google sign-in timed out" };
            return result;
        }
    }

    /** Called by DriveAuthActivity when the consent screen closes. */
    static void consentFinished( Context context, Intent data, boolean ok )
    {
        String[] result;
        if ( !ok || data == null )
            result = new String[] { null, "cancelled" };
        else
        {
            try
            {
                result = tokenOf( Identity.getAuthorizationClient( context ).getAuthorizationResultFromIntent( data ) );
            }
            catch ( Exception e )
            {
                result = new String[] { null, describe( e ) };
            }
        }
        synchronized ( sLock )
        {
            sConsentResult = result;
            if ( sConsentDone != null )
                sConsentDone.countDown();
        }
    }

    private static String[] tokenOf( AuthorizationResult result )
    {
        String token = result.getAccessToken();
        if ( token == null || token.isEmpty() )
            return new String[] { null, "Google returned no access token" };
        if ( !result.getGrantedScopes().contains( DRIVE_FILE_SCOPE ) )
            return new String[] { null, "Google Drive access was not granted" };
        return new String[] { token, null };
    }

    // Status codes and exception types only: messages from the network stack can contain URLs.
    private static String describe( Exception e )
    {
        Throwable cause = e.getCause() != null ? e.getCause() : e;
        if ( cause instanceof ApiException )
            return "Google sign-in failed (status " + ( ( ApiException ) cause ).getStatusCode() + ")";
        return "Google sign-in failed (" + cause.getClass().getSimpleName() + ")";
    }
}
