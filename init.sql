-- (c) 2009-2010 by Anton Korenyushkin


CREATE LANGUAGE plpgsql;


DROP SCHEMA IF EXISTS ak CASCADE;
CREATE SCHEMA ak;


CREATE DOMAIN ak.json AS text;


CREATE FUNCTION ak.to_string(f float8) RETURNS text AS $$
    SELECT $1::text;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_string(b bool) RETURNS text AS $$
    SELECT $1::text;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_string(t timestamp(3)) RETURNS text AS $$
    SELECT to_char($1, 'Dy Mon DD YYYY HH24:MI:SS');
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_string(j ak.json) RETURNS text AS $$
    SELECT $1::text;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_number(t text) RETURNS float8 AS $$
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


CREATE FUNCTION ak.to_number(b bool) RETURNS float8 AS $$
    SELECT $1::int::float8;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_number(t timestamp(3)) RETURNS float8 AS $$
    SELECT extract(epoch from $1) * 1000;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_number(j ak.json) RETURNS float8 AS $$
    SELECT ak.to_number($1::text);
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_boolean(t text) RETURNS bool AS $$
    SELECT CASE WHEN $1 = '' THEN false ELSE true END;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_boolean(f float8) RETURNS bool AS $$
    SELECT CASE WHEN $1 = 0 OR $1 = 'NaN'::float8 THEN false ELSE true END;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_boolean(t timestamp(3)) RETURNS bool AS $$
    SELECT true;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.to_boolean(j ak.json) RETURNS bool AS $$
    SELECT true;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ak.mod(a float8, b float8) RETURNS float8 AS $$
    SELECT $1 - trunc($1/$2) * $2;
$$ LANGUAGE SQL IMMUTABLE;


CREATE OPERATOR % (leftarg = float8, rightarg = float8, procedure = ak.mod);


CREATE FUNCTION ak.eval(t text) RETURNS text AS $$
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


CREATE FUNCTION ak.insert_into_empty(table_name text)
    RETURNS void AS
$$
DECLARE
    row RECORD;
BEGIN
    EXECUTE 'INSERT INTO ' || quote_ident(table_name) || ' DEFAULT VALUES';
    BEGIN
        EXECUTE 'SELECT * FROM ' || quote_ident(table_name) INTO STRICT row;
    EXCEPTION
        WHEN TOO_MANY_ROWS THEN
            RAISE EXCEPTION 'Empty relation "%" already has a row', table_name;
    END;
END;
$$ LANGUAGE plpgsql VOLATILE;


CREATE FUNCTION ak.describe_table(
    name text, OUT attname name, OUT typname name, OUT def text)
    RETURNS SETOF RECORD AS
$$
    SELECT attribute.attname, pg_type.typname, ak.eval(attribute.adsrc)
    FROM pg_catalog.pg_type AS pg_type,
         (pg_catalog.pg_attribute LEFT JOIN pg_catalog.pg_attrdef
          ON pg_catalog.pg_attribute.attrelid = pg_catalog.pg_attrdef.adrelid
          AND pg_catalog.pg_attribute.attnum = pg_catalog.pg_attrdef.adnum)
         AS attribute
    WHERE pg_type.oid = attribute.atttypid
    AND attribute.attnum > 0
    AND attribute.attrelid = $1::regclass
    ORDER BY attribute.attnum;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ak.get_schema_tables(name text, OUT relname name)
    RETURNS SETOF name AS
$$
    SELECT tablename AS relname
    FROM pg_catalog.pg_tables
    WHERE schemaname=$1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ak.describe_constrs(
    table_name text,
    OUT contype "char", OUT conkey int2[], OUT relname name, OUT confkey int2[])
    RETURNS SETOF RECORD AS
$$
    SELECT contype, conkey, relname, confkey
    FROM pg_catalog.pg_constraint LEFT JOIN pg_catalog.pg_class
    ON pg_catalog.pg_constraint.confrelid = pg_catalog.pg_class.oid
    WHERE conrelid = $1::regclass;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ak.drop_all_constrs(table_name text) RETURNS void AS $$
DECLARE
    cmd text;
    sep text;
    constr_name text;
BEGIN
    cmd := 'ALTER TABLE ' || quote_ident(table_name);
    sep := '';
    FOR constr_name IN
        SELECT conname
        FROM pg_catalog.pg_constraint
        WHERE conrelid = ('"' || table_name || '"')::regclass
    LOOP
        cmd := cmd || sep || ' DROP CONSTRAINT ' || quote_ident(constr_name);
        sep := ',';
    END LOOP;
    EXECUTE cmd;
END;
$$ LANGUAGE plpgsql VOLATILE;


CREATE TABLE ak.meta (schema_name text UNIQUE NOT NULL, state int NOT NULL);


INSERT INTO ak.meta VALUES ('public', 0);


CREATE FUNCTION ak.create_schema(schema_name text) RETURNS void AS $$
BEGIN
    EXECUTE 'CREATE SCHEMA ' || quote_ident(schema_name);
    EXECUTE 'CREATE OPERATOR ' || quote_ident(schema_name) ||
            '.% (leftarg = float8, rightarg = float8, procedure = ak.mod)';
    INSERT INTO ak.meta VALUES (schema_name, 0);
END;
$$ LANGUAGE plpgsql VOLATILE;


CREATE FUNCTION ak.get_meta_state(schema_name text) RETURNS int AS $$
    SELECT state FROM ak.meta WHERE schema_name = $1 FOR SHARE;
$$ LANGUAGE SQL VOLATILE;


CREATE FUNCTION ak.set_meta_state(schema_name text, state int)
    RETURNS void AS
$$
    UPDATE ak.meta SET state = $2 WHERE schema_name = $1;
$$ LANGUAGE SQL VOLATILE;
