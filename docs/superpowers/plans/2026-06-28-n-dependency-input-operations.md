# N-dependency Input Operations — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generalize the hard-coded two-slot input/alternative dependency model to an arbitrary list of input operations parsed from comma-separated init-data columns, applying `max` aggregation in both calculation phases.

**Architecture:** Extract pure, std-only helpers (`parseInputOperations`, templated `aggregateMax`/`aggregateMin`) into `src/utils/InputAggregation.{hpp,cpp}` so they can be unit-tested offline with MinGW `g++`. The production data structures and calc passes consume those helpers. Only `static_initial_data_control_operations` gets a DB migration; the output table is unchanged.

**Tech Stack:** C++17, boost::gregorian (production only), libpqxx, CMake. Tests use a tiny offline header-only harness (`tests/mini_test.hpp`) built directly with `C:\mingw64\bin\g++` — no boost/pqxx/vcpkg required.

**Spec:** `docs/superpowers/specs/2026-06-28-n-dependency-input-operations-design.md`

---

## File Structure

| File | Responsibility | Action |
| --- | --- | --- |
| `src/utils/InputAggregation.hpp` | `InputOperation` struct, `parseInputOperations` decl, templated `aggregateMax`/`aggregateMin` | Create |
| `src/utils/InputAggregation.cpp` | `parseInputOperations` + `splitInts`/`trim` impl | Create |
| `tests/mini_test.hpp` | Offline test harness (macros + runner) | Create |
| `tests/test_main.cpp` | Test entry point | Create |
| `tests/test_input_aggregation.cpp` | Parse + aggregation test cases | Create |
| `src/data_struct/InitialData.hpp` | Replace 4 scalar fields with `input_operations` | Modify |
| `src/data_struct/InitialData.cpp` | Parse TEXT columns; update `Print()` | Modify |
| `src/data_struct/SapData.hpp` | Replace 2 scalar dates with `actual_input_dates` | Modify |
| `src/data_struct/SapData.cpp` | Update `Print()` | Modify |
| `src/db/db.cpp` | Drop `alternative_*` from `fetchInitialDataRaw` SELECT | Modify |
| `src/calc/SapControlTech/CalcMinimalPlannedDate.cpp` | Phase 1 rewrite | Modify |
| `src/calc/SapControlTech/CalcActualDateCompleteEntryOperation.hpp/.cpp` | Phase 2 collection (`calcActualInputDates`) | Modify |
| `src/calc/SapControlTech/CalcMinimalDate.cpp` | Phase 2 aggregation + `setIsActual` | Modify |
| `src/calc/SapControlTech/SapControlTech.cpp` | Update Phase 2 call site | Modify |
| `src/utils/DebugLogger.hpp` | Log `actual_input_dates` | Modify |
| `CMakeLists.txt` | Replace Catch2 test block with offline harness target | Modify |
| `migrations/2026-06-28_n_dependency_forward.sql` | Forward migration | Create |
| `migrations/2026-06-28_n_dependency_rollback.sql` | Rollback migration | Create |

**Local test build command (used throughout Tasks 1-3):**

```bash
cd "C:/Users/achuv/c++task/ControlOps" && mkdir -p build_tests && \
/c/mingw64/bin/g++ -std=c++17 -Wall -Wextra -I src -I tests \
  tests/test_main.cpp tests/test_input_aggregation.cpp src/utils/InputAggregation.cpp \
  -o build_tests/tests.exe && ./build_tests/tests.exe
```

---

## Task 1: Offline test harness + helper skeleton

**Files:**
- Create: `tests/mini_test.hpp`
- Create: `tests/test_main.cpp`
- Create: `tests/test_input_aggregation.cpp`
- Create: `src/utils/InputAggregation.hpp`
- Create: `src/utils/InputAggregation.cpp`

- [ ] **Step 1: Create the test harness** `tests/mini_test.hpp`

```cpp
#pragma once
#include <iostream>
#include <vector>
#include <functional>
#include <string>

struct MiniTestCase { std::string name; std::function<void()> fn; };

inline std::vector<MiniTestCase>& miniRegistry() { static std::vector<MiniTestCase> r; return r; }
inline int& miniFailures() { static int f = 0; return f; }

struct MiniRegistrar {
    MiniRegistrar(const std::string& n, std::function<void()> fn) { miniRegistry().push_back({n, fn}); }
};

#define TEST_CASE(name)                                   \
    static void name();                                   \
    static MiniRegistrar mini_reg_##name(#name, name);    \
    static void name()

#define CHECK(cond)                                                              \
    do { if (!(cond)) { ++miniFailures();                                        \
        std::cerr << "  CHECK failed: " #cond " (" << __FILE__ << ":"            \
                  << __LINE__ << ")\n"; } } while (0)

#define CHECK_THROWS(expr)                                                       \
    do { bool threw = false; try { (void)(expr); } catch (...) { threw = true; } \
        if (!threw) { ++miniFailures();                                          \
            std::cerr << "  CHECK_THROWS failed: " #expr " did not throw\n"; }   \
    } while (0)

inline int runAllTests() {
    for (auto& tc : miniRegistry()) {
        int before = miniFailures();
        tc.fn();
        std::cout << (miniFailures() == before ? "[PASS] " : "[FAIL] ") << tc.name << "\n";
    }
    std::cout << (miniFailures() == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return miniFailures() == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Create the entry point** `tests/test_main.cpp`

```cpp
#include "mini_test.hpp"
int main() { return runAllTests(); }
```

- [ ] **Step 3: Create the helper header** `src/utils/InputAggregation.hpp`

```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>

struct InputOperation {
    int  operation_order;
    int  deadline;
    bool is_alternative;   // false for index 0, true for the rest
};

// Parse parallel comma-separated lists into input operations.
// Empty strings -> empty vector. Throws std::runtime_error on count mismatch,
// std::invalid_argument on non-integer tokens.
std::vector<InputOperation>
parseInputOperations(const std::string& orders, const std::string& deadlines);

// Max over the present (engaged) optionals. Returns std::nullopt if none present.
template <typename T>
std::optional<T> aggregateMax(const std::vector<std::optional<T>>& xs) {
    std::optional<T> best;
    for (const auto& x : xs) {
        if (!x) continue;
        if (!best || *best < *x) best = x;
    }
    return best;
}

// Min over the present (engaged) optionals. Returns std::nullopt if none present.
template <typename T>
std::optional<T> aggregateMin(const std::vector<std::optional<T>>& xs) {
    std::optional<T> best;
    for (const auto& x : xs) {
        if (!x) continue;
        if (!best || *x < *best) best = x;
    }
    return best;
}
```

- [ ] **Step 4: Create a stub impl** `src/utils/InputAggregation.cpp`

```cpp
#include <utils/InputAggregation.hpp>

std::vector<InputOperation>
parseInputOperations(const std::string& /*orders*/, const std::string& /*deadlines*/) {
    return {};
}
```

- [ ] **Step 5: Create a smoke test** `tests/test_input_aggregation.cpp`

```cpp
#include "mini_test.hpp"
#include <utils/InputAggregation.hpp>

TEST_CASE(harness_smoke) {
    CHECK(1 + 1 == 2);
}
```

- [ ] **Step 6: Build and run**

Run:
```bash
cd "C:/Users/achuv/c++task/ControlOps" && mkdir -p build_tests && \
/c/mingw64/bin/g++ -std=c++17 -Wall -Wextra -I src -I tests \
  tests/test_main.cpp tests/test_input_aggregation.cpp src/utils/InputAggregation.cpp \
  -o build_tests/tests.exe && ./build_tests/tests.exe
```
Expected: compiles; output contains `[PASS] harness_smoke` and `ALL TESTS PASSED`.

- [ ] **Step 7: Commit**

```bash
git add tests/ src/utils/InputAggregation.hpp src/utils/InputAggregation.cpp
git commit -m "test: offline harness + InputAggregation skeleton"
```

---

## Task 2: `parseInputOperations` (TDD)

**Files:**
- Modify: `tests/test_input_aggregation.cpp`
- Modify: `src/utils/InputAggregation.cpp`

- [ ] **Step 1: Write failing tests** — append to `tests/test_input_aggregation.cpp`

```cpp
TEST_CASE(parse_multi_marks_first_primary_rest_alternative) {
    auto v = parseInputOperations("23, 17", "1, 5");
    CHECK(v.size() == 2);
    CHECK(v[0].operation_order == 23);
    CHECK(v[0].deadline == 1);
    CHECK(v[0].is_alternative == false);
    CHECK(v[1].operation_order == 17);
    CHECK(v[1].deadline == 5);
    CHECK(v[1].is_alternative == true);
}

TEST_CASE(parse_single_value) {
    auto v = parseInputOperations("90", "100");
    CHECK(v.size() == 1);
    CHECK(v[0].operation_order == 90);
    CHECK(v[0].deadline == 100);
    CHECK(v[0].is_alternative == false);
}

TEST_CASE(parse_empty_returns_empty) {
    CHECK(parseInputOperations("", "").empty());
}

TEST_CASE(parse_tolerates_whitespace_and_trailing_comma) {
    auto v = parseInputOperations("  4 , 4 ,", " 60 , 260 ");
    CHECK(v.size() == 2);
    CHECK(v[0].operation_order == 4);
    CHECK(v[1].deadline == 260);
}

TEST_CASE(parse_count_mismatch_throws) {
    CHECK_THROWS(parseInputOperations("1, 2, 3", "10, 20"));
}

TEST_CASE(parse_non_integer_throws) {
    CHECK_THROWS(parseInputOperations("1, x", "10, 20"));
}
```

- [ ] **Step 2: Run to verify failure**

Run the build command from Task 1 Step 6.
Expected: `[FAIL]` lines for the new parse tests (stub returns empty), `TESTS FAILED`.

- [ ] **Step 3: Implement** — replace the body of `src/utils/InputAggregation.cpp`

```cpp
#include <utils/InputAggregation.hpp>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<int> splitInts(const std::string& csv) {
    std::vector<int> out;
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        std::string t = trim(token);
        if (t.empty()) continue;            // skip blanks / trailing comma
        size_t pos = 0;
        int value = std::stoi(t, &pos);     // throws std::invalid_argument on junk
        if (pos != t.size())
            throw std::invalid_argument("Non-integer token in list: '" + t + "'");
        out.push_back(value);
    }
    return out;
}

} // namespace

std::vector<InputOperation>
parseInputOperations(const std::string& orders, const std::string& deadlines) {
    std::vector<int> ord = splitInts(orders);
    std::vector<int> dl  = splitInts(deadlines);
    if (ord.size() != dl.size())
        throw std::runtime_error(
            "input_operation_order/input_deadline count mismatch: "
            + std::to_string(ord.size()) + " vs " + std::to_string(dl.size()));

    std::vector<InputOperation> result;
    result.reserve(ord.size());
    for (size_t i = 0; i < ord.size(); ++i)
        result.push_back(InputOperation{ ord[i], dl[i], i != 0 });
    return result;
}
```

- [ ] **Step 4: Run to verify pass**

Run the build command from Task 1 Step 6.
Expected: all parse tests `[PASS]`, `ALL TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add tests/test_input_aggregation.cpp src/utils/InputAggregation.cpp
git commit -m "feat: parseInputOperations for comma-separated dependency lists"
```

---

## Task 3: `aggregateMax` / `aggregateMin` (TDD)

**Files:**
- Modify: `tests/test_input_aggregation.cpp`

(The templates are already defined in the header from Task 1; these tests lock their behavior. Dates are comparable ordinals, so `int` is a faithful stand-in for `boost::gregorian::date`.)

- [ ] **Step 1: Write failing tests** — append to `tests/test_input_aggregation.cpp`

```cpp
#include <optional>

TEST_CASE(aggregate_max_picks_largest_present) {
    std::vector<std::optional<int>> xs{ std::optional<int>(1), std::nullopt,
                                        std::optional<int>(5), std::optional<int>(3) };
    auto r = aggregateMax(xs);
    CHECK(r.has_value());
    CHECK(*r == 5);
}

TEST_CASE(aggregate_max_all_empty_is_nullopt) {
    std::vector<std::optional<int>> xs{ std::nullopt, std::nullopt };
    CHECK(!aggregateMax(xs).has_value());
}

TEST_CASE(aggregate_max_empty_vector_is_nullopt) {
    std::vector<std::optional<int>> xs;
    CHECK(!aggregateMax(xs).has_value());
}

TEST_CASE(aggregate_min_picks_smallest_present) {
    std::vector<std::optional<int>> xs{ std::optional<int>(7), std::nullopt,
                                        std::optional<int>(2), std::optional<int>(9) };
    auto r = aggregateMin(xs);
    CHECK(r.has_value());
    CHECK(*r == 2);
}
```

- [ ] **Step 2: Run to verify they pass immediately**

The aggregation templates already exist (header), so these tests should pass on first run — they exist to lock behavior and catch regressions. Run the build command from Task 1 Step 6.
Expected: all aggregate tests `[PASS]`, `ALL TESTS PASSED`. (If any fail, fix the templates in `InputAggregation.hpp`.)

- [ ] **Step 3: Commit**

```bash
git add tests/test_input_aggregation.cpp
git commit -m "test: lock aggregateMax/aggregateMin behavior"
```

---

## Task 4: Data model — `InitialData` (model + parsing)

**Files:**
- Modify: `src/data_struct/InitialData.hpp`
- Modify: `src/data_struct/InitialData.cpp`

> Cannot compile locally (needs boost/pqxx); verified by CI build later. Make edits exactly as shown.

- [ ] **Step 1: Update the header** `src/data_struct/InitialData.hpp`

Add the include near the top (after the existing includes, before `struct KeyOrder4`):

```cpp
#include <utils/InputAggregation.hpp>
```

In `struct PlannedDates`, remove `input_date` and `alternative_date` so it reads:

```cpp
struct PlannedDates
{
    std::optional<boost::gregorian::date> planned_date;
    std::optional<boost::gregorian::date> minimal_planned_date;
};
```

In `struct InitialDataFrame`, replace these four lines:

```cpp
    std::optional<int> input_operation_order;
    std::optional<int> alternative_operation_order;
    std::optional<int> input_deadline;
    std::optional<int> alternative_deadline;
```

with:

```cpp
    std::vector<InputOperation> input_operations;
```

(Leave `region_date`, `noinput_deadline`, `order`, `year`, `planned_dates` as-is.)

- [ ] **Step 2: Update the constructor parsing** `src/data_struct/InitialData.cpp`

In `InitialData::InitialData`, add a string reader lambda next to `optInt`:

```cpp
    auto strOr = [](const auto& r, const char* col) -> std::string {
        return r[col].is_null() ? std::string()
                                : r[col].template as<std::string>();
    };
```

Replace these four assignments:

```cpp
        frame.input_operation_order = optInt(row, "input_operation_order");
        frame.alternative_operation_order = optInt(row, "alternative_operation_order");
        frame.input_deadline = optInt(row, "input_deadline");
        frame.alternative_deadline = optInt(row, "alternative_deadline");
```

with:

```cpp
        frame.input_operations = parseInputOperations(
            strOr(row, "input_operation_order"),
            strOr(row, "input_deadline"));
```

- [ ] **Step 3: Update `Print()`** in `src/data_struct/InitialData.cpp`

Replace these four lines:

```cpp
            std::cout << "input_operation_order: " << row.input_operation_order << std::endl;
            std::cout << "alternative_operation_order: " << row.alternative_operation_order << std::endl;
            std::cout << "input_deadline: " << row.input_deadline << std::endl;
            std::cout << "alternative_deadline: " << row.alternative_deadline << std::endl;
```

with:

```cpp
            std::cout << "input_operations: ";
            for (const auto& dep : row.input_operations)
                std::cout << "(order=" << dep.operation_order
                          << ", deadline=" << dep.deadline
                          << ", alt=" << dep.is_alternative << ") ";
            std::cout << std::endl;
```

Also remove the now-deleted planned-dates prints (`input_date`, `alternative_date`) in `Print()`:

```cpp
            std::cout << "input_date: " << row.planned_dates.input_date << std::endl;
            std::cout << "alternative_date: " << row.planned_dates.alternative_date << std::endl;
```

(Delete both lines.)

- [ ] **Step 4: Commit**

```bash
git add src/data_struct/InitialData.hpp src/data_struct/InitialData.cpp
git commit -m "refactor: InitialData uses input_operations list"
```

---

## Task 5: DB query — drop alternative columns

**Files:**
- Modify: `src/db/db.cpp`

- [ ] **Step 1: Edit `fetchInitialDataRaw`**

In the `SELECT` inside `fetchInitialDataRaw()`, remove these two lines:

```cpp
            alternative_operation_order,
            alternative_deadline,
```

The remaining column list keeps `input_operation_order` and `input_deadline` (now TEXT in the DB). Final query columns:

```sql
        SELECT
            culture_id,
            t_material_id,
            region_id,
            season,
            region_date,
            input_operation_order,
            input_deadline,
            noinput_deadline,
            "order",
            year
        FROM static_initial_data_control_operations ORDER BY "order" ASC, culture_id ASC, region_id ASC
```

- [ ] **Step 2: Commit**

```bash
git add src/db/db.cpp
git commit -m "refactor: drop alternative_* from init-data query"
```

---

## Task 6: Data model — `SapData` (`actual_input_dates`)

**Files:**
- Modify: `src/data_struct/SapData.hpp`
- Modify: `src/data_struct/SapData.cpp`

- [ ] **Step 1: Edit `SapDataFrame`** in `src/data_struct/SapData.hpp`

Replace these two lines:

```cpp
	std::optional<boost::gregorian::date> actual_input_date;
	std::optional<boost::gregorian::date> actual_alternative_date;
```

with:

```cpp
	std::vector<boost::gregorian::date> actual_input_dates;
```

- [ ] **Step 2: Update `Print()`** in `src/data_struct/SapData.cpp`

Replace these two lines (around 204-205):

```cpp
                std::cout << "actual_input_date: " << frame.actual_input_date << std::endl;
                std::cout << "actual_alternative_date: " << frame.actual_alternative_date << std::endl;
```

with:

```cpp
                std::cout << "actual_input_dates: ";
                for (const auto& d : frame.actual_input_dates)
                    std::cout << boost::gregorian::to_simple_string(d) << " ";
                std::cout << std::endl;
```

- [ ] **Step 3: Commit**

```bash
git add src/data_struct/SapData.hpp src/data_struct/SapData.cpp
git commit -m "refactor: SapDataFrame uses actual_input_dates list"
```

---

## Task 7: Phase 1 — `CalcMinimalPlannedDate`

**Files:**
- Modify: `src/calc/SapControlTech/CalcMinimalPlannedDate.cpp`

- [ ] **Step 1: Replace the file contents** with:

```cpp
#include <calc/SapControlTech/CalcMinimalPlannedDate.hpp>
#include <utils/utilsBoostDate.hpp>
#include <utils/InputAggregation.hpp>

void calcPlannedDate(InitialData& InitData, int row)
{
    if (InitData.data[row].noinput_deadline.has_value() && InitData.data[row].region_date.has_value())
    {
        InitData.data[row].planned_dates.planned_date =
            InitData.data[row].region_date.value()
            + boost::gregorian::days(InitData.data[row].noinput_deadline.value());
    }
    else
    {
        InitData.data[row].planned_dates.planned_date = std::nullopt;
    }
}

void calcMinimalPlannedDate(InitialData& InitData)
{
    for (int row = 0; row < InitData.Size(); row++)
    {
        calcPlannedDate(InitData, row);

        InitialDataFrame& frame = InitData.data[row];

        // Candidates start with this operation's own region-based planned date.
        std::vector<std::optional<boost::gregorian::date>> candidates;
        candidates.push_back(frame.planned_dates.planned_date);

        // Add the planned date implied by each input dependency.
        for (const auto& dep : frame.input_operations)
        {
            KeyOrder4 key{ frame.culture_id, frame.region_id, dep.operation_order, frame.year };
            auto it = InitData.order_index_map.find(key);
            if (it == InitData.order_index_map.end())
                continue;

            const InitialDataFrame& parent = InitData.data[it->second];
            if (parent.planned_dates.minimal_planned_date.has_value())
            {
                candidates.push_back(
                    parent.planned_dates.minimal_planned_date.value()
                    + boost::gregorian::days(dep.deadline));
            }
        }

        // Literal task.md rule: max over the region-based date and all dependency dates.
        frame.planned_dates.minimal_planned_date = aggregateMax(candidates);
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add src/calc/SapControlTech/CalcMinimalPlannedDate.cpp
git commit -m "feat: Phase 1 minimal_planned_date = max over N dependencies"
```

---

## Task 8: Phase 2 — collect actual input dates

**Files:**
- Modify: `src/calc/SapControlTech/CalcActualDateCompleteEntryOperation.hpp`
- Modify: `src/calc/SapControlTech/CalcActualDateCompleteEntryOperation.cpp`
- Modify: `src/calc/SapControlTech/SapControlTech.cpp`

- [ ] **Step 1: Replace the header** `CalcActualDateCompleteEntryOperation.hpp`

```cpp
#pragma once
#include <data_struct/SapData.hpp>
#include <data_struct/InitialData.hpp>

// For each SAP frame, compute the actual date implied by every input dependency
// (parent operation actual_date + dependency deadline) and store them on the frame.
void calcActualInputDates(YearSlices& uniqueSlices, const InitialData& initData);
```

- [ ] **Step 2: Replace the implementation** `CalcActualDateCompleteEntryOperation.cpp`

```cpp
#include <calc/SapControlTech/CalcActualDateCompleteEntryOperation.hpp>
#include <utils/utilsBoostDate.hpp>
#include <utils/DebugLogger.hpp>

void calcActualInputDates(YearSlices& uniqueSlices, const InitialData& initData)
{
    for (auto& [year, higherTmMap] : uniqueSlices)
    {
        for (auto& [higher_tm, sliceList] : higherTmMap)
        {
            for (auto& slice : sliceList)
            {
                for (auto& frame : slice)
                {
                    logFrameState("CalcActualInputDates_BEFORE", frame);
                    frame.actual_input_dates.clear();

                    KeyCRTYS5 key{
                        frame.culture_id, frame.region_id, frame.t_material_id,
                        frame.year, frame.season
                    };

                    auto it = initData.CRTYS_index_map.find(key);
                    if (it == initData.CRTYS_index_map.end())
                    {
                        logFrameState("CalcActualInputDates_AFTER", frame);
                        continue;
                    }

                    const InitialDataFrame& matchInitData = initData.data[it->second];

                    for (const auto& dep : matchInitData.input_operations)
                    {
                        KeyOrder4 parentKey{
                            matchInitData.culture_id, matchInitData.region_id,
                            dep.operation_order, matchInitData.year
                        };

                        auto parentIt = initData.order_index_map.find(parentKey);
                        if (parentIt == initData.order_index_map.end())
                            continue;

                        const InitialDataFrame& parentInitData = initData.data[parentIt->second];

                        // actual_date of the parent operation, taken from the SAP slices.
                        std::optional<boost::gregorian::date> parentActual;
                        auto& parentFrames = uniqueSlices[year][higher_tm];
                        for (const auto& parentSlice : parentFrames)
                        {
                            for (const auto& candidate : parentSlice)
                            {
                                if (candidate.culture_id == parentInitData.culture_id &&
                                    candidate.region_id == parentInitData.region_id &&
                                    candidate.t_material_id == parentInitData.t_material_id &&
                                    candidate.year == parentInitData.year &&
                                    candidate.season == parentInitData.season)
                                {
                                    parentActual = candidate.actual_date;
                                    break;
                                }
                            }
                            if (parentActual.has_value()) break;
                        }

                        if (parentActual.has_value())
                        {
                            frame.actual_input_dates.push_back(
                                parentActual.value() + boost::gregorian::days(dep.deadline));
                        }
                    }

                    logFrameState("CalcActualInputDates_AFTER", frame);
                }
            }
        }
    }
}
```

- [ ] **Step 3: Update the call site** in `src/calc/SapControlTech/SapControlTech.cpp`

Replace these two lines:

```cpp
        calcInputDate(sapDataUniqueTMaterialSlices, initialData);

        calcAlternativeDate(sapDataUniqueTMaterialSlices, initialData);
```

with:

```cpp
        calcActualInputDates(sapDataUniqueTMaterialSlices, initialData);
```

- [ ] **Step 4: Commit**

```bash
git add src/calc/SapControlTech/CalcActualDateCompleteEntryOperation.hpp \
        src/calc/SapControlTech/CalcActualDateCompleteEntryOperation.cpp \
        src/calc/SapControlTech/SapControlTech.cpp
git commit -m "feat: Phase 2 collects actual dates for N dependencies"
```

---

## Task 9: Phase 2 — aggregate minimal date

**Files:**
- Modify: `src/calc/SapControlTech/CalcMinimalDate.cpp`

- [ ] **Step 1: Replace the file contents** with:

```cpp
// calcMinimalDate adapted to N input dependencies.
#include <calc/SapControlTech/CalcMinimalDate.hpp>
#include <utils/utilsBoostDate.hpp>
#include <utils/InputAggregation.hpp>
#include <utils/DebugLogger.hpp>

using boost::gregorian::date;

#include <string>
#include <optional>
#include <boost/date_time/gregorian/gregorian.hpp>

std::optional<std::string> setStatus(const std::optional<date>& actual_date, const std::optional<date>& minimal_date, const date& today)
{
    if (!minimal_date) return "Операция не отслеживается";
    if (!actual_date)
    {
        if (today < minimal_date.value()) return "Не завершено";
        else return "Просрочено";
    }
    else
    {
        if (*actual_date <= minimal_date.value()) return "Выполнено в срок";
        else return "Выполнено не в срок";
    }
}

std::optional<std::string> setIsActual(const std::optional<std::string>& status, const std::optional<date>& actual_data, bool has_actual_input)
{
    if (!status || *status == "Операция не отслеживается")
        return "Статус отсутствует";
    if (actual_data || has_actual_input)
        return "Актуально";
    else
        return "Ориентировочно";
}

void calcMinimalDate(YearSlices& uniqueSlices, const InitialData& initData)
{
    using namespace boost::gregorian;

    date today = day_clock::local_day();

    for (auto& [year, higherTmMap] : uniqueSlices)
    {
        for (auto& [higher_tm, sliceList] : higherTmMap)
        {
            for (auto& slice : sliceList)
            {
                for (auto& frame : slice)
                {
                    logFrameState("CalcMinimalDate_BEFORE", frame);

                    KeyCRTYS5 key{
                        frame.culture_id, frame.region_id, frame.t_material_id,
                        frame.year, frame.season
                    };
                    auto it = initData.CRTYS_index_map.find(key);
                    if (it == initData.CRTYS_index_map.end())
                    {
                        frame.minimal_date = std::nullopt;
                        frame.status = "Операция не отслеживается";
                        frame.is_actual = "Статус отсутствует";
                        continue;
                    }

                    const InitialDataFrame& initFrame = initData.data[it->second];

                    frame.order = initFrame.order;

                    // Compute min_plan_date.
                    std::optional<date> min_plan_date;
                    if (!initFrame.input_operations.empty())
                    {
                        min_plan_date = initFrame.planned_dates.minimal_planned_date;
                    }
                    else if (frame.start_date.has_value())
                    {
                        min_plan_date = frame.start_date.value() + days(initFrame.noinput_deadline.value_or(0));
                    }
                    else
                    {
                        min_plan_date = std::nullopt;
                    }

                    // Literal task.md rule: max over min_plan_date and all actual dependency dates.
                    std::vector<std::optional<date>> candidates;
                    candidates.push_back(min_plan_date);
                    for (const auto& d : frame.actual_input_dates)
                        candidates.push_back(d);

                    frame.minimal_date = aggregateMax(candidates);

                    frame.status = setStatus(frame.actual_date, frame.minimal_date, today);
                    frame.is_actual = setIsActual(frame.status, frame.actual_date,
                                                  !frame.actual_input_dates.empty());

                    logFrameState("CalcMinimalDate_AFTER", frame);
                }
            }
        }
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add src/calc/SapControlTech/CalcMinimalDate.cpp
git commit -m "feat: Phase 2 minimal_date = max over N actual deps + plan date"
```

---

## Task 10: Update DebugLogger

**Files:**
- Modify: `src/utils/DebugLogger.hpp`

- [ ] **Step 1: Replace the logging body**

Replace the `spdlog::info(...)` call inside `logFrameState` so it no longer references `actual_input_date` / `actual_alternative_date`. Replace the whole `if (...) { spdlog::info(...); }` block with:

```cpp
    if (frame.higher_tm == "TM-04-41-35-0008" && frame.year == 2026) {
        std::string actual_inputs;
        for (const auto& d : frame.actual_input_dates)
            actual_inputs += boost::gregorian::to_simple_string(d) + " ";

        spdlog::info(
            "[DEBUG {}] higher_tm={}, year={}, t_material_id={}, order={}, "
            "actual_date={}, start_date={}, is_completed={}, is_started={}, "
            "actual_input_dates=[{}], sawing_date={}, "
            "resawing_date={}, minimal_date={}, status={}",
            step,
            frame.higher_tm,
            frame.year,
            frame.t_material_id,
            frame.order.value_or(-1),
            dateToStr(frame.actual_date),
            dateToStr(frame.start_date),
            frame.is_completed,
            frame.is_started,
            actual_inputs,
            dateToStr(frame.sawing_date),
            dateToStr(frame.resawing_date),
            dateToStr(frame.minimal_date),
            frame.status.value_or("NULL")
        );
    }
```

- [ ] **Step 2: Commit**

```bash
git add src/utils/DebugLogger.hpp
git commit -m "refactor: log actual_input_dates list"
```

---

## Task 11: CMake — wire the offline test target

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Replace the test block**

Replace the entire `if(BUILD_TESTS) ... endif()` block (the one that does `find_package(Catch2 REQUIRED)`) with:

```cmake
# ------------------- Тесты -------------------
if(BUILD_TESTS)
  enable_testing()
  add_executable(tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_input_aggregation.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/InputAggregation.cpp
  )
  target_include_directories(tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
  )
  add_test(NAME unit_tests COMMAND tests)
endif()
```

- [ ] **Step 2: Verify the GLOB still includes the helper for the main target**

`InputAggregation.cpp` lives under `src/utils/`, so the existing `file(GLOB_RECURSE SRC_FILES ...)` already picks it up for the main executable. No change needed. (Sanity check only — no edit.)

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: offline unit-test target (no Catch2 dependency)"
```

---

## Task 12: SQL migrations

**Files:**
- Create: `migrations/2026-06-28_n_dependency_forward.sql`
- Create: `migrations/2026-06-28_n_dependency_rollback.sql`

- [ ] **Step 1: Create the forward migration** `migrations/2026-06-28_n_dependency_forward.sql`

```sql
-- Forward migration: N-dependency input operations.
-- Converts input_operation_order/input_deadline to TEXT (comma-separated lists)
-- and drops the now-unused alternative_* columns.
BEGIN;

-- Safety backup; drop after a successful verified run.
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

- [ ] **Step 2: Create the rollback migration** `migrations/2026-06-28_n_dependency_rollback.sql`

```sql
-- Rollback migration: restore the two-slot schema.
-- NOTE: lossy for input_* — only the FIRST dependency survives the integer cast.
-- For a full restore, reload the old Excel or restore from
-- static_initial_data_control_operations_backup_20260628.
BEGIN;

ALTER TABLE static_initial_data_control_operations
    ADD COLUMN IF NOT EXISTS alternative_operation_order integer,
    ADD COLUMN IF NOT EXISTS alternative_deadline        integer;

ALTER TABLE static_initial_data_control_operations
    ALTER COLUMN input_operation_order TYPE integer
        USING NULLIF(split_part(input_operation_order, ',', 1), '')::integer,
    ALTER COLUMN input_deadline TYPE integer
        USING NULLIF(split_part(input_deadline, ',', 1), '')::integer;

COMMIT;
```

- [ ] **Step 3: Commit**

```bash
git add migrations/
git commit -m "feat: forward + rollback migrations for N-dependency init data"
```

---

## Task 13: Final verification & CI build

**Files:** none (verification only)

- [ ] **Step 1: Re-run the local unit tests**

Run the build command from Task 1 Step 6.
Expected: `ALL TESTS PASSED`.

- [ ] **Step 2: Grep for leftover references to removed fields**

Run:
```bash
cd "C:/Users/achuv/c++task/ControlOps" && \
grep -rn "actual_input_date\b\|actual_alternative_date\|alternative_operation_order\|alternative_deadline\|planned_dates.input_date\|planned_dates.alternative_date\|calcInputDate\|calcAlternativeDate" src/
```
Expected: no matches (empty output). Fix any stragglers and re-commit.

- [ ] **Step 3: Trigger the Windows CI build**

> Pushing is outward-facing — confirm with the user before running. The existing `.github/workflows/build-windows.yml` builds via vcpkg + MSVC and uploads `RusAgroBack_Control_Operations.exe` as an artifact.

```bash
git push origin feature-fixed
```

- [ ] **Step 4: Confirm the CI run is green and download the artifact**

Run:
```bash
gh run list --branch feature-fixed --limit 1
gh run watch    # or: gh run view --log
```
Expected: the "Build Windows Executable" workflow succeeds; `RusAgroBack-Windows` artifact contains the `.exe`.

---

## Self-Review

**Spec coverage:**
- Data model (spec §1) → Tasks 4, 6.
- Parsing (spec §2) → Tasks 1, 2, 4.
- Phase 1 rule (spec §3) → Task 7.
- Phase 2 rules (spec §4) → Tasks 8, 9.
- Behavior changes (spec §5) → encoded as `aggregateMax` in Tasks 7, 9.
- SQL migrations (spec §6) → Task 12.
- Tests (spec §7) → Tasks 1-3, 11.
- Build/branch (spec §8) → Task 13.
- DB query / DebugLogger (spec "Affected files") → Tasks 5, 10.

**Type consistency:** `InputOperation{operation_order, deadline, is_alternative}`, `parseInputOperations(orders, deadlines)`, `aggregateMax/aggregateMin(vector<optional<T>>)`, `input_operations`, `actual_input_dates`, `calcActualInputDates`, `setIsActual(status, actual, bool)` — names match across all tasks.

**Placeholder scan:** none — every code step shows complete content.
