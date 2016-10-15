#!/bin/bash

echo $1- | # feed the input in to elinks
sed -r 's/^> //' | # lop off the prompt from where kolmafia would have shown the input
elinks -dump -dump-color-mode 1 -no-numbering -no-references -eval 'set document.dump.width = 300' | # parse and dump with elinks
sed -r 's/[[:space:]]*$//' | # strip off trailing whitespace
sed -r 's/[[:space:]]{3}//' | # strip off the three spaces elinks likes to add to the start of the line
sed -r 's/\o33\[48;5;0m//g' | # remove color codes that set the background to 0 since elinks just will not obey the transparency setting
sed -r 's/-$//' | # If there's a hyphen at the end of the line, ditch it. There always seem to be random floating hyphens coming out of nowhere
sed -r '/^(\o33\[[[:digit:]]*(;[[:digit:]]+)+m)+$/ d' # get rid of lines that are just color codes
