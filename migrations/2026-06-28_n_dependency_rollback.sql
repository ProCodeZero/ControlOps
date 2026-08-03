-- Rollback migration: возврат к схеме с двумя слотами зависимостей.
-- Первый элемент списка возвращается в input_*, второй — в alternative_*.
-- ПОТЕРИ: зависимости с третьей и далее в старую схему не помещаются — она
-- рассчитана ровно на две. Полное восстановление — из таблицы
-- static_initial_data_control_operations_backup_20260628.
BEGIN;

-- Предупреждение: сколько строк потеряют зависимости (третью и далее).
DO $$
DECLARE lossy integer;
BEGIN
    SELECT count(*) INTO lossy
    FROM static_initial_data_control_operations
    WHERE btrim(split_part(coalesce(input_operation_order, ''), ',', 3)) <> '';

    IF lossy > 0 THEN
        RAISE WARNING 'Строк с 3+ зависимостями: % — третья и последующие будут потеряны (старая схема их не вмещает)', lossy;
    END IF;
END $$;

ALTER TABLE static_initial_data_control_operations
    ADD COLUMN IF NOT EXISTS alternative_operation_order integer,
    ADD COLUMN IF NOT EXISTS alternative_deadline        integer;

-- Второй элемент списка -> альтернативная зависимость. Делаем ДО смены типа
-- input_*, пока списки ещё доступны как текст.
UPDATE static_initial_data_control_operations
SET alternative_operation_order = NULLIF(btrim(split_part(input_operation_order, ',', 2)), '')::integer,
    alternative_deadline        = NULLIF(btrim(split_part(input_deadline,        ',', 2)), '')::integer;

ALTER TABLE static_initial_data_control_operations
    ALTER COLUMN input_operation_order TYPE integer
        USING NULLIF(btrim(split_part(input_operation_order, ',', 1)), '')::integer,
    ALTER COLUMN input_deadline TYPE integer
        USING NULLIF(btrim(split_part(input_deadline, ',', 1)), '')::integer;

COMMIT;
