
#!/usr/bin/env perl

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


my @lang_lines;

my $start = 0;
my @lines = read_file("extra/english.txt") or die("Can't read english.txt");
for (@lines) {
	$start = 1 if /##override off/;
	next unless $start;

	next if /^##/;

	push @lang_lines, $_;
}


for my $lang (@langs) {
	my @in_lines = read_file($lang);
	push @in_lines, read_file("extra/$lang");

	my %strs;
	for (@in_lines) {
		chomp;
		next if /^#/;

		if (/^(\w+)\s*:/) {
			$strs{$1} = $_;
		}
	}

	my @lines;
	my $suppress_whitespace = 1;
	for (@lang_lines) {
		if (/^(\w+)\s*:/) {
			if ($strs{$1}) {
				push @lines, $strs{$1} . "\n";
				$suppress_whitespace = 0;
			}
		} else {
			next if ($_ eq "\n") && $suppress_whitespace;
			push @lines, $_;
			$suppress_whitespace = ($_ eq "\n");
		}
	}
	pop @lines if ($lines[-1] eq "\n");
	write_file("extra/$lang", @lines);
}