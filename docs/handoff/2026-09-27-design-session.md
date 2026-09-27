# Handoff: design session 2026-09-27 (claude.ai) → repo

This PR brings four documents from a design session held outside the repo. They are **additive**: no existing file is changed. Several of them conflict with ADR 0002, ADR 0003, `CLAUDE.md` and `docs/roadmap.md`. This brief lists every place that needs reconciling, in order.

## Added in this PR

| File | What it is |
|---|---|
| `docs/licensing.md` | Licence and compliance analysis: code (Mergin fork, Qt, QGIS, SDKs, trademarks), data sources, GDPR, Google/Play, user obligations, peer-to-peer sharing, on-prem servers |
| `docs/adr/0004-user-owned-data.md` | **Biomitta hosts no user data.** Three modes: phone only, the user's own cloud account, a self-hosted ufield-server. Users bring their own MML key, CDSE account and LLM/STT |
| `docs/adr/0005-p2p-public-layers.md` | Peer-to-peer sharing of openly licensed public layers (post-MVP): what may be shared, location privacy, staged transports |
| `docs/adr/0006-on-prem-server.md` | Primary professional deployment: advisory firms and modern farms run ufield-server on their own NAS + GPU over fibre; tenants; pluggable GPU workers; worker/model licence gate |

## Decision needed from the owner first

The owner's stated aim (2026-09-27): **users self-host their data; advisory firms and modern farms run their own server on a local NAS + GPU over fibre.**

- **A. Accept ADR 0004 fully:** Biomitta runs no service. Phone-only users bring their own keys and LLM endpoint, and professionals run ufield-server themselves.
- **B. Hybrid:** the same ufield-server code, self-hosted by professionals, *plus* an optional Biomitta-run instance for phone-only users. This keeps ADR 0002/0003's convenience (Biomitta keys, Claude proxy, cloud STT) for those users, but Biomitta then processes their transcripts, audio and jobs, and becomes a GDPR processor/controller with accounts, account deletion and data processing agreements.

The rest of this brief assumes **A**. Under B, apply the same changes but keep ADR 0002/0003's hosted path as a second deployment profile.

## Facts to fix regardless of the decision

1. **Upstream licence.** Mergin Maps mobile source headers are **GPL-2.0-or-later** (the licence file is GPLv2). The distributed app must be GPL-3.0 because Qt (LGPLv3) and Apache-2.0 dependencies (OpenSSL 3, zxing-cpp) are incompatible with GPLv2-only. `CLAUDE.md` rule 3 currently says the upstream is GPL-3.0. Fix it; new app files: `SPDX-License-Identifier: GPL-2.0-or-later` (lets fixes go upstream). Details: `docs/licensing.md` §1.1.
2. **Google sign-in on Android.** Since October 2023, new Android OAuth clients can't use custom-URI-scheme redirects by default, and Google points developers to its Play Services SDK (proprietary; a licence risk in a GPL app we don't fully own). Options and the choice: `docs/licensing.md` §1.3.
3. **GTK "basic licence" layers** forbid commercial and public-service use. Only GTK open-licence layers (CC BY 4.0 since 2025-12-01) may be in the registry. The 2023 peatland layers' licence is unverified. `docs/licensing.md` §2.
4. **Parcel data privacy.** In 2019 Ruokavirasto shut Peltolohkot.fi down over parcel data; it reopened after location search was removed. The open WFS has parcel ID, crop and organic flag, but no farmer identity. Limit parcel queries and Farm Packs to the user's confirmed parcels; no bulk export. `docs/licensing.md` §3.1.
5. **Play account deletion** applies only if the app creates developer-side accounts. It's not needed under decision A.
6. **Play Data safety** counts any data sent off the device as "collected", even to the user's own server. `docs/licensing.md` §3.4.
7. **Mergin Maps CE** has one workspace (multiple workspaces are Enterprise-only), which is why ADR 0006 uses one CE instance per tenant.

## Reconciliation tasks (for Claude Code, in this order)

Follow the repo's own conventions: `scripts/format_schema` and `scripts/validate_schema` after schema/registry edits; cross-field rules in `x-ufield-rules`, **never** top-level `oneOf`; schema test cases in `schema/tests/cases.json`; one PR per step.

1. **`CLAUDE.md`**
   - Rule 3: licensing fact above.
   - "What this is": add the user-owned-data paragraph (ADR 0004, 0005, 0006).
   - Architecture: ufield-server is self-hosted (NAS storage + GPU compute, Compose profiles `core`/`ai`/`products`/`ops`/`sharing`, tenants, WireGuard mesh). The voice agent calls the user's LLM endpoint; no Biomitta `/agent`.
   - New rules, adapted from the session (names already `ufield`):
     - **No proprietary SDKs** in the default build (CI checks the APK).
     - **Data licences and parcel privacy** (registry allow-list; automatic attribution; parcel limits).
     - **Nothing reaches Biomitta** (no hosted user data, no telemetry by default, no Biomitta-held keys).
     - **Sharing is explicit** (P2P off by default; only `p2p_shareable` inputs).
     - **Worker and model licences** (commercially usable only; no Inria 3DGS, DUSt3R/MASt3R, NC VGGT weights; licence in STAC provenance).
     - **Tenant isolation** (one Mergin CE instance, catalogue and storage per client farm; tokens scoped to one tenant).
   - MVP scope: storage `local`, `device_folder`, `google_drive`, `webdav`, `mergin_ce`; ufield-server profiles `core` + `ai` + `ops` with tenants; desktop build or `ufield-mcp` for Claude Desktop; OneDrive, S3, P2P and GPU workers out.
2. **ADR 0002 (credentials):** mark §1 rows for Biomitta-held MML/CDSE/Anthropic/STT keys as superseded by ADR 0004. Those keys belong to the user: in the device keystore, or in the environment of *their* ufield-server. Replace §6's Google sign-in → server session: there are no accounts, "Connect Google Drive" uses AppAuth on Biomitta's public client ID, and the self-hosted server authenticates its own local users (it's its own OAuth authorisation server for remote MCP). Everything else in ADR 0002 stays: no secrets in tool inputs, HTTP Basic for MML, never keys in URLs, QGIS auth DB, qtkeychain.
3. **ADR 0003 (Claude, notes, voice):**
   - §1 in-app agent: calls the user-configured LLM endpoint (their ufield-server `ai` profile, or their own API key), not Biomitta's `/agent`. `ufield-mcp` local mode is unchanged.
   - Remote connector: served by the user's server, exposed via tunnel only if they opt in.
   - §4 speech-to-text: the user's Whisper server, or on-device whisper.cpp (MIT); this reverses "no on-device STT". The async voice-note queue design stays, pointed at the user's server.
   - Notes (§2) and analysis tools (§3) are unaffected.
4. **`schema/tools.json`** (as proposals; implement with the repo's rules):
   - `request_farm_pack`: add `run_on: auto | device | server`.
   - `list_projects.storage`: add `device_folder`, `webdav`, `s3`, `mergin_ce`.
   - `get_setup_status`: storage connections, server/tenant, GPU and workers, backups, which credentials exist (never values), sharing scope. Check overlap with `list_credentials` and merge if cleaner.
   - `connect_storage`: starts the native UI flow; returns status only; no secrets.
   - `prefetch_public_layers` and `configure_public_sharing` (ADR 0005). Post-MVP, so they can go in as `x-ufield-status: planned` or wait.
   - `request_product`: runs on a registered GPU worker, otherwise `unavailable`.
   - The session drafted these with top-level `oneOf`; convert them to `x-ufield-rules` (`exactly_one_of`).
5. **`config/layers.yaml`:**
   - Add `license_allowlist: [CC-BY-4.0, copernicus-open]`.
   - Add `p2p_shareable` per provider: ruokavirasto `false` (parcel attributes), luke `false` until verified, others `true`.
   - Header comments for the `auth` section: secrets are the **user's**, read from the user's ufield-server environment or the device keystore; `via_server` means "prefer the user's server".
   - GTK: open-licence layers only; peatland layers and Luke MVMI disabled until their licences are confirmed.
6. **`docs/layer-sources.md`:**
   - Farm Pack "why it runs on the server" becomes "where it runs": the user's server or the phone, with the user's own keys; same C++/GDAL code (ADR 0004 §3).
   - CDSE scaling note: per-user accounts.
   - Decision 4 (fetching through Biomitta's server) is replaced.
7. **`docs/roadmap.md`:**
   - Phase 0: drop Biomitta's server MML/CDSE accounts; add AppAuth OAuth client with custom scheme enabled.
   - Phase 3 becomes the *self-hosted* ufield-server: Compose profiles, tenants, mesh-network docs, backups; no Google-session sign-in; no account-deletion endpoint.
   - Phase 6: user-configured LLM/STT.
   - Phase 7: Data safety per `docs/licensing.md` §3.4; account deletion only under decision B; in-app licences screen and source tags per release; CI check for proprietary SDKs.
   - After 1.0: orthomosaic/DEM worker (NodeODM) first, then 3DGS (gsplat); P2P sharing pilot with a farmer group.
8. **Server code** (`server/src/ufield/`): `dispatcher.py`, `project.py` and `mcp_local.py` are unaffected. For the remote MCP server, the self-hosted instance authenticates its own users (local accounts or the organisation's OIDC) instead of Google sign-in.

## Not decided in the session (open)

- Decision A vs B above.
- Pilot group for P2P sharing (cooperative, ProAgria region, SEAMK test farms).
- Planet partner terms (only relevant under B or if Biomitta resells).
- Trademark search and registration for μField.
- A lawyer's review of `docs/licensing.md` §1.3, §3 and §4 before public release.
