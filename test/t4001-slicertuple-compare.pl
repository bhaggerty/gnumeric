
#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Checking function go_data_slicer_tuple_compare_to.");
&sstestslicer ("test_go_data_slicer_tuple_compare_to", $expected);

__DATA__
-----------------------------------------------------------------------------
Start: test_go_data_slicer_tuple_compare_to
-----------------------------------------------------------------------------

pass haha
End: test_go_data_slicer_tuple_compare_to