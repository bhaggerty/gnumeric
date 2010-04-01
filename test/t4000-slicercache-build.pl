#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "data1.xls";
&message ("Check that $file evaluates correctly.");
test_cache_slicer ("$samples/slicers/$file", "A4", sub { /All ok/i });