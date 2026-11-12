# 0007 — Field events as claims with evidence: farmer's report, satellite, machine log

Status: proposed, 2026-11-12

## Context

ADR 0006 makes field event timing a product. A single date and confidence hides what an event rests on. Three kinds of evidence are available, and each knows something different:

| Evidence | Knows well | Weakness | Independent of the farmer? |
|---|---|---|---|
| **Farmer's report** | *What* was done and why: silage or hay, which cut, ploughing or light tillage, the exact day | May be wrong, late or missing | No |
| **Satellite** (open + Planet/UP42) | *Whether* something happened and *when* (interval), over the whole parcel | Can't tell work methods apart well; clouds | **Yes** |
| **Machine log** | *Exact time* and *which parts* of the parcel were worked; implement type (ISOBUS) | Doesn't cover every machine; very sensitive | No, but hard to forge |

The report and the log both come from the farmer or the contractor. Only satellite evidence is independent.

## Decision

### 1. An event is a claim with a list of evidence

Each piece of evidence is its own STAC item with its own `ufield:visibility`. The event's `ufield:provenance` lists the evidence item ids (rule 6 holds when evidence is in different visibility classes).

### 2. Evidence level instead of a single confidence

| Level | Basis |
|---|---|
| **T0** | Report only, or log only |
| **T1** | Satellite from open data (interval) |
| **T2** | Satellite refined with paid imagery (± 1 day) |
| **T3** | Satellite + report or log, in agreement |
| **T4** | All three in agreement |

- **Agreement:** the reported or logged day falls inside the satellite interval, and the log's worked area covers the part of the parcel where the satellite sees the change.
- An event counts as **independently verified** only with satellite evidence.
- **Conflicts are flagged, never resolved silently.** Example: reported mowing 5 July, satellite 8–12 July. The farmer can correct the report; the conflict stays in the history. Conflicting events don't rise above T1 and aren't used as training data.

### 3. What may leave the customer server

| Evidence | Stays on the customer server | May leave (through the release gate, ADR 0006) |
|---|---|---|
| Report | Free-text notes | Event, day, work method; with the farmer's consent |
| Satellite, paid | Images, series, image ids | Interval, confidence, source class (ADR 0006 §2) |
| Machine log | **The whole track**, times of day, operator, machine id, speeds | Day (no time of day), worked share rounded to quarters, implement class |

Machine logs:

- **The operator's location is personal data**, and tracking employees falls under the Finnish Act on the Protection of Privacy in Working Life. Only day and worked share are released; never times of day, speeds or the operator.
- **A contractor's machine isn't the farmer's data.** When the log comes from a contractor's machine, the contractor's consent is needed as well. A contractor's full route history is also a trade secret.
- **EU Data Act** (applies from September 2025): the user of a connected product such as a tractor has the right to get the data it generates and share it with a third party of their choice. Check each manufacturer's API and its terms before integrating.

### 4. Machine log sources, in order

1. **ISO-XML (ISO 11783-10 TaskData):** the export format of most ISOBUS terminals; includes implement and task.
2. **GeoJSON, Shapefile, GPX:** exports from manufacturer portals and guidance systems.
3. **μField's own track:** the phone in the cab, when the machine has no telematics. Recorded as a user-started foreground service with an ongoing notification, so no background location is needed (rule 8); the foreground service location type is declared in Play Console and justified in `docs/play-policy.md`.
4. **Manufacturer APIs** (John Deere, CNH, CLAAS, …) later, when there is demand.

The raw log is stored unchanged in the customer's private store, like capture originals (rule 5). Event evidence is derived from it, and its provenance refers to the log's hash.

### 5. Record (tier B)

```json
{
  "parcel": "1641431471-1", "year": 2025,
  "event": "mowing", "cut": 1, "method": "silage",
  "best_estimate": "2025-07-09", "evidence_level": "T4", "conflicts": [],
  "evidence": [
    {"kind": "report", "day": "2025-07-09", "method": "silage"},
    {"kind": "satellite", "start": "2025-07-08", "end": "2025-07-10",
     "confidence": 0.85, "source_classes": ["sentinel-2", "sentinel-1", "planetscope"]},
    {"kind": "machine_log", "day": "2025-07-09", "coverage": 1.0,
     "implement": "mower", "format": "iso-xml"}
  ],
  "detection_method": "field-events/2.3"
}
```

Tier A gets `share_verified_t3`: the share of a cell's events with at least two sources, a direct measure of how far to trust the calendar.

### 6. Training data

T3 and T4 events are the best training and validation data for open-data detection. Machine logs give exact days and partial work within a parcel, which is the hardest thing to learn from satellites. Model weights are a derivative and hold no individual events, but training use is a separate item in the consent.

### 7. Tools (rule 1)

| Tool | Purpose | Needs the server? |
|---|---|---|
| `record_field_event` | Farmer's report: parcel, event, day, method, note | **No**, works on the phone |
| `import_machine_log` | Import ISO-XML, GeoJSON or GPX and derive event evidence; the raw log stays private | No for import, yes for satellite comparison |
| `start_work_track` / `stop_work_track` | Phone track as a machine log | No |
| `get_field_events` | Events, evidence, level and conflicts (ADR 0006) | Yes for satellite evidence |
| `resolve_event_conflict` | Farmer corrects the report or acknowledges a conflict | No |

## Consequences

- Reports and logs can be collected before the satellite side exists, so evidence builds up from the first season.
- Consent covers more parties: farmer, contractor, and training use.
- The phone app gets a work-track feature with its own Play declaration.

## Open questions

- Whether `record_field_event` and `import_machine_log` go into the MVP (they work without the server, like point notes) or after it. This widens the MVP scope in `CLAUDE.md`, so it's a product decision.
- Which ISO-XML elements are needed for implement class, and how reliably terminals fill them.
- Agreement tolerances (days, worked share) per event type, measured against logged events.
