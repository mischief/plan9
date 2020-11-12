chat
====

simple irc client for 9front

install
-------

	mkdir -p $home/src && cd $home/src
	hg clone https://bitbucket.org/mischief/chat
	cd chat
	mk install
	man chat
	chat

uninstall
---------

	mk uninstall

commands
--------

rio delete is treated like `/x`.

| `/x` | close window, parting if in a channel or quit if in main window |
| `/q mischief` | start a privmsg with `mischief` in a new rio window |
| `/q #cat-v` | join the channel `#cat-v` in a new rio window |
| `/n doofus` | set nickname to `doofus` |
