#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "data-errors.xls";
my $dump_file = "data-error-slicer-dump.txt";
&message ("Check that $file generates slicer correctly.");
test_cache_slicer ("$samples/slicers/$file",  "gen-slicer", 61, 6, "$test/$dump_file");