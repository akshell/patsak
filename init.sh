#!/bin/bash

THIRD_PARTY_DIR=~/work/third_party

cp "$THIRD_PARTY_DIR/MochiKit.js" sample/code/release/test_app/ku

createdb test_app
createdb test_app@test_user@test_tag
createlang plpgsql test_app
createlang plpgsql test_app@test_user@test_tag
psql -f src/sql-funcs.sql test_app
psql -f src/sql-funcs.sql test_app@test_user@test_tag
