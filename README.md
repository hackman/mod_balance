# mod_balance

This module aims to provide protection from aggressive clients 
by delaying the execution of dynamic content and throttling the
serving of static content.

## The module provides several options that work indepndantly

* MinThrottleLoad
	- this option defines the minimum load that the machine 
	should have in order for the throttling to kick in

* MaxThrottleLoad
	- this option defines the load over which we will always 
	throttle requests

* MaxConnsPerIP
	- this options defines the maximum allowed connections 
	from a single IP to the server (on all virtual hosts).

* MaxUserConnections 
	- this options defines the maximum allowed connections
	to vhosts running with the same UID

* MaxVhostConnections 
	- this options defines the maximum allowed connections
	to the current vhost

* MaxGlobalConnections 
	- this options defines the maximum allowed connections
	to the server


When any of these limits is reached the current and newly created
connections will be throttled.

There are two other options that control the delaying and throttling:
* StaticContentThrottle
	- if this option is set, then the module will disable the dynamic
	static content throttling and will simply use a static delay value

* DynamicContentThrottle
	- if this option is set, all dynamic content will be delayed with
	the value of this option

## Note for Apache 1.3
  This module has to be loaded before mod_cgi in order to work for 
dynamic content. What this means is that if you use the 'ClearModuleList' 
directive, the AddModule mod_balance.c has to be on a line AFTER mod_cgi.c.
This is because the AddModule lines are read from the bottom to the top.

There is no such problem with Apache 2.x

## For more information and comments

* Marian Marinov <mm@yuhu.biz>
* Jabber: hackman@jabber.org
* ICQ: 7556201
* URL: [http://github.com/hackman/mod_balance](http://github.com/hackman/mod_balance)
