
BEGIN;
-- Страховочная копия. Удалить саму таблицу (DROP TABLE ...) после проверки результата. 
CREATE TABLE IF NOT EXISTS static_initial_data_control_operations_backup_20260628 AS
    SELECT * FROM static_initial_data_control_operations;

DO $$
DECLARE broken integer;
BEGIN
    SELECT count(*) INTO broken
    FROM static_initial_data_control_operations
    WHERE (alternative_operation_order IS NULL) <> (alternative_deadline IS NULL);

    IF broken > 0 THEN
        RAISE WARNING 'Строк с неполной парой alternative_operation_order/alternative_deadline: % — они не попадут в список', broken;
    END IF;
END $$;

ALTER TABLE static_initial_data_control_operations
    ALTER COLUMN input_operation_order TYPE text USING input_operation_order::text,
    ALTER COLUMN input_deadline        TYPE text USING input_deadline::text;

UPDATE static_initial_data_control_operations
SET input_operation_order = input_operation_order || ', ' || alternative_operation_order::text,
    input_deadline        = input_deadline        || ', ' || alternative_deadline::text
WHERE alternative_operation_order IS NOT NULL
  AND alternative_deadline        IS NOT NULL
  AND input_operation_order       IS NOT NULL
  AND input_deadline              IS NOT NULL;

ALTER TABLE static_initial_data_control_operations
    DROP COLUMN IF EXISTS alternative_operation_order,
    DROP COLUMN IF EXISTS alternative_deadline;

COMMIT;
