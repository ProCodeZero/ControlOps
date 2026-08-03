"""Оффлайн-проверки загрузчика: разбор Excel и все валидации без подключения к БД.

Запуск:
    py scripts/test_load_init_data.py <путь к "инит дата ... .xlsx">

Если файл не передан, гоняются только тесты на синтетических данных.
"""

import sys
from datetime import date
from pathlib import Path

for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, "reconfigure"):
        stream.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, str(Path(__file__).resolve().parent))

from load_init_data import (  # noqa: E402
    REGION_COLUMNS,
    LoadError,
    drop_duplicates,
    normalize_list_cell,
    parse_int_list,
    read_excel,
    resolve_ids,
    to_date,
)

failures = 0


def check(condition, label):
    global failures
    if condition:
        print(f"[PASS] {label}")
    else:
        failures += 1
        print(f"[FAIL] {label}")


def check_raises(fn, label):
    global failures
    try:
        fn()
    except LoadError:
        print(f"[PASS] {label}")
        return
    failures += 1
    print(f"[FAIL] {label} — исключение не выброшено")


# --------------------------------------------------------------------------- #
# Нормализация ячеек со списками
# --------------------------------------------------------------------------- #

check(normalize_list_cell("90, 100") == "90, 100", "список остаётся списком")
check(normalize_list_cell(90) == "90", "одиночное число -> строка")
check(normalize_list_cell("  4 , 4 ,") == "4, 4", "пробелы и висячая запятая убираются")
check(normalize_list_cell(None) is None, "пустая ячейка -> None")
check(normalize_list_cell("   ") is None, "пробелы -> None")

check(parse_int_list("23, 17") == [23, 17], "разбор списка в числа")
check(parse_int_list(None) == [], "None -> пустой список")
check_raises(lambda: parse_int_list("1, x"), "нечисловое значение бросает LoadError")

check(to_date(None) is None, "пустая дата -> None")
check_raises(lambda: to_date("не дата"), "мусор вместо даты бросает LoadError")


# --------------------------------------------------------------------------- #
# Отсев дубликатов
# --------------------------------------------------------------------------- #

def make_record(row, culture, material, order, season="ZP", year=2026):
    return {
        "excel_row": row, "culture_id": culture, "t_material": material,
        "season": season, "input_operation_order": None, "input_deadline": None,
        "noinput_deadline": 5, "order": order, "year": year,
        "region_dates": {column: None for column in REGION_COLUMNS},
    }


same_material = [
    make_record(10, 11, "T001", 50),
    make_record(20, 11, "T001", 360),   # тот же t_material -> дубликат ключа CRTYS
]
check(len(drop_duplicates(same_material, strict=False)) == 1, "дубликат t_material отсеивается")
check_raises(lambda: drop_duplicates(same_material, strict=True), "--strict прерывает загрузку")

same_order = [
    make_record(10, 11, "T001", 50),
    make_record(20, 11, "T002", 50),    # тот же order -> дубликат ключа Order4
]
check(len(drop_duplicates(same_order, strict=False)) == 1, "дубликат order отсеивается")

distinct = [
    make_record(10, 11, "T001", 50),
    make_record(20, 11, "T002", 60),
    make_record(30, 12, "T001", 50),    # другая культура — не дубликат
]
check(len(drop_duplicates(distinct, strict=False)) == 3, "разные ключи сохраняются")


# --------------------------------------------------------------------------- #
# Разворот регионов
# --------------------------------------------------------------------------- #

record = make_record(10, 11, "T001", 50)
record["region_dates"]["regionbels"] = date(2026, 3, 23)
rows = resolve_ids([record], {"T001": 777}, {name: i + 1 for i, name in enumerate(REGION_COLUMNS.values())})
check(len(rows) == len(REGION_COLUMNS), "одна операция -> строка на каждый регион")
check(all(r[1] == 777 for r in rows), "t_material_id подставлен")
check(sum(1 for r in rows if r[4] == date(2026, 3, 23)) == 1, "дата попала только в свой регион")
check(sum(1 for r in rows if r[4] is None) == len(REGION_COLUMNS) - 1, "остальные регионы без даты")

check_raises(
    lambda: resolve_ids([record], {}, {name: 1 for name in REGION_COLUMNS.values()}),
    "неизвестный t_material бросает LoadError",
)
check_raises(
    lambda: resolve_ids([record], {"T001": 777}, {}),
    "отсутствующий регион бросает LoadError",
)


# --------------------------------------------------------------------------- #
# Реальный файл (если передан)
# --------------------------------------------------------------------------- #

if len(sys.argv) > 1:
    path = Path(sys.argv[1])
    print(f"\n--- разбор реального файла: {path.name} ---")
    records = read_excel(path)
    check(len(records) > 0, f"файл разобран, строк: {len(records)}")

    multi = [r for r in records if len(parse_int_list(r["input_operation_order"])) > 1]
    print(f"       строк с несколькими зависимостями: {len(multi)}")
    if multi:
        sample = multi[0]
        print(
            f"       пример: culture={sample['culture_id']} tm={sample['t_material']} "
            f"deps='{sample['input_operation_order']}' deadlines='{sample['input_deadline']}'"
        )
        check(
            len(parse_int_list(sample["input_operation_order"]))
            == len(parse_int_list(sample["input_deadline"])),
            "количество зависимостей совпадает с количеством сроков",
        )

    kept = drop_duplicates(records, strict=False)
    print(f"       после отсева дубликатов: {len(kept)} из {len(records)}")

    fake_materials = {r["t_material"]: i + 1 for i, r in enumerate(kept)}
    fake_regions = {name: i + 1 for i, name in enumerate(REGION_COLUMNS.values())}
    expanded = resolve_ids(kept, fake_materials, fake_regions)
    check(
        len(expanded) == len(kept) * len(REGION_COLUMNS),
        f"развёрнуто в {len(expanded)} строк для БД",
    )
    dated = sum(1 for row in expanded if row[4] is not None)
    print(f"       из них с заполненной region_date: {dated}")

print()
if failures:
    print(f"ТЕСТЫ ПРОВАЛЕНЫ: {failures}")
    sys.exit(1)
print("ALL TESTS PASSED")
