# 0004 — User-owned data (self-hosting)

- Status: proposed
- Date: 2026-09-27
- Decider: Miiro / Biomitta Oy
- Supersedes, if accepted: parts of ADR 0002 (§1 server-held Biomitta keys, §6 Google sign-in → server session) and ADR 0003 (§1 `/agent` Claude proxy with Biomitta's key, §4 cloud speech-to-text through ufield-server). See `docs/handoff/2026-09-27-design-session.md`.

## Context

ADR 0002 and 0003 (2026-09-25) assume a ufield-server run by Biomitta that holds Biomitta's MML, CDSE, Anthropic and speech-to-text keys, signs users in with Google, and proxies Claude and transcription. Project data already stays in the user's storage in that design, but transcripts, audio, target areas and Farm Pack jobs pass through Biomitta. This ADR moves the server to the user.

The aim is that μField users host their own data. Earlier drafts assumed a Biomitta-run `ufield-server` for sign-in, Farm Pack processing, the MCP endpoint and API keys. That design would make Biomitta the GDPR controller for users' field data, locations, photos and voice. It would also make Biomitta a high-volume consumer of the MML and CDSE free services, and require user accounts (which triggers Google Play's account-deletion rule).

## Decision

**Biomitta hosts no user data and runs no service that stores it.** The user chooses where their data lives, in one of three modes. All three use the same app and the same tools.

| Mode | Where data lives | Who it suits | What runs where |
|---|---|---|---|
| **1. Phone only** | App storage on the phone; export and import as files (GeoPackage, ZIP) | Single user, trial, offline work | Everything on the phone, including the Farm Pack (section 3) |
| **2. Own cloud account** | A folder in the user's Google Drive, OneDrive, Nextcloud/WebDAV or S3-compatible storage | Farms and contractors without a server | Phone plus the user's storage; optional μField on a desktop for MCP (section 5) |
| **3. Own server** | **self-hosted ufield-server** on the user's NAS, PC, Raspberry Pi or VPS (Docker Compose) | Advisers, cooperatives, research sites, power users | Sync, catalogue, Farm Pack and MCP on the user's server |

What Biomitta still provides (none of it involves user data):

- the app on Google Play and the source on GitHub
- self-hosted ufield-server container images and install docs
- a **signed layer registry** (`layers.yaml` plus an ed25519 signature) published as a static file in GitHub releases; the app verifies the signature before using it
- one public Google OAuth client ID for the Drive connection (section 2)
- optional *paid services that don't hold data*: installing self-hosted ufield-server at a farm (user-owned hardware), support, consulting. A future Biomitta-hosted option would be a separate product with its own privacy terms.

## Consequences by component

### 1. Storage and sync

- `StorageProvider` implementations: `local`, `device_folder` (Android Storage Access Framework, which also covers USB drives and provider apps that support folder access), `google_drive`, `onedrive`, `webdav` (Nextcloud, ownCloud, most NAS), `s3` (MinIO, Hetzner, etc.) and `mergin_ce`.
- **Always edit a local working copy.** GeoPackage/SQLite files can't be safely edited directly on cloud or SAF storage. Sync copies whole files; conflicts produce a `conflict-<device>-<time>` copy, never a silent overwrite.
- **Mode 3 sync uses the upstream Mergin sync client against a user-run Mergin Maps Community Edition server** (AGPL-3.0), bundled unmodified in the self-hosted ufield-server Compose file. This reuses code the fork already contains and gives feature-level merges (geodiff), version history and multi-user projects for free. UI wording is "compatible with Mergin Maps Community Edition server", which is nominative use only.
- Backups are the user's job. The app shows the last successful sync and backup time, and the docs recommend a 3-2-1 backup.

### 2. Sign-in and credentials

- **There are no μField accounts.** "Login with Google" becomes **"Connect Google Drive"**, and it's only needed if the user picks Drive.
- Google: AppAuth with a custom-scheme redirect on Biomitta's public OAuth client ID. This is option B in `docs/licensing.md` §1.3 and needs "custom URI scheme" enabled in the client's advanced settings. A native-app client ID isn't a secret. Tokens stay in the phone's Android Keystore-backed storage and never reach Biomitta. If Google disables custom schemes entirely, the fallback is the SAF `device_folder` route through the Drive app, or a stateless token relay.
- OneDrive: AppAuth or MSAL (MIT) with a Microsoft public client and `msauth://` redirect.
- WebDAV, S3 and Mergin CE: the user's own URL and credentials, kept in the Keystore.
- Credentials are entered **only in the UI**, never through a tool call, so an LLM never sees secrets. Tools can see *status* only (`get_setup_status`).

### 3. External data keys and the Farm Pack

- Each user or server uses **its own free MML API key and its own CDSE account**. Guided setup links to both sign-ups. Keyless sources (Ruokavirasto, GTK, SYKE, Metsäkeskus, FMI) work with no setup.
- This fits both providers' terms better than one central Biomitta key: MML's open interface is meant for small-scale use, and CDSE quotas apply per user. Genuine separate users each having an account isn't the "multiple accounts to get round limits" that CDSE forbids.
- **The Farm Pack runs wherever the user's keys are.** It is implemented once in C++ in `ufield/core` using GDAL, which the app already bundles.
  - Phone (Modes 1–2): WCS tiles are fetched and mosaicked on the phone. Sentinel-2 processing runs on CDSE through openEO, so the phone only downloads results. It runs as a foreground job with a notification, on Wi-Fi by default. Suggested limit on the phone is about 1,000 ha.
  - Server (Mode 3): the same code in a headless build.
- `request_farm_pack` gets `run_on: auto | device | server`.

### 4. self-hosted ufield-server

- **A headless build of the same GPL core** (`ufield/core` plus ToolDispatcher) with small adapters: MCP over Streamable HTTP, a STAC catalogue, and a job runner. Shipped with Mergin CE (unmodified) and optionally Ollama/llama.cpp and a Whisper server.
- **This settles the server-licence question:** the server contains GPL core code, so it's GPL-3.0. Container images must come with their Corresponding Source (a GitHub tag and the Dockerfile; distro packages are covered by the distro's own source repositories).
- Security defaults: bound to the LAN only. Remote access is opt-in through Tailscale or a Cloudflare Tunnel. MCP tokens are read-only by default; write scopes are granted per project.

### 5. MCP and the voice agent

- **Mode 3:** the MCP endpoint is on self-hosted ufield-server.
- **Modes 1–2:** upstream Mergin mobile also builds for Windows and Linux. The **desktop build of μField exposes MCP over stdio** so Claude Desktop and similar clients can work on the same synced project folder with no server.
- The phone app's voice agent calls ToolDispatcher directly, with no MCP hop.
- Speech-to-text: whisper.cpp on the phone (MIT), or the user's Whisper server.
- LLM: the user configures an OpenAI-compatible endpoint (their own Ollama or llama.cpp server) or enters their own API key for a hosted model. **Biomitta holds no keys and sees no prompts.**
- Write tools always ask for confirmation in the UI. Small local models are weaker at Finnish and at tool calling, so treat voice as an assistant, not an autopilot.

### 6. Paid imagery

- The user buys directly (their own Planet or UP42 account) and is the licensee. That removes the reseller problem in `docs/licensing.md`. Biomitta can help as a contractor.

## Compliance effects (update `docs/licensing.md`)

- **GDPR:** Biomitta is **neither controller nor processor** of field data. The user (farm, contractor, adviser) is the controller of their own data; purely personal use is exempt. Biomitta still needs a privacy policy that explains what the app sends where. That covers the Google/Microsoft OAuth consent screens and the external data sources, which receive the user's IP address and requested area.
- **Google Play Data safety:** Play defines "collection" as any data sent off the device, with no exemption for the user's own server. Declare the data types the app sends (location, photos, video, audio, files) with the note "sent only to the storage the user selects". Mark them as not shared, since each transfer is user-initiated.
- **Play account deletion rule:** not applicable, because there are no developer-side accounts. It would apply to any future Biomitta-hosted service.
- **Parcel privacy:** the risk is lower without a central service, but the app is still Biomitta's product and MCP could bulk-query WFS. Keep rule 10's limits.

## Risks

- **Setup friction:** own keys, own storage and own server. Mitigations: everything keyless works on first launch, guided key setup, a "μField Box" install service.
- **Support load** from varied self-hosted setups. Support only the documented Compose stack.
- **Google OAuth policy** could tighten further. The SAF fallback keeps Drive usable.
- **Heavy future products** (orthomosaic, 3DGS, super-resolution) need a GPU, so they run on the user's server, or later through an opt-in processing service that receives only the inputs the user picks. In that case Biomitta becomes a processor for that job, with a data processing agreement.

## MVP scope change

- **In:** Modes 1 and 2 (local, SAF, Google Drive, WebDAV), Mode 3 using Mergin CE plus a minimal self-hosted ufield-server (catalogue, Farm Pack, MCP), desktop build with MCP over stdio, the signed registry.
- **Later:** OneDrive, S3, the server-side speech-to-text/LLM bundle, peer-to-peer sharing (ADR 0005).
