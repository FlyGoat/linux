#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# (c) 2024, Jiaxun Yang <jiaxun.yang@flygoat.com>
#           Inspired by scripts/get_maintainer.pl
#
# Check the compliance status for participants.
#
# usage: perl scripts/check_compliance.pl [OPTIONS] <participants>
# example: perl scripts/check_compliance.pl --maintainer <email@address>
#          perl scripts/check_compliance.pl --warning "FullName <email@address>"
#

use warnings;
use strict;

my $P = $0;
my $V = '0.11';

use Getopt::Long qw(:config no_auto_abbrev);
use Cwd;
use File::Find;
use File::Spec::Functions;
use open qw(:std :encoding(UTF-8));

my $opt_participation = 0;
my $opt_maintainer    = 0;
my $opt_warning       = 0;
my $opt_help          = 0;

if (
    !GetOptions(
	'participation!' => \$opt_participation,
	'maintainer!'    => \$opt_maintainer,
	'warning!'       => \$opt_warning,
	'help|h'         => \$opt_help,
    )
  )
{
    die "$P: invalid argument - use --help if necessary\n";
}

# Display help message if --help is specified
if ($opt_help) {
    print_help();
    exit 0;
}

# Default to all warnings if no specific option is provided
if ( !$opt_participation && !$opt_maintainer && !$opt_warning ) {
    $opt_participation = 1;
    $opt_maintainer    = 1;
    $opt_warning       = 1;
}

my @compliance_entries;
my @mfiles;

# Function to display the help message
sub print_help {
    print <<"END_HELP";
$P - Check the compliance status for participants.

Usage:
  perl $P [OPTIONS] <participants>

Options:
  --participation    Check for 'No Participation' status.
  --maintainer       Check for 'No Maintainer' status.
  --warning          Check for 'Warning' status.
  --help, -h         Display this help message.

Description:
  This script checks the compliance status of participants based on the COMPLIANCE files found
  in the directory tree. It matches provided email addresses (or names with emails) against
  entries in the COMPLIANCE files and reports any compliance issues found.

  If no specific option is provided, the script checks for all statuses: 'No Participation',
  'No Maintainer', and 'Warning'.

Exit Codes:
  0 - No compliance issues found.
  1 - Compliance issues were found.

END_HELP
}

# Function to read the compliance file and store entries
sub read_compliance_file {
    my ($file) = @_;

    open( my $comp, '<', $file )
      or die "$P: Can't open COMPLIANCE file '$file': $!\n";

    my $current_entity;
    my %entry;

    while ( my $line = <$comp> ) {
	chomp $line;
	$line =~ s/^\s+|\s+$//g;    # Trim leading and trailing whitespace

	next if $line =~ /^#/;      # Skip comments
	next if $line =~ /^$/;      # Skip empty lines

	if ( $line !~ /^[A-Z]:\s+/ ) {
	    # It's an entity name
	    if (%entry) {
		push @compliance_entries, {%entry};
		%entry = ();
	    }
	    $current_entity = $line;
	    $entry{entity}  = $current_entity;
	}
	elsif ( $line =~ /^M:\s*(.*)/ ) {
	    my $email_entry = $1;
	    # Extract email address from possible 'FullName <email@address>' format
	    my $email;
	    if ( $email_entry =~ /<(.+)>/ ) {
		$email = $1;
	    }
	    else {
		$email = $email_entry;
	    }
	    push @{ $entry{emails} }, $email;
	}
	elsif ( $line =~ /^S:\s*(.*)/ ) {
	    $entry{status} = $1;
	}
	elsif ( $line =~ /^R:\s*(.*)/ ) {
	    $entry{reason} = $1;
	}
    }

    # Push the last entry
    if (%entry) {
	push @compliance_entries, {%entry};
    }

    close($comp);
}

# Function to find compliance files named 'COMPLIANCE'
sub find_is_compliance_file {
    my $file = $_;
    return unless $file eq 'COMPLIANCE';
    $file = $File::Find::name;
    return unless -f $file;
    push @mfiles, $file;
}

# Search for COMPLIANCE files starting from the current directory
find( \&find_is_compliance_file, '.' );

# Read and parse each COMPLIANCE file found
foreach my $file (@mfiles) {
    read_compliance_file($file);
}

# Function to check compliance status for a given email
sub check_compliance_status {
    my ($email) = @_;

    foreach my $entry (@compliance_entries) {
	next unless exists $entry->{emails};
	foreach my $pattern ( @{ $entry->{emails} } ) {
	    # Convert wildcard patterns to regex
	    my $regex = quotemeta($pattern);
	    $regex =~ s/\\\*/.*/g;
	    $regex =~ s/\\\?/.{1}/g;

	    if ( $email =~ /^$regex$/i ) {
		return $entry;
	    }
	}
    }
    return undef;
}

# Main logic to process each participant email provided
my $compliance_issues_found = 0;

foreach my $participant (@ARGV) {
    my $original_input = $participant;

    # Extract email address from input if in 'FullName <email@address>' format
    my $email;
    if ( $participant =~ /<(.+)>/ ) {
	$email = $1;
    }
    else {
	$email = $participant;
    }

    my $entry = check_compliance_status($email);

    if ( defined $entry ) {
	my $status = $entry->{status};

	# Insert a new line before printing if not the first issue
	if ( $compliance_issues_found > 0 ) {
	    print "\n";
	}

	if ( $opt_participation && $status eq 'No Participation' ) {
	    print "$original_input: Participation prohibited.\n";
	    print "Reason: $entry->{reason}\n" if exists $entry->{reason};
	    $compliance_issues_found++;
	}
	elsif ( $opt_maintainer && $status eq 'No Maintainer' ) {
	    print "$original_input: Maintainer role prohibited.\n";
	    print "Reason: $entry->{reason}\n" if exists $entry->{reason};
	    $compliance_issues_found++;
	}
	elsif ( $opt_warning && $status eq 'Warning' ) {
	    print "$original_input: May subject to compliance reuqest.\n";
	    print "Reason: $entry->{reason}\n" if exists $entry->{reason};
	    $compliance_issues_found++;
	}
    }
    # Do not print anything if no compliance issues are found for this participant
}

# If no compliance issues were found for any participants
if ( $compliance_issues_found == 0 ) {
    print "No compliance issues found for the provided participants.\n";
    exit 0;
}
else {
    exit 1;
}
