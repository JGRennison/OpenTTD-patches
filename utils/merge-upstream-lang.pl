#!/usr/bin/env perl

# This file is part of OpenTTD.
# OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
# OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.

use strict;
use warnings;

use utf8;

use File::Slurp;
use Getopt::Long;
use IPC::Cmd;

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
maori.txt
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

my $ref = "master";
my $commit;

Getopt::Long::Configure("no_auto_abbrev", "bundling");
GetOptions (
	"ref|r=s"      => \$ref,
	"commit|c"     => \$commit,
) or die("Invalid options");

my $upstream_commit;
if (!scalar IPC::Cmd::run(
				command => [qw(git rev-parse --verify), "$ref^{commit}"],
				verbose => 0,
				buffer  => \$upstream_commit,
				timeout => 20)) {
	die "Could not get upstream commit from ref: $ref";
}
chomp $upstream_commit;

my %lang_str;

my @lines = read_file("english.txt") or die("Can't read english.txt");
my @records;
my $seen_header = 0;
for (@lines) {
	if (/##id 0x0000/) {
		$seen_header = 1;
		next;
	}
	next if !$seen_header;

	if (/^(\w+)\s*:(.*)$/) {
		my $str = {
			current => $2,
		};
		$lang_str{$1} = $str;
		push @records, {
			strid => $1,
		};
	} else {
		push @records, {
			fixed => $_,
		};
	}
}

my $upstream_lang;
if (!scalar IPC::Cmd::run(
				command => [qw(git show), "$upstream_commit:./english.txt"],
				verbose => 0,
				buffer  => \$upstream_lang,
				timeout => 20)) {
	die "Could not get upstream english.txt";
}
my @upstream_lines = split(/^/m, $upstream_lang);
for (@upstream_lines) {
	next if /^##/;

	if (/^(\w+)\s*:(.*)$/) {
		my $str = $lang_str{$1};
		if ($str && $str->{current} eq $2) {
			$str->{use_upstream} = 1;
		}
	}
}

for my $lang (@langs) {
	my @in_lines = read_file($lang);

	my $in_upstream_lang;
	if (!scalar IPC::Cmd::run(
					command => [qw(git show), "$upstream_commit:./$lang"],
					verbose => 0,
					buffer  => \$in_upstream_lang,
					timeout => 20)) {
		die "Could not get upstream $lang";
	}
	my @in_upstream_lines = split(/^/m, $in_upstream_lang);

	my @lines;
	my %translation_str;

	my @cases;

	for (@in_lines) {
		# Preserve file header intact
		if (/^##case\s+(.*)$/) {
			@cases = split(/\s+/, $1);
		}

		push @lines, $_;
		last if /##id 0x0000/;
	}

	for (@in_lines) {
		if (/^(\w+)(?:\.(\w+))?\s*:/) {
			my $str = $lang_str{$1};
			if ($str) {
				if ($2) {
					$translation_str{$1}->{$2} = $_;
				} else {
					$translation_str{$1} = {
						main => $_,
					};
				}
			}
		}
	}
	for (@in_upstream_lines) {
		if (/^(\w+)(?:\.(\w+))?\s*:/) {
			my $str = $lang_str{$1};
			if ($str && $str->{use_upstream}) {
				if ($2) {
					$translation_str{$1}->{$2} = $_;
				} else {
					$translation_str{$1} = {
						main => $_,
					};
				}
			}
		}
	}

	for (@records) {
		my $strid = $_->{strid};
		if ($strid) {
			my $trans = $translation_str{$strid};
			if ($trans) {
				push @lines, $trans->{main};
				for my $case (@cases) {
					my $casestr = $trans->{$case};
					push @lines, $casestr if defined $casestr;
				}
			}
		} else {
			push @lines, $_->{fixed};
		}
	}
	write_file($lang, @lines);
}

if ($commit) {
	system(qw(git commit -m), "Merge translations up to $upstream_commit", '--author=translators <translators@openttd.org>', '--', @langs);
}
