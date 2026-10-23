# 0008 — Google Drive storage and sync (release 1)

- Status: proposed, 2026-10-23
- Relation: implements the Google Drive part of release 1 (ADR 0007) and the `google_drive` StorageProvider in `CLAUDE.md`. Follows ADR 0002 (credentials) and rule 8 (`drive.file` only).

## Context

Release 1 is a standalone app: no μField server. Projects must sync between the phone, Google Drive and the user's computer, where QGIS desktop and `ufield-mcp` (Claude) work on the same project folder. Constraints:

- **Scope `drive.file` only** (rule 8). It's a non-sensitive scope, so it needs only basic OAuth verification. The app can see only files it created, files the user opened with it, and files created by another client of the same Google Cloud project *(verify the last point against Google's documentation before building on it)*.
- **No Play Services** (rule 3), and Android OAuth clients can't use custom URI schemes by default (`docs/licensing.md` §1.3).
- **Tokens on the device only** (ADR 0002 §1).
- **Capture fidelity** (rule 5): media originals reach Drive byte for byte.
- The fork already contains what sync needs:
  - **geodiff** (three-way GeoPackage merge; `core/geodiffutils.*`, used by Mergin's own sync in `core/merginapi.cpp`)
  - **qtkeychain-qt6**
  - **Qt NetworkAuth**, with Qt 6.10.3 from the vcpkg baseline

## Decision

### 1. Sign-in to Drive

- `QOAuth2AuthorizationCodeFlow` with PKCE, a public client (no client secret) and `QOAuthUriSchemeReplyHandler` (Qt 6.8+), with the redirect `fi.biomitta.ufield:/oauth2redirect`. No Java OAuth library and no Play Services.
- Google Cloud project "μField":
  - an **Android** OAuth client (package `fi.biomitta.ufield` + signing certificate SHA-1), with custom URI scheme enabled in its advanced settings
  - a **Desktop** OAuth client for `ufield-mcp` (§4)
  - consent screen brand verification
- Scopes: `https://www.googleapis.com/auth/drive.file` and `openid email` (to show which account is connected).
- The refresh token goes in the Android Keystore through qtkeychain. The μField Android build turns on `USE_KEYCHAIN` (upstream builds Android without it).
- Disconnect deletes the token locally and revokes it at Google.

### 2. Layout in Drive

```
My Drive/μField/                     created by the app
  <project>/                         one folder per project, same layout as on the phone
    data.gpkg  <project>.qgz  media/  notes/  analyses/  catalog.json  catalog/  CLAUDE.md
```

- The app creates `μField/` and everything under it, so `drive.file` covers it.
- **Existing QGIS projects** that μField didn't create are invisible under `drive.file`, and there's no native Android Google Picker. They come in through `device_folder` (Android Storage Access Framework, which includes the Drive app's file provider): the app copies the project into a new μField project, then uploads it. QGIS desktop users who start a project on the computer save it into `μField/` only after μField has created the project (step "Create project on Drive" in the app); later edits to existing files are fine.
- **New files created on the computer outside μField** (a new layer file or photo copied in with Drive for desktop) aren't visible to the phone. The app's project screen says so, and offers "Add files" through the Android Storage Access Framework instead.

### 3. Sync on the phone

Per project, a local working copy is what the user edits (`StorageProvider` contract):

- `.ufield/sync.json` is the manifest: path → Drive file id, `md5Checksum`, `headRevisionId` and size at the last sync, plus the Drive changes `startPageToken`.
- `.ufield/base/` holds the last-synced copy of each diffable file (GeoPackage), exactly as Mergin keeps `.mergin/`.

**Pull**
1. Remote changes come from `changes.list` since the saved page token; there's a full folder listing as fallback.
2. For a remote change with an unchanged local file, download it and replace the local one.
3. For a GeoPackage changed on both sides, run `GeodiffUtils::rebase( base, theirs, mine, conflictfile )`. Local edits are rebased on the remote version; if geodiff reports conflicts, the conflict file is kept next to the data.
4. If the rebase fails, keep the local file as a conflicted copy (`data (conflict <device> <date>).gpkg`) and take the remote one. Nothing is overwritten silently.
5. Other files changed on both sides become conflicted copies too. Notes are append-only (new files), so they don't conflict.

**Push**
1. Before uploading a GeoPackage, run `PRAGMA wal_checkpoint(TRUNCATE)`. `-wal` and `-shm` files are never uploaded.
2. Re-check the remote `headRevisionId` just before uploading. If it changed, pull first. Drive has no conditional upload, so a short race window remains; the next sync catches it.
3. Media uploads are resumable, with an optional Wi-Fi-only setting; after upload the Drive `md5Checksum` is compared with the local file (rule 5).
4. A local deletion moves the Drive file to the Drive trash, never a permanent delete.

- **Queue:** sync runs as a persistent queue that survives restarts and offline periods. `sync_project` triggers it and reports what changed, conflicts and pending uploads.
- **Ids:** features are matched by `ufield_uuid`, never by fid (`CLAUDE.md`).

### 4. The computer side

- QGIS desktop and Claude work on the project through **Drive for desktop**. Edits to files μField created (for example `data.gpkg`) keep their Drive file id and are visible to the phone.
- **`ufield-mcp` writes new files through the Drive API**, not the synced folder, using μField's Desktop OAuth client (same Cloud project, `drive.file`). Otherwise new AI notes and STAC items, which are new files, would be invisible to the phone.
  - New option: `ufield-mcp --project <folder> --drive`. Reads still come from the local synced folder.
  - Writes are uploaded into the matching Drive folder, then appear locally through Drive for desktop.
  - The token stays in the OS keychain (Python `keyring`), never in the project folder.
- Without `--drive`, `ufield-mcp` works as today on a local folder, for projects stored only on the computer.

### 5. Tests

- **C++** (Qt Test): sync logic against an in-process fake Drive (HTTP), covering first upload, pull of a remote change, GeoPackage rebase without and with conflict, conflicted copy of a binary file, resumed media upload with checksum check, trash instead of delete, and an expired token.
- **Python**: `ufield-mcp --drive` against the same fake-Drive fixtures.
- **Manual exit check** (roadmap phase 4): a project edited offline on one phone syncs through Drive to QGIS desktop and back, with no data or metadata loss. An AI note written by Claude on the computer shows up on the phone.

## Consequences

- `ufield-android.yml`: `-DUSE_KEYCHAIN=TRUE`; check that qtkeychain's Android backend builds and stores tokens in the Keystore.
- Google Cloud setup (roadmap phase 0): Android and Desktop OAuth clients in one project, custom scheme enabled, consent screen verification.
- `schema/tools.json`: `sync_project` reports `conflicts`, `pending_uploads` and `last_sync`; `list_projects` / `open_project` mark Drive projects. A follow-up PR adds `connect_storage` (native UI flow, no secrets; from ADR 0004).
- Code goes in `ufield/storage/` (rule 2). Mergin's `core/merginapi.cpp` sync stays untouched; μField reuses `GeodiffUtils` only.
- **User-visible limits, written in the app and the docs:** projects must be created by μField, or imported through it, to sync; new files added on the computer outside μField or `ufield-mcp --drive` don't reach the phone.

## Open questions

- **First implementation step, before any sync code: a 15-minute spike** to confirm that `drive.file` access is shared by all OAuth clients of one Cloud project (§4 depends on it). Google's documentation says only "files created or opened by the app" and doesn't define "app"; I found no authoritative answer.
  1. Create a file with the Desktop client.
  2. List the folder with a second client of the same project.
  3. Also check a file copied into the folder with Drive for desktop: it is expected to be invisible.
  If client access isn't shared, fall back to `ufield-mcp` appending notes to an app-created inbox file per project, which the app splits into note files on sync.
- Google may restrict custom URI schemes further. The fallback is the Storage Access Framework route through the Drive app, which needs no OAuth but has no change notifications.
- Project size limits for mobile data; the default for automatic media sync on mobile data is off.
