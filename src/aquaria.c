/*
 * Aquarium Power Manager
 * Configuration files are in /etc/Aquaria/schedule
 *
 * Copyright 2007, Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 * GPL v2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <syslog.h>

#include <ip-usbph.h>

const char *POWER_SWITCH="192.168.0.100";
const char *DOTDIR="/etc/Aquaria";

const char *SCHEDULE="schedule";

#if 0
my %Switch;
my %Device;
# Device is a 'class', which points to a hash of 'name', which points to an array of:
#    { 'sensor' => { Temp | Time }, 'state' => ( ON | OFF ),
#      .. sensor specific details
#    )

sub power_set {
	my ($class, $name, $state, $override) = @_;

	my $sw = $Switch{$class}->{$name};

	if (!defined $state) {
		undef $sw->{'override'};
		return;
	}

	if (!defined $override && $sw->{'override'} > time) {
		return;
	}

	if (defined $override) {
		$sw->{'override'} = time + $override;
	} else {
		undef $sw->{'override'};
	}

	if ($state eq $sw->{'state'}) {
		return;
	}

	system("curl --user admin:1234 --silent \"http://$POWER_SWITCH/outlet?".$Switch{$class}->{$name}->{'id'}."=".$state."\" >/dev/null");
	$sw->{'state'} = $state;
}

sub power_get {
	my ($class, $name) = @_;

	return ($Switch{$class}->{$name}->{'state'},
		$Switch{$class}->{$name}->{'override'});
}


sub power_config {
	open(CONF, "curl --user admin:1234 --silent http://$POWER_SWITCH/admin.cgi |") or die "Can't open port to read config!";

	my $id=1;
	my $label;
	while (<CONF>) {
		if ($_ =~ /^\<td\>(.*)\<\/td\>/) {
			$label = $1;
			my ($class,$name) = split(/:/, $label, 2);

			$Switch{$class}->{$name} = { 'id'=>$id };
			$id++;
		}
	}
	close(CONF);
}

sub schedule_config {
	open(SCHED, "< $SCHEDULE") or die "Can't read schedule!";

	my $label;
	while (<SCHED>) {
		chomp;
		$_ =~ s/#.*$//;		# Remove comments

		if ($_ =~ /^Device\s+([^\s]+)\s*$/) {
			$label = $1;
			next;
		}

		if ($_ =~ /\s*(On|Off)\s+Always\s*$/) {
			my $state = $1;
			$state =~ tr/[a-z]/[A-Z]/;

			my ($class,$name) = split(/:/, $label, 2);

			push @{$Device{$class}->{$name}},
				{ 'sensor'=>'Always',
					'state' => $state,
				};
			next;
		}


		if ($_ =~ /^\s*(On|Off)\s+Time\s+([0-9]+):([0-9]+)\s+for\s+([0-9]+):([0-9]+)\s*$/) {
			my $time_start = $2 * 60 + $3;
			my $time_len   = $4 * 60 + $5;
			my $state = $1;
			$state =~ tr/[a-z]/[A-Z]/;

			my ($class,$name) = split(/:/, $label, 2);

			if ( ! defined $Switch{$class}->{$name}->{'id'}) {
				printf STDERR "Device $label is not defined by the switch!";
				exit 1;
			}

			push @{$Device{$class}->{$name}},
				{ 'sensor'=>'Time',
					'state' => $state,
					'start' => $time_start,
					'length' => $time_len,
				};
			next;
		}
		if ($_ =~ /^\s+(On|Off)\s+Temp\s+([<>=!]+)([0-9.]+)([FC])\s*$/) {
			my $state = $1;
			my $condition = $2;
			my $temp = $3;
			my $scale = $4;
			$state =~ tr/[a-z]/[A-Z]/;

			# Convert to degress C
			if ($scale eq 'F') {
				$scale = 'C';
				$temp = (($temp-32) * 5.0/9.0);
			}

			my ($class,$name) = split(/:/, $label, 2);

			if ( ! defined $Switch{$class}->{$name}->{'id'}) {
				printf STDERR "Device $label is not defined by the switch!";
				exit 1;
			}

			push @{$Device{$class}->{$name}},
				{ 'sensor'=> 'Temp',
					'state' => $state,
					'temp' => $temp,
					'condition' => $condition,
				};
			next;
		}

		if ($_ !~ /^\s*$/) {
			printf STDERR "Unrecognized line in schedule:\n$_\n";
			exit 1;
		}
	}

	close(SCHED);
}

sub sensor_time {
	my ($value, $cond, $state) = @_;

	if (($value > $cond->{'start'}) &&
	    ($value < ($cond->{'start'} + $cond->{'length'}))) {
		return $cond->{'state'};
	}

	$value += 24 * 60;

	if (($value > $cond->{'start'}) &&
	    ($value < ($cond->{'start'} + $cond->{'length'}))) {
		return $cond->{'state'};
	}

	return $state;
}

sub sensor_temp {
	my ($value, $cond, $state) = @_;

	my $temp = $cond->{'temp'};

	my $doit = eval "$value $cond->{'condition'} $temp";

	if ($doit) {
		return $cond->{'state'};
	}

	return $state;
}

sub device_check {
	my $t;
	my $event;
	my $time;
	my $temp;

	my $NOW_Hour=`date +%H` + 0;
	my $NOW_Min=`date +%M` + 0;
	my $time = $NOW_Hour * 60 + $NOW_Min;

	my $temp=`gotemp -C` + 0;

	foreach $class (keys %Device) {
		my $name;
		foreach $name (keys %{$Device{$class}}) {
			my $state = 'OFF';
			foreach $cond (@{$Device{$class}->{$name}}) {
				if ($cond->{'sensor'} eq 'Always') {
					$state = $cond->{'state'};
				}
				if ($cond->{'sensor'} eq 'Temp') {
					$state = &sensor_temp($temp, $cond, $state);
				}
				if ($cond->{'sensor'} eq 'Time') {
					$state = &sensor_time($time, $cond, $state);
				}
			}
			&power_set($class, $name,  $state);
		}
	}
}
#endif

void ui_timestamp(void *ui)
{
	struct ip_usbph *ph = ui;
	struct tm local_now;
	time_t time_now;
	char buff[256];
	static int colon = 0;
	int i;
	const ip_usbph_sym day_of_week[7] = {
		IP_USBPH_SYMBOL_SUN,
		IP_USBPH_SYMBOL_MON,
		IP_USBPH_SYMBOL_TUE,
		IP_USBPH_SYMBOL_WED,
		IP_USBPH_SYMBOL_THU,
		IP_USBPH_SYMBOL_FRI,
		IP_USBPH_SYMBOL_SAT,
	};

	time(&time_now);
	localtime_r(&time_now, &local_now);

	snprintf(buff, sizeof(buff), "%2d%2d%2d%02d",
	         local_now.tm_mon, local_now.tm_mday,
	         local_now.tm_hour, local_now.tm_min);

	for (i = 0; buff[i] != 0; i++) {
		ip_usbph_top_digit(ph, i + 3, ip_usbph_font_digit(buff[i]));
	}

	ip_usbph_symbol(ph, IP_USBPH_SYMBOL_COLON, colon);
	for (i = 0; i < 7; i++) {
		ip_usbph_symbol(ph, day_of_week[i], (i == local_now.tm_wday) ? 1 : 0);
	}
	colon ^= 1;

	ip_usbph_symbol(ph, IP_USBPH_SYMBOL_M_AND_D, 1);
	ip_usbph_flush(ph);
}

#if 0
sub show_menu {
	my ($class, $name) = @_;

	my $title;
	my $state;

	if ($class eq '') {
		&ip_usbph("symbol man off");
		&ip_usbph("symbol up off");
		&ip_usbph("symbol down off");
		&ip_usbph("top \"TEMP\"");
		&ip_usbph("symbol decimal on");
		my $temp=`gotemp -F`;
		chomp $temp;
		$temp = sprintf("%4d", $temp * 10);
		&ip_usbph("bot \"$temp\"");
		return;
	}

	&ip_usbph("symbol decimal off");
	if ($name eq '') {
		$title = $class;
		$title =~ tr/a-z/A-Z/;
		$title = sprintf("%-5.5s ->", $title);
		&ip_usbph("symbol man off");
		&ip_usbph("symbol up off");
		&ip_usbph("symbol down off");
		&ip_usbph("top \"$title\"");
		&ip_usbph("bot \"\"");
		return;
	}

	# We have class and name
	$title = $name;
	$title =~ tr/a-z/A-Z/;
	$title = sprintf("*%7.7s", $title);
	my $override;
	($state, $override) = &power_get($class, $name);
	if ($state eq 'ON') {
		&ip_usbph("symbol up on");
		&ip_usbph("symbol down off");
	} else {
		&ip_usbph("symbol up off");
		&ip_usbph("symbol down on");
	}

	&ip_usbph("top \"$title\"");
	if (defined $override) {
		$override = sprintf("%4d", $override - time);
		&ip_usbph("bot \"$override\"");
		&ip_usbph("symbol man on");
	} else {
		&ip_usbph("bot \"\"");
		&ip_usbph("symbol man off");
	}
}

sub handle_key {
	my ($class, $name, $key) = @_;

	my @classes = sort keys %Switch;

	if ($class eq '') {
		($class) = @classes;
		return ($class, $name);
	} elsif ($name eq '') {
		if ($key eq 'VOL-' or $key eq '4') {
			$class = '';
		} elsif ($key eq 'VOL+' or $key eq '6') {
			($name) = sort keys %{$Switch{$class}};
		} elsif ($key eq 'DOWN' or $key eq '8') {
			my $i;
			for ($i = 0; $i < @classes; $i++) {
				if ($classes[$i] eq $class) {
					$i = ($i + 1) % @classes;
					$class = $classes[$i];
					last;
				}
			}
		} elsif ($key eq 'UP' or $key eq '2') {
			my $i;
			for ($i = 0; $i < @classes; $i++) {
				if ($classes[$i] eq $class) {
					$i = ($i + $#classes) % @classes;
					$class = $classes[$i];
					last;
				}
			}
		}
		return ($class, $name);
	}

	my @names = sort keys %{$Switch{$class}};

	if ($key eq 'VOL-' or $key eq '4') {
		$name = '';
	} elsif ($key eq 'DOWN' or $key eq '8') {
		my $i;
		for ($i = 0; $i < @names; $i++) {
			if ($names[$i] eq $name) {
				$i = ($i + 1) % @names;
				$name = $names[$i];
				last;
			}
		}
	} elsif ($key eq 'UP' or $key eq '2') {
		my $i;
		for ($i = 0; $i < @names; $i++) {
			if ($names[$i] eq $name) {
				$i = ($i + $#names) % @names;
				$name = $names[$i];
				last;
			}
		}
	} elsif ($key eq 'YES') {
		&power_set($class, $name, "ON", 15*60);
	} elsif ($key eq 'NO') {
		&power_set($class, $name, "OFF", 15*60);
	} elsif ($key eq 'C') {
		&power_set($class, $name);
	}

	return ($class, $name);
}


sub ip_usbph_get {
	sleep 15;
	return;

	my ($cmd) = @_;
	&ip_usbph($cmd);
	$get = <IP_USBPH_KEY>;
	if ($!) { &ip_usbph_reinit };
	chomp $get;
	return $get;
}
#endif

void *ui_open(void)
{
	struct ip_usbph *ph;
	int err;

	ph = ip_usbph_acquire(0);
	if (ph == NULL) {
		syslog(LOG_WARNING, "Can't find IP-USBPH user interface, continuing without it.");
	}
	err = ip_usbph_init(ph);
	if (err < 0) {
		syslog(LOG_WARNING, "Can't init IP-USBPH user interface, continuing without it.");
		ip_usbph_release(ph);
		ph = NULL;
	}

	return ph;
}

void ui_clear(void *ui)
{
	struct ip_usbph *ph = ui;

	if (ph == NULL)
		return;

	ip_usbph_clear(ph);
	ip_usbph_flush(ph);
}

int main(int argc, char **argv)
{
	int idle;
	void *ui;

	openlog("aquaria", LOG_CONS | LOG_PERROR, LOG_USER);

	ui = ui_open();
	ui_clear(ui);

#if 0
	my $class = '';
	my $name = '';

	# Generate the power port ID configuration
	power_config();

	# Read the schedule
	&schedule_config;

	my $NOW_Hour;
	my $NOW_Min;
	my $NOW;
#endif

	while (1) {
		ui_timestamp(ui);
#if 0
		$key=&ip_usbph_get("key 1000");
		chomp $key;
		if ( $key eq '') {
			$idle++;
			if ($idle == 10) {
				$idle = 0;
			}
		} else {
			&ip_usbph("backlight");
			($class, $name) = &handle_key($class, $name, $key);
		}

		&show_menu($class, $name);
		&device_check();
#else
		sleep(1);
#endif
	}
	return EXIT_SUCCESS;
}
