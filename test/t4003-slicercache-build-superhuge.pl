#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "data-superhuge.xls";
my $dump_file = "data-superhuge-cache-dump.txt";
&message ("Check that $file generates cache correctly.");
test_cache_slicer ("$samples/slicers/$file",  "gen-cache", 32500, 4, "$test/$dump_file");