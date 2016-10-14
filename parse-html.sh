#!/bin/bash

echo $1- | elinks -dump -dump-color-mode 3 -eval 'set document.dump.numbering =0' -eval 'set document.dump.references = 0'

