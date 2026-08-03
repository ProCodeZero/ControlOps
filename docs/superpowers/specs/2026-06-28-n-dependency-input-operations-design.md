# Design: N-dependency input operations

**Date:** 2026-06-28
**Branch:** `feature-fixed` (branched from the first `main` commit `03dec5c`)
**Status:** Approved (business + caveats confirmed)

## Problem

Operations have appeared that take **more than two** operations as input dependencies
(beyond the single "input operation" and a single "alternative operation"). The current
code hard-codes exactly two dependency slots (`input_*` and `alternative_*`). The
calculation must be generalized to an arbitrary list of dependencies per operation.

The new init-data Excel (`инит дата 26 год (новый вариант).xlsx`) reflects this:

- Columns `alternative_operation_order` and `alternative_deadline` are **removed**.
- Columns `input_operation_order` and `input_deadline` now hold **comma-separated lists**
  (e.g. `input_operation_order = "23, 17"`, `input_deadline = "1, 5"`).
- The element at index 0 is the primary input (`is_alternative = false`); every element
  after index 0 is an alternative (`is_alternative = true`).

Example — `input_operation_order = "23, 17"`, `input_deadline = "1, 5"` produces:

```json
[
  { "operation_order": 23, "deadline": 1, "is_alternative": false },
  { "operation_order": 17, "deadline": 5, "is_alternative": true  }
]
```

## Decisions (confirmed)

> **Update 2026-07-13 (manager clarification, supersedes the aggregation decision below):**
> The rule is **conditional**, not universal `max`: take `min` when no alternative
> dependency produced a date, `max` once at least one did. This reproduces the old
> behaviour for single-input rows (~212) and matches `task.md` for rows with
> alternatives (~39). Implemented in one place — `aggregateDependencyDates()`.
> The manager also confirmed we do **not** wait for all inputs: only dependencies that
> actually have a date participate ("сколько фактически выполнено предшественников,
> столько и считаются") — which the collection loops already do.
> **Rationale (manager, 2026-07-29):** an alternative predecessor usually runs on a later
> schedule of its own, so the dependent operation must be given more time — hence the later
> (max) date. `minimal_date` is used as a *deadline* (see `setStatus`), so max = more slack,
> min = stricter. The rationale is about the principle, not a specific code location, so the
> conditional is applied in **both** phases (the old code had it in Phase 1 only, with
> Phase 2 always `min`).
>
> Note: the condition is "an alternative dependency **produced a date**", not merely
> "an alternative is configured" — this reproduces the old `alternative_date.has_value()`
> check exactly.

| Topic | Decision |
| --- | --- |
| Aggregation semantics | Conditional `min`/`max` — see the update note above. |
| DB schema | `input_*` columns become **TEXT** holding the comma lists; drop the two `alternative_*` columns. No normalized child table. |
| Build of `.exe` | Existing **GitHub Actions** workflow (`build-windows.yml`, vcpkg + MSVC). |
| Testing | **Unit tests** for parsing + aggregation (no live DB required). |
| Branch / standard | `feature-fixed`, keep **C++17**. |

## Scope

**In scope (deliverables):**

1. Patched C++ program (`ControlOps`).
2. Windows `.exe` produced by the existing GitHub Actions CI.
3. Forward SQL migration for the affected table.
4. Rollback SQL migration for #3.
5. Unit tests for the new parsing + aggregation logic.

**Out of scope:**

- The Python batch jobs (`export_control_ops.py`, etc.) — they load SAP data, not the
  init Excel. Confirmed via grep: no references to `static_initial_data` or `alternative`.
- The Excel → `static_initial_data_control_operations` ETL (external; the DB table is
  "long" with `region_id`/`region_date`, the Excel is "wide" per-region). Only the
  per-region columns are unchanged; the schema migration here is what the loader targets.
- The output table `control_operations_aggregated_new` — it never stored input/alt dates,
  so its schema is **unchanged**.

## 1. Data model (`src/data_struct/InitialData.hpp`)

```cpp
struct InputOperation {
    int  operation_order;
    int  deadline;
    bool is_alternative;   // false for index 0, true for the rest
};
```

In `InitialDataFrame`, replace the four scalar fields
(`input_operation_order`, `alternative_operation_order`, `input_deadline`,
`alternative_deadline`) with:

```cpp
std::vector<InputOperation> input_operations;
```

Keep `culture_id`, `t_material_id`, `region_id`, `season`, `region_date`,
`noinput_deadline`, `order`, `year`, `planned_dates`.

`PlannedDates` keeps only `planned_date` + `minimal_planned_date` (the per-dependency
planned dates are transient, computed during Phase 1).

In `SapDataFrame` (`src/data_struct/SapData.hpp`), replace `actual_input_date` and
`actual_alternative_date` with:

```cpp
std::vector<boost::gregorian::date> actual_input_dates;
```

## 2. Parsing (`InitialData` constructor)

Read `input_operation_order` / `input_deadline` as **strings** (TEXT columns), split on
`,`, trim whitespace, parse to ints, and zip into `input_operations`. Index 0 →
`is_alternative=false`; later indices → `true`. Null or empty → empty list.
Count mismatch (orders count ≠ deadlines count) → throw with a clear message.

Extracted as a pure, DB-free function for unit testing:

```cpp
std::vector<InputOperation>
parseInputOperations(const std::string& orders, const std::string& deadlines);
```

## 3. Phase 1 — planned dates (`CalcMinimalPlannedDate.cpp`)

```
planned_date = (region_date && noinput_deadline)
                 ? region_date + noinput_deadline
                 : null
list_planned_inputs = []
for dep in input_operations:
    parent = order_index_map[(culture, region, dep.operation_order, year)]
    if parent exists and parent.minimal_planned_date is set:
        list_planned_inputs += parent.minimal_planned_date + dep.deadline
minimal_planned_date = max( planned_date, all of list_planned_inputs )   // null if none
```

Rows are processed in `ORDER BY "order" ASC`, so parents (lower order) are computed
before children.

## 4. Phase 2 — actual dates

`calcInputDate` + `calcAlternativeDate` (in `CalcActualDateCompleteEntryOperation.cpp`)
collapse into a single `calcActualInputDates`:

```
for each SAP frame:
    init = CRTYS_index_map[frame key]
    actual_input_dates = []
    for dep in init.input_operations:
        parent_init   = order_index_map[(culture, region, dep.operation_order, year)]
        parent_actual = actual_date of parent op, found by CRTYS in uniqueSlices[year][higher_tm]
        if parent_actual is set:
            actual_input_dates += parent_actual + dep.deadline
    frame.actual_input_dates = actual_input_dates
```

This **fixes a latent bug**: the old `calcAlternativeDate` looked up the parent by
`input_operation_order` while applying `alternative_deadline`. Each dependency now carries
its own order + deadline.

`calcMinimalDate.cpp` (literal `task.md`):

```
min_plan_date = input_operations not empty
                  ? init.minimal_planned_date
                  : (frame.start_date ? frame.start_date + noinput_deadline : null)
minimal_date  = max( max(actual_input_dates), min_plan_date )   // null if none
status        = setStatus(actual_date, minimal_date, today)     // unchanged
is_actual     = (actual_date || !actual_input_dates.empty())
                  ? "Актуально" : "Ориентировочно"              // generalized
```

`setIsActual` is generalized to take a single "any actual input present" boolean instead
of two `actual_input_date` / `actual_alternative_date` arguments.

## 5. Confirmed behavior changes vs. today

1. **Phase 1 is now universal `max`.** Previously an operation with one input and no
   alternative used `min(planned, input)`. It now uses `max(planned, input)`, so
   single-input operations with an earlier region date shift **later**.
2. **Phase 2 flips `min` → `max`.** `minimal_date` was the `min` over actual/plan dates;
   it is now the `max`. This changes `status` / `minimal_date` for multi-dependency rows.

Both were explicitly accepted by the business.

## 6. SQL migrations (`static_initial_data_control_operations` only)

Forward (`migrations/2026-06-28_n_dependency_forward.sql`):

```sql
BEGIN;
-- Safety backup (drop after verifying the new run).
CREATE TABLE IF NOT EXISTS static_initial_data_control_operations_backup_20260628 AS
    SELECT * FROM static_initial_data_control_operations;

ALTER TABLE static_initial_data_control_operations
    ALTER COLUMN input_operation_order TYPE text USING input_operation_order::text,
    ALTER COLUMN input_deadline        TYPE text USING input_deadline::text;

ALTER TABLE static_initial_data_control_operations
    DROP COLUMN IF EXISTS alternative_operation_order,
    DROP COLUMN IF EXISTS alternative_deadline;
COMMIT;
```

Rollback (`migrations/2026-06-28_n_dependency_rollback.sql`):

```sql
BEGIN;
ALTER TABLE static_initial_data_control_operations
    ADD COLUMN IF NOT EXISTS alternative_operation_order integer,
    ADD COLUMN IF NOT EXISTS alternative_deadline        integer;

-- Lossy: keep only the first dependency. For a full restore, reload the old Excel
-- or restore from static_initial_data_control_operations_backup_20260628.
ALTER TABLE static_initial_data_control_operations
    ALTER COLUMN input_operation_order TYPE integer
        USING NULLIF(split_part(input_operation_order, ',', 1), '')::integer,
    ALTER COLUMN input_deadline TYPE integer
        USING NULLIF(split_part(input_deadline, ',', 1), '')::integer;
COMMIT;
```

The forward script also reads cleanly into the new TEXT columns when the external loader
imports the new Excel (comma lists go straight into `input_*`).

## 7. Tests

No boost / libpqxx / vcpkg are installed locally (only standalone MinGW `g++` at
`C:\mingw64` and CMake). To make tests **build and run locally** and verify them here:

- Extract `parseInputOperations` (std-only) and **templated** `aggregateMax` /
  `aggregateMin` helpers (date type as a template parameter) into a DB-free header/cpp.
- Production calc code calls the same helpers with `boost::gregorian::date`.
- Vendor a single-header test framework (doctest) so tests need no package manager.
- Build + run with `C:\mingw64\g++ -std=c++17`; the aggregation is exercised with a
  trivial comparable stand-in type (dates are comparable ordinals — same logic).

Test cases:

- Parse: `"23, 17"` / `"1, 5"`; single value; empty / null; surrounding whitespace;
  count mismatch (must throw).
- Aggregation: `max` over deps + plan date; `null` handling (all empty → `null`);
  single element; alternative flag carried through.

Wire into CMake under `BUILD_TESTS` using the vendored header (no `find_package(Catch2)`),
so CI can also run them.

## 8. Build & deployment notes

- `.exe` built by the existing `build-windows.yml` (vcpkg + MSVC) on push.
- Caveat (pre-existing): the deployed `libs/control_ops` bundle ships **MinGW** runtime
  DLLs (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, …), while the CI artifact is **MSVC**.
  Whoever deploys must pair the CI `.exe` with the matching runtime (or rebuild the bundle
  with MinGW). Out of scope for this change but flagged.

## Affected files

- `src/data_struct/InitialData.hpp` / `.cpp` — model + parsing.
- `src/data_struct/SapData.hpp` — `actual_input_dates`.
- `src/calc/SapControlTech/CalcMinimalPlannedDate.cpp` — Phase 1.
- `src/calc/SapControlTech/CalcActualDateCompleteEntryOperation.cpp` / `.hpp` — Phase 2 collection.
- `src/calc/SapControlTech/CalcMinimalDate.cpp` — Phase 2 aggregation + status.
- `src/db/db.cpp` — `fetchInitialDataRaw` no longer selects the dropped columns.
- `src/utils/DebugLogger.hpp` — log `actual_input_dates` instead of the two scalars.
- New: `src/utils/InputAggregation.hpp` (or similar) — pure helpers.
- New: `tests/` — doctest harness + cases.
- New: `migrations/` — forward + rollback SQL.
