
CREATE LANGUAGE plpgsql;

DROP SCHEMA IF EXISTS ku CASCADE;
CREATE SCHEMA ku;


CREATE FUNCTION ku.to_string(f float8) RETURNS text AS $$
    SELECT $1::text;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ku.to_string(b bool) RETURNS text AS $$
    SELECT $1::text;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ku.to_string(t timestamp(3)) RETURNS text AS $$
    SELECT to_char($1, 'Dy, DD Mon YYYY HH:MI:SS GMT');
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ku.to_number(t text) RETURNS float8 AS $$
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


CREATE FUNCTION ku.to_number(b bool) RETURNS float8 AS $$
    SELECT $1::int::float8;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ku.to_number(t timestamp(3)) RETURNS float8 AS $$
    SELECT extract(epoch from $1)*1000;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ku.to_bool(f float8) RETURNS bool AS $$
    SELECT CASE WHEN $1 = 0 OR $1 = 'NaN'::float8 THEN false ELSE true END;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ku.to_bool(t text) RETURNS bool AS $$
    SELECT CASE WHEN $1 = '' THEN false ELSE true END;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ku.mod(a float8, b float8) RETURNS float8 AS $$
    SELECT $1 - trunc($1/$2) * $2;
$$ LANGUAGE SQL IMMUTABLE;


CREATE FUNCTION ku.eval(t text) RETURNS text AS $$
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


CREATE FUNCTION ku.insert_into_empty(table_name text)
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


CREATE FUNCTION ku.create_schema(schema_name text) RETURNS void AS $$
BEGIN
    EXECUTE 'CREATE SCHEMA ' || quote_ident(schema_name);
    EXECUTE 'CREATE OPERATOR ' || quote_ident(schema_name) ||
            '.% (leftarg = float8, rightarg = float8, procedure = ku.mod)';
END;
$$ LANGUAGE plpgsql VOLATILE;


CREATE FUNCTION ku.drop_schemas(prefix text) RETURNS void AS $$
DECLARE
    nsp RECORD;
BEGIN
    FOR nsp IN SELECT nspname
               FROM pg_namespace
               WHERE nspname LIKE (prefix || '%') LOOP
        EXECUTE 'DROP SCHEMA ' || quote_ident(nsp.nspname) || ' CASCADE';
    END LOOP;
END;
$$ LANGUAGE plpgsql VOLATILE;


CREATE FUNCTION ku.get_schema_size(prefix text) RETURNS numeric AS $$
    SELECT SUM(pg_total_relation_size(pg_class.oid))
    FROM pg_class, pg_namespace
    WHERE pg_namespace.nspname = $1
    AND pg_class.relnamespace = pg_namespace.oid;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.describe_table(
    name text, OUT attname name, OUT typname name, OUT def text)
    RETURNS SETOF RECORD AS
$$
    SELECT attribute.attname, pg_type.typname, ku.eval(attribute.adsrc)
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


CREATE FUNCTION ku.get_schema_tables(name text, OUT relname name)
    RETURNS SETOF name AS
$$
    SELECT tablename AS relname
    FROM pg_catalog.pg_tables
    WHERE schemaname=$1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.describe_constrs(
    table_name text,
    OUT contype "char", OUT conkey int2[], OUT relname name, OUT confkey int2[])
    RETURNS SETOF RECORD AS
$$
    SELECT contype, conkey, relname, confkey
    FROM pg_catalog.pg_constraint LEFT JOIN pg_catalog.pg_class
    ON pg_catalog.pg_constraint.confrelid = pg_catalog.pg_class.oid
    WHERE conrelid = $1::regclass;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.get_app_patsak_version(name text) RETURNS text AS $$
    SELECT patsak_version
    FROM public.main_app AS app
    WHERE app.name = $1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.get_app_quotas(
    app_name text, OUT db_quota integer, OUT fs_quota integer)
    RETURNS RECORD AS
$$
    SELECT db_quota, fs_quota
    FROM public.main_app AS app
    WHERE app.name = $1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.describe_app(
    name text,
    OUT id int4, OUT admin text, OUT summary text, OUT description text)
    RETURNS SETOF RECORD AS
$$
    SELECT app.id, usr.username, app.summary, app.description
    FROM public.main_app AS app, public.auth_user AS usr
    WHERE app.admin_id = usr.id
    AND app.name = $1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.get_app_devs(app_id int4, OUT dev_name text)
    RETURNS SETOF text AS
$$
    SELECT usr.username
    FROM public.auth_user AS usr, public.main_app_devs AS app_devs
    WHERE usr.id = app_devs.user_id
    AND app_devs.app_id = $1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.get_app_labels(app_id int4, OUT label_name text)
    RETURNS SETOF text AS
$$
    SELECT label.name
    FROM public.main_label AS label, public.main_app_labels AS app_labels
    WHERE label.id = app_labels.label_id
    AND app_labels.app_id = $1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.get_user_email(user_name text) RETURNS text AS
$$
    SELECT usr.email
    FROM public.auth_user AS usr
    WHERE usr.username = $1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.get_admined_apps(
    user_name text, OUT app_name text)
    RETURNS SETOF text AS
$$
    SELECT app.name
    FROM public.main_app AS app, public.auth_user AS usr
    WHERE app.admin_id = usr.id
    AND usr.username = $1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.get_developed_apps(
    user_name text, OUT app_name text)
    RETURNS SETOF text AS
$$
    SELECT app.name
    FROM public.main_app AS app, public.auth_user AS usr,
         public.main_app_devs AS devs
    WHERE devs.app_id = app.id
    AND devs.user_id = usr.id
    AND usr.username = $1;
$$ LANGUAGE SQL STABLE;


CREATE FUNCTION ku.get_apps_by_label(
    label_name text, OUT app_name text)
    RETURNS SETOF text AS
$$
    SELECT app.name
    FROM public.main_app AS app, public.main_label AS label,
         public.main_app_labels AS app_labels
    WHERE app_labels.app_id = app.id
    AND app_labels.label_id = label.id
    AND label.name = $1;
$$ LANGUAGE SQL STABLE;
