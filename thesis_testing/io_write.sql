EXPLAIN ANALYZE INSERT INTO io_test
SELECT r as id,
        repeat('abcdefghij', 100) as payload       -- ~1KB payload per row
FROM range(1, 10 * 1024 * 1024 + 1) as t(r);    -- 10 million rows

CHECKPOINT;
