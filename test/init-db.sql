
CREATE TABLE "auth_user" (
    "id" serial NOT NULL PRIMARY KEY,
    "username" varchar(30) NOT NULL UNIQUE
)
;
CREATE TABLE "main_label" (
    "id" serial NOT NULL PRIMARY KEY,
    "name" varchar(30) NOT NULL UNIQUE,
    "count" integer CHECK ("count" >= 0) NOT NULL
)
;
CREATE TABLE "main_app" (
    "id" serial NOT NULL PRIMARY KEY,
    "name" varchar(30) NOT NULL UNIQUE,
    "admin_id" integer NOT NULL REFERENCES "auth_user" ("id") DEFERRABLE INITIALLY DEFERRED,
    "email" varchar(75) NOT NULL,
    "summary" varchar(75) NOT NULL,
    "description" text NOT NULL
)
;
CREATE TABLE "main_app_labels" (
    "id" serial NOT NULL PRIMARY KEY,
    "app_id" integer NOT NULL REFERENCES "main_app" ("id") DEFERRABLE INITIALLY DEFERRED,
    "label_id" integer NOT NULL REFERENCES "main_label" ("id") DEFERRABLE INITIALLY DEFERRED,
    UNIQUE ("app_id", "label_id")
)
;
CREATE TABLE "main_app_devs" (
    "id" serial NOT NULL PRIMARY KEY,
    "app_id" integer NOT NULL REFERENCES "main_app" ("id") DEFERRABLE INITIALLY DEFERRED,
    "user_id" integer NOT NULL REFERENCES "auth_user" ("id") DEFERRABLE INITIALLY DEFERRED,
    UNIQUE ("app_id", "user_id")
)
;

INSERT INTO "auth_user" VALUES (0, 'test_user');
INSERT INTO "auth_user" VALUES (1, 'Odysseus');
INSERT INTO "auth_user" VALUES (2, 'Achilles');
INSERT INTO "main_label" VALUES (0, '1', 2);
INSERT INTO "main_label" VALUES (1, '2', 1);
INSERT INTO "main_app" VALUES (0, 'test_app', 0, 'a@b.com',
                               'test app', 'test app...');
INSERT INTO "main_app" VALUES (1, 'another_app', 1, 'x@y.com',
                               'another app', 'another app...');
INSERT INTO "main_app" VALUES (2, 'ak', 0, '', '', '');
INSERT INTO "main_app" VALUES (3, 'lib', 0, '', '', '');
INSERT INTO "main_app" VALUES (4, 'bad_app', 0, '', '', '');
INSERT INTO "main_app" VALUES (5, 'throwing_app', 0, '', '', '');
INSERT INTO "main_app" VALUES (6, 'blocking_app', 0, '', '', '');
INSERT INTO "main_app_labels" VALUES (0, 0, 0);
INSERT INTO "main_app_labels" VALUES (1, 0, 1);
INSERT INTO "main_app_labels" VALUES (2, 1, 0);
INSERT INTO "main_app_devs" VALUES (0, 0, 1);
INSERT INTO "main_app_devs" VALUES (1, 0, 2);




