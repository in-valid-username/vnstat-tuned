#!/usr/bin/perl -w

# vnstat-metrics.cgi -- Prometheus compatible metrics endpoint output from vnStat data
# copyright (c) 2022 Teemu Toivola <tst at iki dot fi>
# released under the GNU General Public License

use strict;
use JSON::PP;
use Time::Local qw(timelocal_nocheck timegm_nocheck);

# location of vnstat binary
my $vnstat_cmd = '/usr/bin/vnstat';


################


sub get_interface_alias
{
	my ($interface) = @_;
	my $interface_alias = $interface->{'alias'};
	if (length($interface_alias) == 0) {
		$interface_alias = $interface->{'name'};
	}
	return $interface_alias;
}

sub prom_escape
{
	my ($value) = @_;
	$value = '' unless defined $value;
	$value =~ s/\\/\\\\/g;
	$value =~ s/"/\\"/g;
	$value =~ s/\n/\\n/g;
	return $value;
}

sub prom_interface_labels
{
	my ($name, $alias) = @_;
	return 'interface="' . prom_escape($name) . '",alias="' . prom_escape($alias) . '"';
}

sub print_totals
{
	my ($data) = @_;

	print "\n# HELP vnstat_interface_total_received_bytes All time total received (rx) bytes\n";
	print "# TYPE vnstat_interface_total_received_bytes counter\n";

	foreach my $interface ( @{ $data->{'interfaces'} } ) {
		my $interface_alias = get_interface_alias($interface);
		print "vnstat_interface_total_received_bytes{" . prom_interface_labels($interface->{'name'}, $interface_alias) . "} $interface->{'traffic'}{'total'}{'rx'}\n";
	}

	print "\n# HELP vnstat_interface_total_transmitted_bytes All time total transmitted (tx) bytes\n";
	print "# TYPE vnstat_interface_total_transmitted_bytes counter\n";

	foreach my $interface ( @{ $data->{'interfaces'} } ) {
		my $interface_alias = get_interface_alias($interface);
		print "vnstat_interface_total_transmitted_bytes{" . prom_interface_labels($interface->{'name'}, $interface_alias) . "} $interface->{'traffic'}{'total'}{'tx'}\n";
	}
}

sub print_updated
{
	my ($data) = @_;

	print "\n# HELP vnstat_interface_updated_timestamp_seconds Unix time when the interface data was last updated\n";
	print "# TYPE vnstat_interface_updated_timestamp_seconds gauge\n";

	foreach my $interface ( @{ $data->{'interfaces'} } ) {
		my $interface_alias = get_interface_alias($interface);
		print "vnstat_interface_updated_timestamp_seconds{" . prom_interface_labels($interface->{'name'}, $interface_alias) . "} $interface->{'updated'}{'timestamp'}\n";
	}
}

sub normalized_monthrotate
{
	my ($monthrotate) = @_;
	if (!defined $monthrotate || $monthrotate !~ /^\d+$/ || $monthrotate < 1 || $monthrotate > 31) {
		return 1;
	}
	return int($monthrotate);
}

sub normalized_int_range
{
	my ($value, $min, $max, $default) = @_;
	if (!defined $value || $value !~ /^\d+$/ || $value < $min || $value > $max) {
		return $default;
	}
	return int($value);
}

# 只有 JSON 布尔值 true 才算开启；缺字段或数字都按关闭算
sub json_flag
{
	my ($value) = @_;
	return ref($value) eq 'JSON::PP::Boolean' && $value ? 1 : 0;
}

sub days_in_month
{
	my ($year1900, $month_index) = @_;
	my @days = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31);
	my $year = $year1900 + 1900;
	my $leap = ($year % 4 == 0 && ($year % 100 != 0 || $year % 400 == 0)) ? 1 : 0;
	return $days[$month_index] + ($month_index == 1 ? $leap : 0);
}

# 按 vnStat 入库口径算出 $when 对应的桶时间戳。
# 5 分钟桶是四舍五入（round(min/5)*5），可能比当前系统时间超前几分钟，不能直接向下取整。
# 无论 UseUTC 是否开启，db_getdata_range 吐出来的 JSON timestamp 都是按进程本地时区映射的日历时间，
# 所以先在账期时区里判定落在哪个月/年，最后统一用 timelocal_nocheck 转成与 JSON 一致的时间戳。
sub current_bucket_timestamp
{
	my ($resolution, $billing, $when) = @_;
	$when = time() unless defined $when;
	my $use_utc = json_flag($billing->{'useutc'});
	my ($sec, $min, $hour, $day, $mon, $year) = $use_utc ? gmtime($when) : localtime($when);

	if ($resolution eq 'fiveminute') {
		$min = int($min / 5 + 0.5) * 5;
		return timelocal_nocheck(0, $min, $hour, $day, $mon, $year);
	}
	if ($resolution eq 'hour') {
		return timelocal_nocheck(0, 0, $hour, $day, $mon, $year);
	}
	if ($resolution eq 'day') {
		return timelocal_nocheck(0, 0, 0, $day, $mon, $year);
	}
	my $month_rotate = normalized_monthrotate($billing->{'monthrotate'});
	my $rotate_hour = normalized_int_range($billing->{'monthrotatehour'}, 0, 23, 0);
	my $rotate_min = normalized_int_range($billing->{'monthrotateminute'}, 0, 59, 0);
	if ($resolution eq 'month') {
		my $last = days_in_month($year, $mon);
		my $boundary_day = $month_rotate > $last ? $last : $month_rotate;
		my $boundary = $use_utc
			? timegm_nocheck(0, $rotate_min, $rotate_hour, $boundary_day, $mon, $year)
			: timelocal_nocheck(0, $rotate_min, $rotate_hour, $boundary_day, $mon, $year);
		if ($when < $boundary) {
			$mon--;
			if ($mon < 0) {
				$mon = 11;
				$year--;
			}
		}
		return timelocal_nocheck(0, 0, 0, 1, $mon, $year);
	}
	if ($resolution eq 'year') {
		if (json_flag($billing->{'monthrotateaffectsyears'})) {
			my $boundary = $use_utc
				? timegm_nocheck(0, $rotate_min, $rotate_hour, $month_rotate, 0, $year)
				: timelocal_nocheck(0, $rotate_min, $rotate_hour, $month_rotate, 0, $year);
			if ($when < $boundary) {
				$year--;
			}
		}
		return timelocal_nocheck(0, 0, 0, 1, 0, $year);
	}
	return;
}

sub current_resolution_value
{
	my ($data, $interface, $resolution, $field) = @_;
	my $rows = $interface->{'traffic'}{$resolution};
	return unless ref($rows) eq 'ARRAY' && @{$rows};
	my $row = $rows->[0];
	return unless ref($row) eq 'HASH';
	return unless defined $row->{$field};
	my $timestamp = $row->{'timestamp'};
	return unless defined $timestamp && $timestamp =~ /^-?\d+$/;
	my %billing = (
		monthrotate => defined $interface->{'monthrotate'} ? $interface->{'monthrotate'} : $data->{'monthrotate'},
		monthrotatehour => defined $interface->{'monthrotatehour'} ? $interface->{'monthrotatehour'} : $data->{'monthrotatehour'},
		monthrotateminute => defined $interface->{'monthrotateminute'} ? $interface->{'monthrotateminute'} : $data->{'monthrotateminute'},
		monthrotateaffectsyears => defined $interface->{'monthrotateaffectsyears'} ? $interface->{'monthrotateaffectsyears'} : $data->{'monthrotateaffectsyears'},
		useutc => defined $interface->{'useutc'} ? $interface->{'useutc'} : $data->{'useutc'},
	);
	my $bucket = current_bucket_timestamp($resolution, \%billing);
	return unless defined $bucket && $timestamp == $bucket;
	return $row->{$field};
}

sub print_data_resolution
{
	my ($resolution, $data) = @_;
	my $output_count = 0;

	print "\n# HELP vnstat_interface_".$resolution."_received_bytes Received (rx) bytes for current $resolution\n";
	print "# TYPE vnstat_interface_".$resolution."_received_bytes gauge\n";

	$output_count = 0;
	foreach my $interface ( @{ $data->{'interfaces'} } ) {
		my $rx = current_resolution_value($data, $interface, $resolution, 'rx');
		if (defined $rx) {
			my $interface_alias = get_interface_alias($interface);
			print "vnstat_interface_".$resolution."_received_bytes{" . prom_interface_labels($interface->{'name'}, $interface_alias) . "} $rx\n";
			$output_count++;
		}
	}
	if ($output_count == 0) {
		print "# no data\n";
	}

	print "\n# HELP vnstat_interface_".$resolution."_transmitted_bytes Transmitted (tx) bytes for current $resolution\n";
	print "# TYPE vnstat_interface_".$resolution."_transmitted_bytes gauge\n";

	$output_count = 0;
	foreach my $interface ( @{ $data->{'interfaces'} } ) {
		my $tx = current_resolution_value($data, $interface, $resolution, 'tx');
		if (defined $tx) {
			my $interface_alias = get_interface_alias($interface);
			print "vnstat_interface_".$resolution."_transmitted_bytes{" . prom_interface_labels($interface->{'name'}, $interface_alias) . "} $tx\n";
			$output_count++;
		}
	}
	if ($output_count == 0) {
		print "# no data\n";
	}
}

sub run_command
{
	my @cmd = @_;
	my $stdout = '';
	my $stderr = '';

	pipe(my $err_read, my $err_write) or return ('', $!, -1);
	my $pid = open(my $out, "-|");
	if (!defined $pid) {
		close $err_read;
		close $err_write;
		return ('', $!, -1);
	}
	if ($pid == 0) {
		close $err_read;
		open(STDERR, ">&", $err_write) or exit 127;
		close $err_write;
		exec {$cmd[0]} @cmd or exit 127;
	}
	close $err_write;
	binmode $out;
	binmode $err_read;
	{
		local $/;
		$stdout = <$out>;
		$stderr = <$err_read>;
	}
	$stdout = '' unless defined $stdout;
	$stderr = '' unless defined $stderr;
	close $out;
	my $status = $?;
	close $err_read;
	return ($stdout, $stderr, $status);
}

sub plain_response
{
	my ($status, $message) = @_;

	if (defined $status) {
		print "Status: $status\n";
	}
	print "Content-Type: text/plain\n";
	print "Cache-Control: private, no-cache\n\n$message\n";
	exit 0;
}

my @data_resolutions = ('fiveminute', 'hour', 'day', 'month', 'year');

my ($json_data, $command_stderr, $command_status) = run_command($vnstat_cmd, "--json", "s", "1");
print STDERR $command_stderr if length($command_stderr);

if ($command_status != 0) {
	plain_response("500 Internal Server Error", "Failed to read vnStat data.");
}

my $data = "";
# Keep rx and tx as the decimal digits from the JSON text.
$json_data =~ s/("(?:rx|tx)":)(\d+)/$1"$2"/g;
eval { $data = decode_json($json_data) };
if ($@) {
	plain_response("500 Internal Server Error", "Invalid command output.");
}

if (ref($data) ne 'HASH' or not defined $data->{'vnstatversion'}) {
	plain_response("500 Internal Server Error", "Expected content from command output missing.");
}

if (ref($data->{'interfaces'}) ne 'ARRAY' or not defined $data->{'interfaces'}[0]) {
	plain_response("500 Internal Server Error", "No interfaces found in command output.");
}

my $first_interface = $data->{'interfaces'}[0];
if (ref($first_interface) ne 'HASH'
	or ref($first_interface->{'created'}) ne 'HASH'
	or not defined $first_interface->{'created'}{'timestamp'}) {
	plain_response("500 Internal Server Error", "Incompatible vnStat version used.");
}

print "Content-Type: text/plain; version=0.0.4; charset=utf-8\n";
print "Cache-Control: private, no-cache\n\n";

print "# vnStat version: ".$data->{'vnstatversion'}."\n";

print_totals($data);
print_updated($data);

foreach my $data_resolution ( @data_resolutions ) {
	print_data_resolution($data_resolution, $data);
}
