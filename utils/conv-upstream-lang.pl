#!/usr/bin/env perl

# This file is part of OpenTTD.
# OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
# OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.

use strict;
use warnings;

use utf8;

use File::Slurp;

my @langs = qw(
afrikaans.txt
arabic_egypt.txt
basque.txt
belarusian.txt
brazilian_portuguese.txt
bulgarian.txt
catalan.txt
chuvash.txt
croatian.txt
czech.txt
danish.txt
dutch.txt
english_AU.txt
english_US.txt
esperanto.txt
estonian.txt
faroese.txt
finnish.txt
french.txt
frisian.txt
gaelic.txt
galician.txt
german.txt
greek.txt
hebrew.txt
hindi.txt
hungarian.txt
icelandic.txt
ido.txt
indonesian.txt
irish.txt
italian.txt
japanese.txt
korean.txt
latin.txt
latvian.txt
lithuanian.txt
luxembourgish.txt
macedonian.txt
malay.txt
maltese.txt
marathi.txt
norwegian_bokmal.txt
norwegian_nynorsk.txt
persian.txt
polish.txt
portuguese.txt
romanian.txt
russian.txt
serbian.txt
simplified_chinese.txt
slovak.txt
slovenian.txt
spanish.txt
spanish_MX.txt
swedish.txt
tamil.txt
thai.txt
traditional_chinese.txt
turkish.txt
urdu.txt
ukrainian.txt
vietnamese.txt
welsh.txt
);

my %lang_str;

my @lines = read_file("english.txt") or die("Can't read english.txt");
for (@lines) {
	next if /^##/;

	chomp;
	if (/^(\w+)\s*:(.*)$/) {
		$lang_str{$1} = 1;
	}
}

for my $lang (@langs) {
	my @in_lines = read_file($lang);

	my @lines;
	for (@in_lines) {
		if (/^(\w+)\s*:/) {
			if ($lang_str{$1}) {
				push @lines, $_;
			}
		} else {
			push @lines, $_;
		}
	}
	write_file($lang, @lines);
}
