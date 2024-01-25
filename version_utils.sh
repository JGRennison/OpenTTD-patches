#!/bin/bash

# $Id$

# This file is part of OpenTTD.
# OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
# OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.

function show_help {
	echo "Usage: version_utils.sh OPTION" >&2
	echo "-s: Output a SHA-256 of the source tree" >&2
	echo "-l: Output the names of all files in the source tree with their SHA-256 hash" >&2
	echo "-n: Output the names of all files in the source tree without a hash" >&2
	echo "-o: Return true (0) if SHA-256 utility can be found" >&2
	echo "-w: Write ./.ottdrev-vc" >&2
	echo "-r TAGNAME: Create a tag, write ./.ottdrev-vc referencing the tag, possibly update README.md," >&2
	echo "	commit it and move the tag to point to the new revision. Requires git." >&2
	echo "-h: Show this help" >&2
}

NAMES=
HASHLIST=
HASH=
TESTOK=
WRITE=
RELEASETAG=
while getopts ":hslnowr:" opt; do
	case $opt in
		s)
			HASH=1
			;;
		l)
			HASHLIST=1
			;;
		n)
			NAMES=1
			;;
		o)
			TESTOK=1
			;;
		w)
			WRITE=1
			;;
		r)
			RELEASETAG="$OPTARG"
			;;
		h | \?)
			show_help
			exit 1
			;;
	esac
done

HASH_CMD=

function handle_source {
	if [ -n "$2" ]; then
		$HASH_CMD "$1"
	else
		echo "$1"
	fi
}

function find_hasher {
	if [ "`echo -n "test" | sha256sum 2> /dev/null`" == "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08  -" ]; then
		HASH_CMD=sha256sum
	elif [ "`echo -n "test" | shasum -a 256 2> /dev/null`" == "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08  -" ]; then
		HASH_CMD="shasum -a 256"
	elif [ "`echo -n "test" | shasum -a 256 -p 2> /dev/null`" == "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08  -" ]; then
		HASH_CMD="shasum -a 256 -p"
	else
		echo "Could not generate SHA-256" >&2
		exit 1
	fi
}

function output_hash_list {
	read_source "1"
}

function read_source {
	handle_source "CMakeLists.txt" "$1"
	while IFS=$'\n' read -r line; do
		handle_source "$line" "$1"
	done < <( find -L src -type f \( -name 'CMakeLists.txt' -o -name '*.cpp' -o -name '*.c' -o -name '*.hpp' -o -name '*.h' -o -name '*.sq' -o -name '*.mm' -o -name '*.in' \) -print | LC_ALL=C sort )
	while IFS=$'\n' read -r line; do
		handle_source "$line" "$1"
	done < <( find -L src/lang -type f -name '*.txt' -print | LC_ALL=C sort )
}

if [ -z "$HASH" -a -z "$NAMES" -a -z "$HASHLIST" -a -z "$TESTOK" -a -z "$WRITE" -a -z "$RELEASETAG" ]; then
	show_help
	exit 1
fi

if [ -n "$NAMES" ]; then
	read_source
fi

if [ -n "$HASHLIST" ]; then
	find_hasher
	output_hash_list
fi

if [ -n "$HASH" ]; then
	find_hasher
	output_hash_list | $HASH_CMD
fi

if [ -n "$WRITE" ]; then
	find_hasher
	cmake -DGENERATE_OTTDREV=.ottdrev-vc-tmp -P cmake/scripts/FindVersion.cmake
	output_hash_list | $HASH_CMD >> .ottdrev-vc-tmp
	mv .ottdrev-vc-tmp .ottdrev-vc
fi

function unignore_files {
	git update-index --no-assume-unchanged README.md jgrpp-changelog.md .ottdrev-vc
}

if [ -n "$RELEASETAG" ]; then
	git update-index --assume-unchanged README.md jgrpp-changelog.md .ottdrev-vc
	trap unignore_files EXIT
	if ! git diff-index --quiet HEAD; then
		echo "Repo is dirty, aborting" >&2
		exit 1
	fi
	if ! git diff-index --quiet --cached HEAD; then
		echo "Repo is dirty, aborting" >&2
		exit 1
	fi
	if [ "${RELEASETAG:0:6}" = "jgrpp-" -a -n "${RELEASETAG:6}" ]; then
		if ! grep -q -e "^### v${RELEASETAG:6} (" jgrpp-changelog.md; then
			echo "v${RELEASETAG:6} is not in changelog, aborting" >&2
			exit 1
		fi
	fi
	if ! git tag "$RELEASETAG"; then
		echo "Tag already exists or is not valid, aborting" >&2
		exit 1
	fi
	if ! ./version_utils.sh -w; then
		exit 1
	fi
	unignore_files
	trap '' EXIT
	if [ "${RELEASETAG:0:6}" = "jgrpp-" -a -n "${RELEASETAG:6}" ]; then
		sed -i "1 s/^\(## JGR's Patchpack version \).\+/\1${RELEASETAG:6}/" README.md
	fi
	git add .ottdrev-vc README.md jgrpp-changelog.md
	git commit -m "Version: Committing version data for tag: $RELEASETAG"
	git tag -f "$RELEASETAG"
fi

if [ -n "$TESTOK" ]; then
	find_hasher 2> /dev/null
fi
