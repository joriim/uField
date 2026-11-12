# 0006 — Field event timing as a shareable data product

Status: proposed, 2026-11-12

## Context

Kasvukuntoanalyysi detects field events (mowing, harvest, ploughing or tillage, emergence) from open satellite data (Sentinel-2, HLS, Sentinel-1) and refines their timing with PlanetScope (`planet_tarkennus.py`, `K2-T/peltotyot_planet.csv`; Niittoraportti `core.planetWindows`). Open data alone often gives a two-week interval in cloudy summers; a paid image narrows it to ± 1 day.

Paid imagery (Planet, UP42 providers) is licensed to the customer who ordered it and stays `proprietary` (ADR 0005 §3). Event timing derived from it is useful to others: regional mowing and harvest progress, contractor planning, nutrient loading and erosion work, and above all training and validation labels that make open-data detection better for everyone.

## Decision

### 1. The product has two tiers

| Tier | Content | Audience | Condition |
|---|---|---|---|
| **A. Regional event calendar** | Per crop group and week: share of parcels mowed, harvested or ploughed, and the cumulative curve; on a 10 km EPSG:3067 grid or per municipality | Everyone, through the hub (CC BY 4.0) | Aggregation thresholds (§3) |
| **B. Per-parcel events** | Parcel id, year, event, interval, best estimate, evidence level | Named recipients (contractor, adviser, buyer) or an opt-in pool | Consent of the parcel's farmer |

Evidence behind each event (farmer's report, satellite, machine log) is defined in ADR 0007.

### 2. Licence boundary: only non-reversible information leaves

| May leave | Never leaves |
|---|---|
| Event type, interval, best estimate, confidence | Images, clipped images, 3 m maps |
| Share of the parcel covered, rounded to quarters | Planet NDVI/NDRE series, per-image parcel statistics |
| Source classes (e.g. "PlanetScope, 2 images") | Image ids in the public item (they reveal what the customer ordered) |
| Method version | Within-parcel progress at pixel level |

A source may feed a released product only if the release allowlist marks it `derivative_shareable: true` with a reference to the contract clause. Before release:

- **Planet:** get written confirmation that event dates and aggregates may be given to third parties. The standard terms are understood to treat non-reversible derived products more freely than imagery, but this hasn't been checked against Biomitta's contract, and Planet Agriculture and the Insights Platform terms may differ (`docs/layer-sources.md` §3).
- **UP42:** terms are per provider (Airbus, ICEYE, Capella, …); allowlist each one separately.

### 3. Privacy

Parcel events are tied to a farmer and are personal data for sole traders. Timing also matters for subsidy compliance (mowing and green cover obligations) and competition.

- **Tier B needs the farmer's consent.** The customer who ordered imagery isn't always the farmer: an adviser or contractor may analyse other people's parcels. Events are released only for parcels the publisher has a recorded right to.
- **Tier A is aggregated.** A cell is published only with at least 10 parcels from at least 3 farms; otherwise it merges into a larger area or is withheld. Thresholds to be confirmed with a data protection adviser.
- **Release delay**, configurable: by default tier A no earlier than 7 days after the event, tier B after the season ends, unless the farmer chooses otherwise. This also limits the timing leak described in ADR 0005.

### 4. Release gate on the customer server

The gate runs where the paid imagery is, on the customer's server (ADR 0005):

```
 Customer server                                             Hub
┌───────────────────────────────────────────────┐        ┌───────────────────┐
│ Planet / UP42 images (private store)          │        │ check against     │
│        ▼                                      │        │ open-data         │
│ detection + refinement (versioned method)     │ signed │ interval          │
│        ▼                                      │ ─────► │        ▼          │
│ release gate: licence · consent · k-threshold │        │ A: calendar       │
│ · delay · field allowlist · no pixels         │        │ B: named only     │
└───────────────────────────────────────────────┘        └───────────────────┘
```

- **Provenance (CLAUDE.md rule 6):** the public STAC item is `public`, CC BY 4.0, and its `ufield:provenance` lists input *classes* plus the hash of a private provenance record. The private record holds image ids and stays on the customer server. Both kinds of input are recorded; ordered areas don't leak.
- **Quality on the hub:** the hub can't re-fetch a Planet image, but it can check that the refined date falls inside the interval detected from open data. Open data is the verification bound; events outside it are rejected. Only events from μField's versioned method, signed by the server's certificate, are accepted.
- **Contribute and consume** switches as in ADR 0005. Contribution can be give-to-get: contributors get the tier A calendar and the better open-data model.

### 5. Record

Tier A row: `grid_10km`, `crop_group` (from Ruokavirasto plant parcels, CC BY), `week`, `event`, `n_parcels`, `n_farms`, `share_done`, `share_cumulative`, `share_verified_t3` (ADR 0007).

Tier B record: see ADR 0007 §5.

### 6. Tools (rule 1)

Added to `schema/tools.json` after the MVP, together with ADR 0007's tools:

| Tool | Purpose |
|---|---|
| `get_field_events` | Events for the user's own parcels, with evidence and level (private) |
| `preview_field_event_release` | What the release gate would let through and what not, and why |
| `publish_field_events` | Release tier A and/or B; requires confirmation |
| `share_field_events` / `revoke_field_event_share` | Tier B to a named recipient, with a consent reference |
| `get_regional_event_calendar` | Tier A from the hub |

## Consequences

- Labels from customers with paid imagery improve open-data detection for all customers, which reduces how much Planet imagery anyone needs.
- A third flow crosses the hub besides public tiles: released derivatives. The hub's intake must tell them apart and apply the open-data bound.
- Consent records (who may release which parcel's events, to whom, for how long, including training use) become part of the customer server's data.

## Open questions

- **Code licence:** Kasvukuntoanalyysi is closed source; μField is GPL-3.0. The detection and refinement code can't be linked into ufield-server as it is. Either run it as a separate service behind a process boundary, or relicense the parts needed.
- Written confirmation from Planet and each UP42 provider on sharing derivatives (§2).
- Aggregation thresholds and delays with a data protection adviser (§3).
- Whether Biomitta buys Planet imagery to fill gaps in the calendar's coverage.
