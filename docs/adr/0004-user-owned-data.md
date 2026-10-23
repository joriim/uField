# 0004 — User-owned data: Biomitta service for phone-only users, self-hosted servers for everyone else

- Status: accepted (decision B), 2026-09-27
- Decider: Miiro / Biomitta Oy
- Relation to earlier ADRs: **keeps** ADR 0002 and ADR 0003 as the design of the *Biomitta service* profile, and **adds** a *self-hosted* profile of the same ufield-server. See `docs/handoff/2026-09-27-design-session.md`.

## Context

ADR 0002 and 0003 (2026-09-25) describe a ufield-server run by Biomitta: it holds Biomitta's MML, CDSE, Anthropic and speech-to-text keys, signs users in with Google, proxies Claude (`/agent`) and transcription, and builds Farm Packs. Project data already stays in the user's own storage in that design.

The owner's aim (2026-09-27) is that users own and host their data, and that advisory firms and modern farms run their own server. Phone-only users should still get the full app without running anything.

## Decision

**One ufield-server codebase, three ways to use it.** The app connects to one server per account: the Biomitta service, or the user's own server. Basic field collection works with no server at all.

| | Who it suits | Where project data lives | Where server features run (Farm Pack, voice, MCP, catalogue) | Keys (MML, CDSE, LLM, STT) |
|---|---|---|---|---|
| **1. Phone only + Biomitta service** | Single farmers, trials, most phone users | On the phone, or in the user's own Google Drive / WebDAV folder | **Biomitta's ufield-server** (profile `service`), as in ADR 0002/0003 | Biomitta's, on the service |
| **2. Own wired server** | Advisory firms, modern farms, research sites | On the user's server (NAS) | The user's ufield-server on their own hardware over fibre (profile `self-hosted`, ADR 0006) | The user's own |
| **3. Own virtual server** | Firms without on-site hardware, multi-site operations | On the user's server (VM storage / object storage) | The user's ufield-server on a VPS or cloud VM (profile `self-hosted`, ADR 0006 §6) | The user's own |

- **Without any server:** collect observations, photos and video, view open layers, sync to the user's storage. Server features (Farm Pack, voice notes and voice agent, remote MCP) need the service or an own server.
- **Switching is cheap:** project data is always in the user's own storage or own server, never locked in the Biomitta service. A user who outgrows the service points the app at their own server and keeps their projects.
- **Self-hosted means nothing reaches Biomitta:** no account, no telemetry, no processing. Biomitta ships the images, the signed layer registry and updates.

What Biomitta provides for all users:

- the app on Google Play and the source on GitHub
- ufield-server container images (both profiles from one codebase) and install docs
- a **signed layer registry** (`layers.yaml` plus an ed25519 signature), published as a static file in GitHub releases; the app verifies the signature before use
- the Biomitta service (profile `service`) for phone-only users
- paid services: installing and maintaining own servers ("μField Box" on site, or a managed VM in the customer's own cloud account), support, consulting

## Consequences by component

### 1. Storage and sync

- `StorageProvider` implementations:
  - `local`
  - `device_folder` (Android Storage Access Framework: USB drives and provider apps that allow folder access)
  - `google_drive`
  - `webdav` (Nextcloud, ownCloud, most NAS)
  - `mergin_ce` (the user's own server)
  - later: `onedrive`, `s3`
- **Always edit a local working copy.** GeoPackage/SQLite can't be edited safely on cloud or SAF storage. Sync copies whole files; conflicts produce a `conflict-<device>-<time>` copy, never a silent overwrite.
- **Own servers sync with the upstream Mergin sync client against Mergin Maps Community Edition** (AGPL-3.0), bundled unmodified in the self-hosted Compose file. That gives feature-level merges (geodiff), version history and multi-user projects. UI wording: "compatible with Mergin Maps Community Edition server" (nominative use).
- **The Biomitta service never stores project data** (ADR 0003's constraint stays). It works on the inputs a request needs and returns results to the user's storage.

### 2. Sign-in and credentials

- **Biomitta service:**
  - The user signs in with Google and gets a server session (ADR 0002 §6).
  - Android no longer allows custom-scheme redirects for new OAuth clients by default, so sign-in uses a **Google web OAuth client through ufield-server with an https redirect and an App Link back to the app** (`docs/licensing.md` §1.3 option A). The server only needs the identity (`openid email`).
  - "Delete account" is in the app and on a web page (Play requirement).
- **Own server:** the server authenticates its own users, either local accounts or the organisation's OIDC provider (Google Workspace, Entra ID, Keycloak). No Biomitta account exists.
- **Google Drive connection (both):** AppAuth on the device with Biomitta's public native client ID (custom scheme enabled in the client settings; `docs/licensing.md` §1.3 option B). **Drive tokens stay on the phone and never reach any server** (ADR 0002 §6). If Google removes custom schemes, the fallback is the SAF route through the Drive app.
- All other ADR 0002 rules apply to both profiles: secrets entered only in native UI, never in tool inputs; HTTP Basic for MML; never keys in URLs; QGIS auth DB and qtkeychain on the device.

### 3. External data keys and the Farm Pack

- **Biomitta service:** Biomitta's MML key and CDSE client in the service environment (ADR 0002 §1). MML's open interface isn't meant for high-volume services, so plan the **MML contract service** and a **commercial CDSE quota** once usage grows, with per-user rate and cost limits (ADR 0003).
- **Own server:** the organisation's own free MML key and CDSE account in *its* server environment (same `auth` section in `config/layers.yaml`, different values). Each organisation is a genuine separate user, which fits both services' terms.
- The Farm Pack runs on whichever server the app is connected to. It's the same code in both profiles, so no on-device Farm Pack is needed.

### 4. ufield-server profiles

| | `service` (Biomitta) | `self-hosted` (wired or virtual) |
|---|---|---|
| Users | Many individuals; Google sign-in; accounts and deletion | The organisation's users; local accounts or organisation OIDC |
| Tenants | One per user | One per client farm (ADR 0006 §4) |
| Sync | None; data is in the user's storage | Mergin CE per tenant |
| Voice agent | `/agent` → Claude with Biomitta's key (ADR 0003) | The organisation's LLM: local on its GPU (OpenAI-compatible API) or its own API key, e.g. Claude |
| Speech-to-text | Cloud STT with Biomitta's key (ADR 0003 §4) | Whisper on the organisation's GPU, or its own cloud STT key |
| Remote MCP for Claude | On the service (Google sign-in) | On the own server; exposed through a tunnel only if the organisation opts in |
| GPU products | Not offered at first | Pluggable workers (ADR 0006 §5) |
| Operated by | Biomitta, in an EU region | The organisation (or Biomitta under a support contract) |

- Both profiles are the same **GPL-3.0** code (a headless build of the GPL core), so container images come with their Corresponding Source.
- Profile-specific code lives behind configuration, not forks: `UFIELD_PROFILE=service|self-hosted`.

### 5. MCP and the voice agent

- The in-app voice agent calls the connected server's agent endpoint. Tools always execute on the phone through ToolDispatcher (ADR 0003).
- `ufield-mcp` local stdio mode for Claude Desktop / Claude Code on a project folder works with either profile (ADR 0003).
- Write tools always ask for confirmation in the UI. Local models on own servers are weaker at Finnish and at tool calling, so organisations can point the agent at a frontier model with their own key instead.

### 6. Paid imagery

- Own servers: the organisation buys directly (its own Planet or UP42 account) and is the licensee.
- Biomitta service: no paid imagery until Biomitta has partner or reseller terms (`docs/licensing.md` §2).

## Compliance effects (see `docs/licensing.md`)

- **Biomitta service:** Biomitta is the controller for service accounts and for the processing it does (sign-in, Farm Pack jobs, transcripts and audio, agent requests).
  - Needed: a privacy policy; a record of processing activities; data processing agreements with Anthropic, the STT provider and the hosting provider (EU region).
  - Data minimisation: no project data stored; audio deleted after transcription; transcripts and agent context not kept beyond the request; Farm Pack outputs deleted after delivery or after a short retention period.
  - Account deletion in the app and on the web.
- **Own servers:** Biomitta receives nothing. The organisation is the controller, or the farm's processor when an advisory firm works for it (ADR 0006 §4). Its hosting provider is its processor.
- **Play Data safety:** declare for the service profile (account e-mail, audio, location and files sent for processing; Anthropic and the STT provider as processors). Data sent to a user's own server also counts as "collected" by Play's definition, so declare it with "sent only to the server the user selects".
- **Parcel privacy** limits apply in both profiles.

## Risks

- **Two profiles to test.** Mitigation: one codebase, profile by configuration, both in CI.
- **Service costs** (Claude, STT, MML contract, CDSE quota). Mitigation: per-user limits; a paid plan for heavy users; heavy users move to their own server.
- **Self-hosting support load.** Support only the documented Compose stack and the documented VM images.
- **Google OAuth policy** could tighten further. Mitigation: server-based sign-in for the service; SAF fallback for Drive.

## MVP scope

- **In:**
  - basic collection with no server
  - Biomitta service profile (ADR 0002/0003 as written): Google sign-in, Farm Pack, voice notes, voice agent, remote MCP, account deletion
  - self-hosted profile with Compose `core` + `ai` + `ops`: Mergin CE sync, tenants, local/OIDC users; documented for a wired NAS + GPU and for a virtual server
  - storage `local`, `device_folder`, `google_drive`, `webdav`, `mergin_ce`
  - signed layer registry
- **Later:** OneDrive, S3, GPU product workers (orthomosaic first), peer-to-peer sharing (ADR 0005), paid imagery.
