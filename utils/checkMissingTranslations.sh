#!/bin/bash

git_top_level=$(git rev-parse --show-toplevel)

if [ $# -eq 0 ]; then
   echo "Please specify a language to update"
   exit 1
fi

language_file="$git_top_level/src/lang/extra/$1.txt"

# Check that the specified file exists
if [ ! -f "$language_file" ]; then
   echo "File not found: $language_file"
   exit 1
fi

in_override_section="true"
# Read each line from the English file and check if it exists in the language file
while read -r line; do
   # Check if we have reached the end of the override section
   if [[ $line == "##override off" ]]; then
      in_override_section="false"
      continue
   fi

   # Skip lines before the end of the override section
   if [[ $in_override_section == "true" ]]; then
      continue
   fi
   # Skip other lines that start with #
   if [[ $line =~ ^#.* ]]; then
      continue
   fi
   # Extract the string code from the English line
   STRING_CODE=$(echo $line | cut -d':' -f1 | tr -d '[:space:]')
   # Check if the string code exists in the language file
   if ! grep -q "$STRING_CODE" "$language_file"; then
      # If the line doesn't exist, add it to the end of the language file
      echo "$line" >>"$language_file"
      echo "Added missing line to $language_file: $line"
   fi
done <"$git_top_level/src/lang/extra/english.txt"
cd $git_top_level/src/lang/
perl $git_top_level/utils/conv-lang.pl
