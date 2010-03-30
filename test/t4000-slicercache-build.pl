#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

#~ open FILE, "t4000.script" or die $!;

#~ my @expected = <FILE>;
#~ my $i = 0;
#~ while ($i < @expected) {
    #~ print $expected[$i];
    #~ $i++;
#~ }

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Checking function go_data_cache_build_cache.");
&sstest ("test_go_data_cache_build_cache", $expected);

__DATA__
-----------------------------------------------------------------------------
Start: test_go_data_cache_build_cache
-----------------------------------------------------------------------------

NUMBER: 0.000000
NUMBER: 0.000000
NUMBER: 0.000000
NUMBER: 0.000000
NUMBER: 1.000000
NUMBER: 2.000000
NUMBER: 0.000000
NUMBER: 2.000000
NUMBER: 4.000000
NUMBER: 0.000000
NUMBER: 3.000000
NUMBER: 6.000000
NUMBER: 0.000000
NUMBER: 4.000000
NUMBER: 8.000000
NUMBER: 0.000000
NUMBER: 5.000000
NUMBER: 10.000000
NUMBER: 0.000000
NUMBER: 6.000000
NUMBER: 12.000000
NUMBER: 0.000000
NUMBER: 7.000000
NUMBER: 14.000000
NUMBER: 0.000000
NUMBER: 8.000000
NUMBER: 16.000000
NUMBER: 0.000000
NUMBER: 9.000000
NUMBER: 18.000000
1)	(0) 0='0'	[1] '0'	[2] '0'
2)	(0) 0='0'	[1] '1'	[2] '2'
3)	(0) 0='0'	[1] '2'	[2] '4'
4)	(0) 0='0'	[1] '3'	[2] '6'
5)	(0) 0='0'	[1] '4'	[2] '8'
6)	(0) 0='0'	[1] '5'	[2] '10'
7)	(0) 0='0'	[1] '6'	[2] '12'
8)	(0) 0='0'	[1] '7'	[2] '14'
9)	(0) 0='0'	[1] '8'	[2] '16'
10)	(0) 0='0'	[1] '9'	[2] '18'
End: test_go_data_cache_build_cache