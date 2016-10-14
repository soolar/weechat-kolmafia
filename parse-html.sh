#!/bin/bash

echo $1- | elinks -dump -dump-color-mode 3 -no-numbering -no-references -eval 'set document.dump.width = 300' -eval 'set terminal.screen-xterm-256color.transparency = 1' | sed -r 's/[[:space:]]*$//; s/[[:space:]]{3}//; /\-$/ d; s/\o33\[48;5;0m//g'

