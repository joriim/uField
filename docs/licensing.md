# Licence and compliance analysis: μField (uGIS)

Status: research draft, 2026-09-27; updated the same day for ADR 0004 decision B (Biomitta service for phone-only users + own wired or virtual servers), peer-to-peer sharing (ADR 0005) and on-prem servers (ADR 0006). Put at `docs/licensing.md`.
This is a practical engineering analysis, not legal advice. Have a lawyer review sections 1.3, 3 and 4 before the public Google Play release.

## Summary

1. **The upstream code is GPL-2.0-or-later, not GPL-3.0.** Mergin Maps mobile's source headers say "version 2 of the License, or (at your option) any later version", and the licence file is GPLv2. Because the app also bundles Qt (LGPLv3) and Apache-2.0 libraries, the finished μField app can only be distributed under **GPL-3.0**. That's allowed by "or later" and matches what we planned.
2. **Publish the exact source of every Play release.** Tag each release in the public GitHub repo, include the licence and third-party notices inside the app, and link to the source from the app and the store listing.
3. **Keep proprietary Google SDKs out of the app if you can.** Google Play Services (the usual Google sign-in library) is closed source and is not a "system library" under the GPL. Biomitta-service sign-in goes through ufield-server (Google web OAuth client + App Link), and "Connect Google Drive" uses AppAuth (open source) on the device, plus plain REST calls to Drive. Neither needs Play Services (section 1.3).
4. **Don't use the Mergin Maps or Peltomappi names or logos.** Mentioning them in the credits ("based on Mergin Maps mobile") is fine. Register μField yourself; a quick search found no app with that name, but that's not a trademark search.
5. **All the national data sources are CC BY 4.0, except one GTK licence tier.** The obligation is attribution, and the app must carry it automatically through the map screen, exports and Farm Packs. GTK's "basic licence" layers forbid commercial and public-service use, so they must not be in the app.
6. **Field parcel data can be personal data.** In 2019 Ruokavirasto shut down Peltolohkot.fi for publishing parcel data too freely; it reopened only after location search was removed. μField must not let users browse or search other farmers' parcels and identifiers at scale. Showing parcels on a map for the user's own farm is fine.
7. **Paid imagery licences are per licensee.** For Planet and UP42 data, only irreversible derived products can normally be shared. Organisations with their own server buy directly and are the licensee. The Biomitta service offers no paid imagery until Biomitta has partner or reseller terms.
8. **Biomitta's duties depend on the profile (ADR 0004 decision B).**
   - **Biomitta service** (phone-only users): Biomitta is the controller. It needs a privacy policy, a record of processing activities, data processing agreements with Anthropic, the STT provider and the EU hosting provider, data minimisation, and account deletion in the app and on the web.
   - **Own wired or virtual servers:** nothing reaches Biomitta; the organisation is the controller.
   - **Both:** Google OAuth verification (identity sign-in and `drive.file`) and a Play Data safety form. Play counts data sent even to the user's own server as "collected".
9. **Peer-to-peer sharing of public layers is allowed** for CC BY and Copernicus data, as long as attribution travels with the packages. Parcel attributes, GTK basic-licence layers, paid imagery and user data must never be shared, and peers see each other's IP addresses (section 3.5).

---

## 1. The app's own code

### 1.1 Components and licences

| Component | Licence | What it means for μField |
|---|---|---|
| Mergin Maps mobile (fork base) | GPL-2.0-or-later (headers), GPLv2 licence file; Mergin's docs say "GPL-v2" | Can be relicensed upward to GPL-3.0 in the combined app. Keep all existing headers unchanged. |
| QGIS core (linked) | GPL-2.0-or-later | Compatible. |
| Qt 6 (qtbase, qtmultimedia, qtsensors, qtwebsockets, …) | LGPL-3.0 / GPL (open-source edition) | LGPLv3 is **incompatible with GPLv2-only**, which is why the combined app must be GPL-3.0. The LGPL relinking requirement is met because the whole app source is public and buildable. |
| GDAL, PROJ, SQLite, libtiff, geodiff | MIT/X11/BSD-style, public domain | Compatible; include their notices. |
| zxing-cpp | Apache-2.0 | Compatible with GPL-3.0 only, which is another reason for GPL-3.0. |
| libspatialite, QCA | MPL/GPL/LGPL tri-licence; LGPL-2.1 | Compatible (use the GPL/LGPL option). |
| Poppler (GDAL feature) | GPL-2.0/3.0 | Compatible with a GPL app. |
| FFmpeg (as configured in vcpkg) | LGPL-2.1+ (the manifest enables no `gpl` or non-free features; openh264 is only built on Linux/Windows) | Compatible. Codec patents are a separate question from copyright; Mergin ships the same setup. |
| exiv2 | GPL-2.0-or-later | Compatible. |
| OpenSSL 3 | Apache-2.0 | Compatible with GPL-3.0, not with GPL-2.0-only. This is another reason the combined app is GPL-3.0. |
| Mergin Maps QGIS plugin | GPL-3.0 | Only relevant if we fork it. |
| Mergin Maps server Community Edition | AGPL-3.0 (dual-licensed with a commercial licence) | If Biomitta runs a modified copy for users, the modified source must be offered to those users (AGPL network clause). |
| Mergin Python/C++ client, media-sync, work-packages | MIT | Free to use anywhere. |
| openEO Python client, pystac, rasterio | Apache-2.0 / BSD | Free to use in the server under any licence. |
| MCP SDKs (Python/TypeScript) | MIT | Free to use. |
| Agrihubi/huikee-maatalous | No licence file (a link list) | We only use facts (URLs), which aren't copyrightable. Nothing to comply with. |

**Licence choice for our own code:**
- Use `SPDX-License-Identifier: GPL-2.0-or-later` on new app files, the same as upstream. That keeps the option of sending fixes back to Mergin. The *distributed* app is still GPL-3.0.
- **self-hosted ufield-server** (self-hosted, ADR 0004) is a headless build of the same GPL core, so it is **GPL-3.0**. Its container images are distributions: publish the matching source tag and Dockerfile; distro packages in the base image are covered by the distro's source repositories. The bundled Mergin Maps CE is pulled unmodified from upstream; if we ever modify it, AGPL-3.0 requires offering the modified source to the users of that server.

### 1.2 What Biomitta must do for each Play release

1. **Publish the Corresponding Source** for the exact build: a git tag per `versionCode`, including build scripts, vcpkg overlay ports and CI workflow files. Signing keys are *not* part of this. GPLv3's "installation information" rule for consumer devices doesn't apply to an app downloaded onto the user's own phone.
2. **Give recipients the licence and a way to get the source.** Add an "About → Licences" screen with the GPL-3.0 text, all third-party notices, and a link to the tagged source. Put the same link in the Play store description.
3. **Keep every upstream copyright header** and add a `NOTICE`/`CREDITS` file: "μField is based on Mergin Maps mobile © Lutra Consulting Ltd and contributors, licensed under GPL-2.0-or-later."
4. **Mark our modifications.** GPL requires modified files to carry a notice that they were changed, with a date. The `// UFIELD:` markers plus git history satisfy this.
5. **Add no further restrictions.** The app's own terms of service must not forbid reverse engineering or redistribution of the app code. Terms about *our server services* (fair use, accounts) are fine.
6. **Keep old sources available** for as long as that release is offered for download. Permanent GitHub tags do this.

### 1.3 Proprietary SDKs

The GPL requires that the complete source of everything linked into the app be available, except "System Libraries". Google Play Services, Firebase, Crashlytics and the Play Integrity and Play Billing libraries are closed source and not system libraries. Adding them to a GPL program whose copyright you don't fully own (Lutra and the other contributors hold most of it) is a licence violation risk.

- **Chosen (ADR 0004 decision B):**
  - **Service sign-in (identity):** option A, through Biomitta's ufield-server.
  - **Own servers:** their own users (local accounts or the organisation's OIDC).
  - **Google Drive connection (both):** option B, on the device, so Drive tokens never reach any server. Keep the SAF route through the Drive app as the fallback if Google removes custom schemes.
- **Google sign-in: there's no perfect option, so choose deliberately.** Since October 2023, new Android OAuth clients can't use custom URI scheme redirects by default, and Google points developers to its Google Identity Services SDK, which depends on Play Services. The options are:
  - **A. Sign-in through our server (recommended):** a Google *web* OAuth client with an https redirect to μField's server, then an Android App Link back into the app. All the code involved is open source (AppAuth, Apache-2.0, or a Custom Tab). The trade-off is that the server holds the user's Drive refresh token, so it must be encrypted and covered in the privacy policy.
  - **B. AppAuth with custom-scheme redirects re-enabled** in the OAuth client's advanced settings. It works today, but Google discourages it and may remove it.
  - **C. Google Identity Services SDK.** This is the easiest, but it puts a proprietary library into a GPL app whose copyright we don't fully own. Only do this after legal advice, and ideally isolate it in an optional build flavour so an open build always exists.
- **Google Drive:** call the Drive REST API directly with Qt's network stack. No Google SDK is needed.
- **Crash reporting and analytics:** use an open-source option (for example self-hosted Sentry or GlitchTip with the Sentry SDK, which is MIT) or none at all.
- **Add a CI check** that fails the build if a `com.google.android.gms`, `com.google.firebase` or other known proprietary artifact appears in the APK's dependency tree.

This also keeps the app installable without Google services (de-Googled Android phones, F-Droid).

### 1.4 Names and trademarks

- **"Mergin Maps"** and its logo belong to Lutra Consulting. The GPL gives no trademark rights. Remove them from the app name, icons, splash screen, UI strings, store listing and package ID. Factual credit ("based on Mergin Maps mobile") is fine.
- **"Peltomappi"** is Livia/Gispo's project name. Don't use it.
- **"QGIS"** can be used descriptively ("works with QGIS projects"), not as part of the product name or logo.
- **Mergin Maps' cloud:** a fork must not use `app.merginmaps.com` by default, because that service has its own terms of service and is a paid product. Point the sync code at our own server or disable it.
- **μField:** do a proper search in the Finnish trademark register (PRH) and EUIPO's TMview before spending on branding, then consider registering it. GPLv3 §7(e) lets us refuse trademark rights to forks, so others can fork the code but not ship it as "μField".

---

## 2. Data sources

All attribution must appear (a) on the map while a layer is visible, (b) inside every exported file or Farm Pack, and (c) in the STAC metadata. CC BY 4.0 also requires a licence link, an indication of any modification, and no DRM on the licensed data itself. Charging for a Farm Pack is allowed; technically locking the CC BY content inside it is not.

| Source | Licence | Required attribution (use in app and exports) | Specific conditions |
|---|---|---|---|
| **Maanmittauslaitos (MML)**: WMTS, WCS ortho/DEM, file service, OGC API | CC BY 4.0 + terms of use for the open interface | "Contains data from the National Land Survey of Finland [dataset] MM/YYYY" (FI: "Sisältää Maanmittauslaitoksen [aineisto] -aineistoa MM/YYYY") | The open interface is **not intended for high-volume services**; MML may restrict heavy use and can change the service with 3 months' notice. The API key holder is responsible for protecting the key. For a public app: keep the key server-side and cache results, and budget for the paid contract service once usage grows. No warranty. |
| **Ruokavirasto**: field parcels, plant parcels, landscape features, agricultural land | CC BY 4.0 | "Source: Ruokavirasto, [dataset] [year], CC BY 4.0" | The published data carries parcel ID, crop, area, organic-farming flag and some area splits, but **no farmer identity**. It can still become personal data when linked to an identifiable farmer (see section 3). |
| **GTK**: soil maps, acid sulphate soils | **Open licence** = CC BY 4.0 (in force since 1 Dec 2025) | "GTK Open License CC Attribution 4.0, includes material from [dataset], accessed in μField on [date]" + licence link + link to the original | Mark modifications with a link to the original. |
| **GTK**: "basic licence" layers (studied peatland areas, shaded relief, flutes, seabed, …) | **GTK basic licence** | n/a | **Commercial or public-service use is prohibited**, and so is transfer to third parties. **Exclude these from the app.** Also confirm the licence of the 2023 peatland layers (peatland types, nutrient level, peat carbon stock) before enabling them. |
| **SYKE**: catchments, groundwater, floods, CORINE, Natura, VEMALA, Carbon Map | CC BY 4.0 for open datasets and map services | "Source: Syke, [dataset], CC BY 4.0" | Check each dataset's metadata for exceptions (for example third-party data inside CORINE). |
| **Metsäkeskus**: forest stands, habitats, grid data | CC BY 4.0 | "Contains Finnish Forest Centre [dataset] data MM/YYYY" (FI: "Sisältää Suomen metsäkeskuksen [aineisto] aineistoa MM/YYYY") | Open data deliberately excludes owner names, contact details and property IDs, so **don't re-link stands to owners**. Owner data is only released through formal data requests. |
| **Luke**: multi-source forest inventory (MVMI) | Probably CC BY 4.0 (**to verify**) | "Source: Luonnonvarakeskus (Luke), MVMI [year]" | Keep disabled until verified. |
| **FMI** open data | CC BY 4.0 | "Finnish Meteorological Institute open data, [dataset]" | Usage limits: 20,000 download-service queries per day, 10,000 view-service queries per day, 600 queries per 5 minutes. Cache on the server. |
| **Copernicus Sentinel** (through CDSE / openEO) | Copernicus Sentinel data licence: free, full and open, commercial use allowed | Unmodified: "Copernicus Sentinel data [year]". Our NDVI, mosaics and composites: "Contains modified Copernicus Sentinel data [year]" | The **CDSE platform** terms are separate from the data licence. Respect the quotas and **don't create multiple accounts to get round them**. Other portal content (not Sentinel data) is non-commercial only. For a public service, buy commercial quota through a CDSE service provider. |
| **Planet** (PlanetScope, future) | Commercial EULA, per licensee | Keep Planet's copyright marks in the data and metadata | Standard terms restrict giving raw imagery to third parties. Derived products where the original can't practically be extracted are usually shareable. **Get partner or reseller terms in writing** before showing raw imagery to farmers. |
| **UP42** (future) | EULA per data provider, with your organisation as licensee | Per provider | "External use and redistribution are usually not allowed" for original imagery. Customers must order the data themselves (Biomitta can act as their contractor), or receive derived products only. |

---

## 3. Personal data, Google and Play obligations

### 3.1 Field parcel data: the Peltolohkot.fi lesson

In June 2019 Ruokavirasto ordered Peltolohkot.fi shut down. Its view was that the parcel dataset is personal data when it identifies subsidy recipients and the exact location of their holdings, and a service publishing it for free public use can't override farmers' data protection rights. The service reopened in July 2019 after **location-based search was removed**, with Ruokavirasto acting as data controller.

Ruokavirasto now publishes the parcel data itself as open CC BY 4.0 data without farmer identities, but the risk from combining data remains. For μField:

- **Allowed:** showing parcel boundaries as a map layer; the user selecting *their own* parcels for a Farm Pack; the user's own observations on those parcels.
- **Avoid:** features that list or search other farms' parcels, crops or organic status by area or ID for arbitrary users; linking parcels to property IDs, owner names or business IDs; exporting a region's parcel attributes in bulk; MCP tools that let an LLM do the same at scale.
- **Tool schema consequence:** `request_farm_pack` takes `parcel_ids`. Limit it to the parcels the user has confirmed as theirs, and cap the number of parcels and the total area per account. The same limits apply to `query_features` on the parcel layers.

### 3.2 The app's own personal data (GDPR)

μField processes location, photos (EXIF and GNSS), video with a pose track, voice recordings, notes and the Google account e-mail.

- **Biomitta service (phone-only users):** Biomitta is the **controller** for accounts and the processing it does: sign-in, Farm Pack jobs, voice audio and transcripts, agent requests.
  - Needed: a privacy policy in Finnish and English; a record of processing activities; data processing agreements with Anthropic (API terms that exclude training on customer data), the speech-to-text provider and the hosting provider (EU region).
  - **Minimise:** no project data stored on the service; audio deleted after transcription; transcripts and agent context not kept beyond the request; Farm Pack outputs deleted after delivery or a short retention period; no telemetry by default.
- **Own wired or virtual servers:** Biomitta receives nothing. The organisation is the controller (or the farm's processor, ADR 0006 §4). Its hosting provider is its processor. Its own LLM or STT provider, if any, is under its own agreement.
- **Managed servers:** if Biomitta installs and maintains a server in the customer's own cloud account under a support contract, Biomitta has access and acts as the customer's processor, so a data processing agreement is needed.
- **Users connecting their own LLM client over MCP** (Claude, ChatGPT, …): the data goes to *that* provider under the *user's* own agreement. Show a clear notice when an external MCP client is authorised, and let the user scope it to one project.
- **Professional users** (contractors, advisers) who record data about other people's farms are controllers of that data themselves. Say so in the terms of use.

### 3.3 Google sign-in and Drive

- `drive.file` is a **non-sensitive** scope, so it only needs basic OAuth app verification: a privacy policy, a home page and a verified domain. Never request `drive` or `drive.readonly`; those are restricted scopes and need a paid security assessment if data reaches our servers.
- **Google API Services User Data Policy:** keep the privacy policy and in-app disclosures accurate. Don't sell or transfer Google user data to ad platforms or data brokers. Don't let staff read it without the user's consent.
- **Google Workspace policy on AI:** data from restricted Workspace scopes may not be used to train AI models beyond that user's own personalised model. We only use `drive.file`, but follow the same rule anyway: **never use users' Drive content to train shared models**, and state that in the privacy policy.

### 3.4 Google Play

- A privacy policy URL and a complete **Data safety** form. Play defines collection as *any* transmission off the device, with no exemption for the user's own server. So declare location, photos/videos, audio and files as collected. For the Biomitta service, list what goes there and name Anthropic and the STT provider as processors; for own storage and own servers, explain "sent only to the storage or server the user selects". Mark them not shared, because each transfer is user-initiated. With P2P in `group`/`public` scope, declare the IP address and region as shared with other users.
- **Account deletion:** required, because the Biomitta service creates accounts. The app needs a delete-account path and there must be a web page, and deleting an account removes all of that user's service data. Own-server accounts are the organisation's responsibility.
- **P2P transfers** on Android run as a foreground service of type `dataSync`, which must be declared in Play Console.
- **Location permission:** foreground only for the MVP. Background location needs a Play declaration and justification (`docs/play-policy.md`).
- Target API 36.

### 3.6 On-prem servers at advisory firms and farms (ADR 0006)

- **GPU workers and model weights:** only commercially usable licences (MIT, BSD, Apache-2.0, GPL, AGPL run unmodified). Excluded as non-commercial: the original Inria 3D Gaussian Splatting code, DUSt3R/MASt3R (CC BY-NC-SA 4.0), non-commercial VGGT weights. Use gsplat/nerfstudio (Apache-2.0) instead. OpenDroneMap is AGPL-3.0: fine unmodified on the customer's server; modified versions offered over a network need their source published. Record each worker's and model's licence in STAC provenance.
- **Mergin Maps CE vs EE:** CE has one workspace; multiple workspaces are EE-only (commercial). Tenant-per-CE-instance (ADR 0006 option A) stays within CE's AGPL licence.
- **Advisory firms** handling a farm's data are either the farm's processor (data processing agreement needed) or a controller for their own advisory records. μField's tenant export and deletion support the farm's rights to take its data and have it erased.
- **Biomitta as a consultancy** running its own self-hosted ufield-server is a normal controller or processor for its clients' data. That's separate from its roles as app publisher and as operator of the Biomitta service.

### 3.5 Peer-to-peer sharing of public layers (ADR 0005)

- **Licences:** CC BY 4.0 and the Copernicus licence allow redistribution. Each package must carry attribution, a licence link and a "modified" notice, with no DRM.
- **Never share:** Ruokavirasto parcel attributes (the Peltolohkot.fi risk), GTK basic-licence layers, Planet/UP42 imagery, the user's own observations. Enforced by `p2p_shareable` in the registry and STAC provenance.
- **Privacy:** peers see each other's IP address (personal data) and which region is requested. Sharing is off by default; share by map sheet, not by farm; fetch surrounding cover sheets; prefer `lan` and trusted `group` scopes; explain `public` scope clearly before enabling.
- **Service terms:** redistributing results computed with your own CDSE account is allowed by the Sentinel licence. Pooling results isn't the multiple-account quota avoidance CDSE forbids. P2P also reduces load on MML's small-scale open interface.
- **Biomitta runs no tracker or index**, so it processes no peer data.

---

## 4. What users must comply with

| When a user… | …they must | How the app helps |
|---|---|---|
| Shares or prints a map, PDF or screenshot with open layers | Keep the attribution (source, licence, date, "modified" where true) | Attribution is burned into exports and PDFs automatically; it can't be switched off for CC BY layers. |
| Exports a Farm Pack or GeoPackage and gives it to others | Pass the attribution and licence along; not add DRM to the CC BY content | Each pack contains `LICENSES.md` and STAC items with licence and attribution. |
| Uses parcel data beyond their own farm | Follow GDPR: no profiling of other farmers, no direct marketing, no linking to owners | The app doesn't offer those features; the terms of use state the rule. |
| Uses paid imagery they bought themselves (they are the licensee) | Stay within the EULA: usually no redistribution of the raw imagery | Paid assets are marked `license: proprietary-<provider>` and `visibility: proprietary`; the export options exclude raw paid imagery unless the licence allows it. |
| Connects their own MML key and CDSE account (every user who wants the Farm Pack) | Accept those services' terms; protect the key; no multiple-account quota tricks | Guided sign-up; keys stay in the device keystore or on the user's self-hosted ufield-server, never with Biomitta. |
| Self-hosts storage or self-hosted ufield-server | Act as controller of the data on it; keep it secure and backed up; follow AGPL if they modify Mergin CE and offer it to others | Documented Compose stack, LAN-only defaults, backup status in the app. |
| Turns on peer-to-peer sharing | Accept that peers see their IP address and region; share only what the app marks shareable | Off by default; clear scope choice; the app filters out non-shareable data. |
| Connects an external AI assistant over MCP | Accept that the assistant's provider receives the data they expose | A consent screen per client and a per-project scope. |
| Records photos, video or voice that include other people or neighbours' property | Handle it lawfully as the controller of that data | A reminder in the capture help text and in the terms of use. |
| Modifies and redistributes the app | Follow GPL-3.0 (publish the source) and not use the μField name or logo | The source link and the licence screen in the app. |

Users' own field data (observations, photos, notes) is **theirs**. The GPL covers the app code, not the data made with it. Say this explicitly in the terms of use.

---

## 5. Build into the product

1. **Attribution engine.** `config/layers.yaml` gets `attribution_template`, `license_url` and `modified_marker` per provider. The map, exports, PDFs and STAC are all generated from these.
2. **Licence gate in the registry.** A layer can only be enabled if `license` is on an allow-list (`CC-BY-4.0`, `copernicus-open`). Anything else (GTK basic licence, Planet, UP42) needs an explicit per-provider flag that also blocks redistribution in exports.
3. **Licence propagation.** A Farm Pack or product output inherits every input licence. If any input is non-redistributable, the output is marked non-redistributable too.
4. **Parcel privacy limits** on `request_farm_pack` and `query_features` for the parcel layers (section 3.1).
5. **About → Licences screen:** the GPL-3.0 text, third-party notices generated from vcpkg and pip metadata, the source link, and the data licences.
6. **CI checks:** SPDX headers present, no proprietary SDKs in the APK, all layers in the registry have a licence and attribution, and a licence report generated per release.

## 6. Changes needed elsewhere in the repo

Listed with exact file and section references in `docs/handoff/2026-09-27-design-session.md` (licensing correction in `CLAUDE.md` rule 3, new rules, `config/layers.yaml` licence allow-list, GTK peatland and Luke MVMI layers disabled until verified).

## 7. Open questions

- ~~Server licence~~: settled as GPL-3.0 (headless build of the GPL core, ADR 0004).
- Does Google keep allowing custom-scheme redirects for our Android OAuth client? Monitor, and keep the SAF fallback working.
- Planet: Finland's price tier, and partner terms that allow showing raw imagery to farmers.
- GTK 2023 peatland layers and Luke MVMI: confirm their licences.
- MML: at what usage level do we move to the contract service, and what does it cost?
- Trademark search and registration for μField.
- A lawyer to review sections 1.3, 3 and 4 before public release.

## Sources

- [MerginMaps/mobile repository (licence file, source headers)](https://github.com/MerginMaps/mobile)
- [Mergin Maps licensing](https://merginmaps.com/docs/misc/licensing/)
- [MerginMaps/server licence](https://github.com/MerginMaps/server)
- [Google: OAuth custom URI scheme restrictions (2023)](https://developers.googleblog.com/improving-user-safety-in-oauth-flows-through-new-oauth-custom-uri-scheme-restrictions/)
- [Choose Google Drive API scopes](https://developers.google.com/workspace/drive/api/guides/api-specific-auth)
- [Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy)
- [Google Workspace API user data and developer policy](https://developers.google.com/workspace/workspace-api-user-data-developer-policy)
- [Google Play account deletion requirements](https://support.google.com/googleplay/android-developer/answer/13327111?hl=en)
- [MML open data licence CC BY 4.0](https://www.maanmittauslaitos.fi/en/opendata-licence-cc40)
- [MML terms of use for the open interface](https://www.maanmittauslaitos.fi/en/terms-of-use-for-the-open-interface)
- [MML general information about interface services](https://www.maanmittauslaitos.fi/en/maps-and-spatial-data/datasets-and-interfaces/general-information-about-interface-services)
- [Ruokavirasto spatial datasets](https://www.ruokavirasto.fi/tietoa-meista/julkaistut-tietoaineistot/paikkatietoaineistot/)
- [Maaseudun Tulevaisuus: Peltolohkot.fi ordered closed (2019)](https://maaseuduntulevaisuus.fi/maatalous/artikkeli-1.455495)
- [Maaseudun Tulevaisuus: Ruokavirasto's GDPR reasoning (2019)](https://maaseuduntulevaisuus.fi/maatalous/artikkeli-1.456969)
- [Maaseudun Tulevaisuus: Peltolohkot.fi reopened (2019)](https://maaseuduntulevaisuus.fi/maatalous/artikkeli-1.467196)
- [GTK open licence](https://www.gtk.fi/en/open-licence/)
- [GTK basic licence](https://www.gtk.fi/en/basic-licence/)
- [SYKE open web services](https://www.syke.fi/en/environmental-data/open-web-services)
- [Metsäkeskus open forest and nature data products](https://www.metsakeskus.fi/fi/avoin-metsa-ja-luontotieto/avoimen-metsa-ja-luontotiedon-tuotteet)
- [Metsäkeskus: processing and disclosure of forest owner data](https://www.metsakeskus.fi/fi/asiointi/tietojen-kasittely-ja-luovutus/metsanomistajan-tietojen-kasittely-ja-luovutus)
- [FMI open data licence and terms](https://www.ilmatieteenlaitos.fi/avoin-data-lisenssi)
- [Copernicus Sentinel data licence](https://cds.climate.copernicus.eu/licences/ec-sentinel)
- [CDSE terms and conditions](https://dataspace.copernicus.eu/terms-and-conditions)
- [Planet expanded EULA (NASA CSDA)](https://www.earthdata.nasa.gov/s3fs-public/2022-02/Planet_Expanded_EULA_06-21.pdf)
- [UP42 EULAs](https://docs.up42.com/getting-started/eulas)
