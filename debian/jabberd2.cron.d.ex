#
# Regular cron jobs for the jabberd2 package
#
0 4	* * *	root	[ -x /usr/bin/jabberd2_maintenance ] && /usr/bin/jabberd2_maintenance
