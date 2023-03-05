#!/bin/bash

if [ $# -eq 0 ]; then
   echo "Please specify a language file to update"
   exit 1
fi

language_file="$1"

# Check that the specified file exists
if [ ! -f "$language_file" ]; then
   echo "File not found: $language_file"
   exit 1
fi

# Read each line from the English file and check if it exists in the language file
while read -r line; do
   # Skip lines that start with #
   if [[ $line =~ ^#.* ]]; then
      continue
   fi
   # Extract the string code from the English line
   STRING_CODE=$(echo $line | cut -d' ' -f1)
   # Check if the string code exists in the language file
   if ! grep -qw "$STRING_CODE" "$language_file"; then
      # If the line doesn't exist, add it to the end of the language file
      continue
      echo "$line" >>"$language_file"
      echo "Added missing line to $language_file: $line"
   fi
done <"english.txt"

######################### Update 2023-01-29
# Remember to first update the project and rebuild it all to have the latest instance of strgen.
# Obtained translations strings with: ../../build/src/strgen/strgen -w galician.txt 2>&1 | cut -d\' -f 2 | head -n-1 | xargs -I $ grep -w $ english.txt >> galician.txt
#########################