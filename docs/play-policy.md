# Google Play declarations — μField release 1

Status: draft, 2026-10-23. Release 1 is the standalone app (ADR 0007): no μField server, no μField account, no analytics. Play forms and rules change often; check every *(verify)* item in Play Console when filling the forms. Have a lawyer review this file and `docs/privacy-policy.md` before the first production release.

## Where data goes in release 1

| Data | Stays on the phone | Leaves the phone, to | When |
|---|---|---|---|
| Observations, attributes, notes | yes | the user's **own Google Drive** (`drive.file`) | only if the user connects Drive and syncs |
| Photos and videos (originals with EXIF and GNSS metadata) | yes | the user's own Google Drive | same |
| Voice-note audio | yes | the user's own Google Drive | same; no transcription in release 1 |
| Precise location | yes, in features and media metadata | the user's own Google Drive; **map services** receive the map area being viewed (WMS/WMTS/WFS requests) | while the map is open |
| IP address | — | Google (Drive, OAuth) and the open map services the user enables (MML, Ruokavirasto, GTK, Syke, Metsäkeskus, Luke, FMI) | each request |
| Google account e-mail | yes, to show which account is connected | Google (OAuth) | when connecting Drive |
| MML API key (user's own, optional) | yes, Android Keystore | Maanmittauslaitos, in HTTP Basic auth | MML layer requests |

**Biomitta receives nothing** in release 1: no account, no telemetry, no crash reporting (`CLAUDE.md` rule 3). `ufield-mcp` runs on the user's own computer, and what Claude sees there is under the user's own agreement with Anthropic.

## Data safety form

Play defines "collected" as any transmission off the device, including to the user's own storage, so declare conservatively:

| Data type (Play category) | Collected | Shared | Optional | Purpose |
|---|---|---|---|---|
| Precise location | Yes | No | Yes (the app works without location, but recording at the current position needs it) | App functionality |
| Photos | Yes | No | Yes | App functionality |
| Videos | Yes | No | Yes | App functionality |
| Voice or sound recordings | Yes | No | Yes | App functionality |
| Files and docs (project files) | Yes | No | Yes | App functionality |
| Email address | Yes | No | Yes (only with Drive) | Account management (shows the connected Google account) |

- **Shared: No.** Every transfer goes to storage the user chose or to a service the user enabled, by the user's own action, which falls under Play's user-initiated transfer exception *(verify the wording in the form)*.
- **Encrypted in transit:** yes (HTTPS only).
- **Deletion:** data is in the user's storage and on the phone, not with the developer. Uninstalling removes the local copy, and the user deletes Drive data in Drive. The form answer: no developer-held data to delete.
- **Account deletion requirement:** not applicable, since there are no μField accounts *(verify; recheck before release 2)*.
- **Independent security review:** no.

## Permissions

| Permission | Why | Play declaration |
|---|---|---|
| `ACCESS_FINE_LOCATION` | Record observations at the GNSS position; show position on the map | Location permission form: foreground use |
| `FOREGROUND_SERVICE` + `FOREGROUND_SERVICE_LOCATION`, service type `location` | Position tracking (upstream feature): records a track while the user walks a field with the screen off | **Foreground service declaration** with a short video *(verify)*. No `ACCESS_BACKGROUND_LOCATION`: tracking only runs while the user started it and the notification shows |
| `CAMERA` | Photos and video of observations | none beyond the Data safety form |
| `RECORD_AUDIO` (added with voice notes) | Voice notes | none beyond Data safety; ask only when voice is first used |
| `ACCESS_MEDIA_LOCATION` | Keep the location in EXIF when attaching existing photos | none *(verify)* |
| `READ_EXTERNAL_STORAGE` (max SDK 32) | Gallery on Android ≤ 12 only; newer versions use the photo picker | Photo and video permissions policy: picker on 13+ *(verify)* |
| `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT` | External GNSS / RTK receivers | Nearby devices; no location derived from Bluetooth scans; consider `neverForLocation` *(verify that it doesn't break receiver discovery)* |
| `HIGH_SAMPLING_RATE_SENSORS` | Compass and orientation at capture (rule 5) | none |
| `POST_NOTIFICATIONS`, `VIBRATE` | Tracking and sync notifications | none |

## Other forms

- **Target audience:** adults, professional and farm use; not directed at children.
- **Content rating:** utility or productivity; no user-generated content shared with other users in release 1.
- **Ads:** none.
- **App access for review:** no login is needed. Provide a sample project (download link or bundled demo) so reviewers can see the map, forms and capture.
- **AI-generated content:** not applicable in release 1; the app itself calls no model *(recheck for release 2's voice agent)*.
- **Financial features, health:** none.

## To do before the first internal-track upload

- [ ] Privacy policy published at a stable URL (`docs/privacy-policy.md` → `ufield.biomitta.fi/privacy`).
- [ ] Foreground-service (location) declaration video recorded from a μField build.
- [ ] Decide on `neverForLocation` for Bluetooth after testing with an external GNSS receiver.
- [ ] Demo project for reviewers.
