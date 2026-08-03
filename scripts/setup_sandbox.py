"""Готовит локальную песочницу для сквозного прогона.

Создаёт таблицы по sandbox_schema.sql и заливает справочники и исходные
данные SAP из CSV-дампов. Справочник операций (static_initial_data_control_operations)
остаётся пустым — его заполняет load_init_data.py из Excel.

Запуск:
    py scripts/setup_sandbox.py --csv-dir "C:/.../db-tables-data"

ВНИМАНИЕ: скрипт делает DROP TABLE. Только для локальной песочницы.
"""

import argparse
import asyncio
import logging
import sys
from pathlib import Path

import asyncpg
import yaml

for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, "reconfigure"):
        stream.reconfigure(encoding="utf-8", errors="replace")

logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
logger = logging.getLogger(__name__)

# Таблица -> (маска имени CSV, колонки в порядке следования в файле)
CSV_TABLES = [
    ("sap_regions", "sap_regions_*.csv", ["id", "name"]),
    ("sap_operations", "sap_operations_*.csv", ["t_material", "operation_name", "id"]),
    (
        "sap_control_operations_source",
        "sap_control_operations_source_*.csv",
        [
            "id", "culture_id", "t_material_id", "region_id", "pu_id", "higher_tm",
            "season", "calendar_day", "planned_volume", "actual_volume", "year",
        ],
    ),
]


def load_db_config(path):
    with open(path, "r", encoding="utf-8") as handle:
        config = yaml.safe_load(handle)
    missing = [k for k in ("host", "port", "user", "password", "dbname") if k not in config]
    if missing:
        raise SystemExit(f"в {path} нет полей: {', '.join(missing)}")
    return config


def find_csv(csv_dir, pattern):
    matches = sorted(csv_dir.glob(pattern))
    if not matches:
        raise SystemExit(f"не найден CSV по маске {pattern} в {csv_dir}")
    return matches[-1]  # самый свежий по имени (в имени дата выгрузки)


async def run(args):
    config = load_db_config(args.config)
    schema_sql = Path(args.schema).read_text(encoding="utf-8")
    csv_dir = Path(args.csv_dir)
    if not csv_dir.is_dir():
        raise SystemExit(f"нет каталога с CSV: {csv_dir}")

    connection = await asyncpg.connect(
        host=config["host"], port=config["port"], user=config["user"],
        password=config["password"], database=config["dbname"],
    )
    try:
        logger.info("База: %s@%s:%s/%s", config["user"], config["host"],
                    config["port"], config["dbname"])

        logger.info("Создаю таблицы (DROP + CREATE)...")
        await connection.execute(schema_sql)

        for table, pattern, columns in CSV_TABLES:
            path = find_csv(csv_dir, pattern)
            size_mb = path.stat().st_size / 1024 / 1024
            logger.info("Загружаю %s <- %s (%.1f МБ)...", table, path.name, size_mb)
            await connection.copy_to_table(
                table,
                source=str(path),
                columns=columns,
                format="csv",
                header=True,
                encoding="utf-8",
            )
            count = await connection.fetchval(f"SELECT count(*) FROM {table}")
            logger.info("  %s: %d строк", table, count)

        logger.info("Песочница готова. Дальше:")
        logger.info("  py scripts/load_init_data.py \"<путь к новому Excel>\"")
    finally:
        await connection.close()


def main():
    parser = argparse.ArgumentParser(description="Подготовка локальной песочницы")
    parser.add_argument("--csv-dir", required=True, help="каталог с CSV-дампами")
    parser.add_argument("--config", default="config/db_config.yml")
    parser.add_argument("--schema", default="scripts/sandbox_schema.sql")
    args = parser.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
