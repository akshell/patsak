#!/bin/bash

THIRD_PARTY_DIR=~/work/third_party

cp "$THIRD_PARTY_DIR/MochiKit.js" test/js/ku/
touch test/js/unreadable.js
chmod -r test/js/unreadable.js
