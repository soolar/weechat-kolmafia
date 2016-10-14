#!/bin/bash

echo $1- | elinks -dump -dump-color-mode 3 -no-numbering -no-references -eval 'set document.dump.width = 300' | sed -r 's/[[:space:]]*$//'

