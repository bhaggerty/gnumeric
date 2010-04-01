#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "data-huge.xls";
my $dump_file = "data-huge-slicer-dump.txt";
&message ("Check that $file generates slicer correctly.");
test_cache_slicer ("$samples/slicers/$file",  "gen-slicer", 350, 20, "$test/$dump_file");