#!/bin/bash

# This file is part of OpenTTD.
# OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
# OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.

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
