--------------------------------------------------------------------------------
-- ku schema
--------------------------------------------------------------------------------

CREATE SCHEMA ku;


SET search_path TO ku;


CREATE OR REPLACE FUNCTION to_string(f float8) RETURNS text AS $$
    SELECT $1::text;
$$ LANGUAGE SQL IMMUTABLE;


CREATE OR REPLACE FUNCTION to_string(b bool) RETURNS text AS $$
    SELECT $1::text;
$$ LANGUAGE SQL IMMUTABLE;


CREATE OR REPLACE FUNCTION to_string(t timestamp(3)) RETURNS text AS $$
    SELECT to_char($1, 'Dy, DD Mon YYYY HH:MI:SS GMT');
$$ LANGUAGE SQL IMMUTABLE;


CREATE OR REPLACE FUNCTION to_number(t text) RETURNS float8 AS $$
BEGIN
    IF t = '' THEN
        RETURN 0;
    END IF;
    RETURN t::float8;
EXCEPTION
    WHEN invalid_text_representation THEN
        RETURN 'NaN'::float8;
END;
$$ LANGUAGE plpgsql IMMUTABLE;


CREATE OR REPLACE FUNCTION to_number(b bool) RETURNS float8 AS $$
    SELECT $1::int::float8;
$$ LANGUAGE SQL IMMUTABLE;


CREATE OR REPLACE FUNCTION to_number(t timestamp(3)) RETURNS float8 AS $$
    SELECT extract(epoch from $1)*1000;
$$ LANGUAGE SQL IMMUTABLE;


CREATE OR REPLACE FUNCTION to_boolean(f float8) RETURNS bool AS $$
    SELECT CASE WHEN $1 = 0 OR $1 = 'NaN'::float8 THEN false ELSE true END;
$$ LANGUAGE SQL IMMUTABLE;


CREATE OR REPLACE FUNCTION to_boolean(t text) RETURNS bool AS $$
    SELECT CASE WHEN $1 = '' THEN false ELSE true END;
$$ LANGUAGE SQL IMMUTABLE;


CREATE OR REPLACE FUNCTION mod(a float8, b float8) RETURNS float8 AS $$
    SELECT $1 - trunc($1/$2) * $2;
$$ LANGUAGE SQL IMMUTABLE;


CREATE OPERATOR % (
    leftarg = float8,
    rightarg = float8,
    procedure = mod
);


CREATE OR REPLACE FUNCTION eval(t text) RETURNS text AS $$
DECLARE
    r text;
BEGIN
    IF (t LIKE '%::text') OR (t LIKE '%::timestamp(3) without time zone') THEN
        EXECUTE 'SELECT ' || t INTO STRICT r;
        RETURN r;
    END IF;
    RETURN t;
END;
$$ LANGUAGE plpgsql IMMUTABLE;


CREATE TABLE "Empty" ();


CREATE OR REPLACE FUNCTION insert_into_empty(table_name text) RETURNS void AS $$
DECLARE
    row ku."Empty"%ROWTYPE;
BEGIN
    EXECUTE 'INSERT INTO "' || table_name || '" DEFAULT VALUES';
    BEGIN
        EXECUTE 'SELECT * FROM "' || table_name || '"' INTO STRICT row;
    EXCEPTION
        WHEN TOO_MANY_ROWS THEN
            RAISE EXCEPTION 'Empty relation "%" already has a row', table_name;
    END;
END;
$$ LANGUAGE plpgsql VOLATILE;


CREATE OR REPLACE FUNCTION init_schema(schema_name text) RETURNS void AS $$
BEGIN
    BEGIN
        EXECUTE 'CREATE SCHEMA "' || schema_name || '"';
    EXCEPTION
        WHEN DUPLICATE_SCHEMA THEN
            RETURN;
    END;
    EXECUTE 'CREATE OPERATOR "' || schema_name ||
            '".% (leftarg = float8, rightarg = float8, procedure = ku.mod)';
END;
$$ LANGUAGE plpgsql VOLATILE;

--------------------------------------------------------------------------------
-- public schema
--------------------------------------------------------------------------------

CREATE OPERATOR public.% (
    leftarg = float8,
    rightarg = float8,
    procedure = ku.mod
);
