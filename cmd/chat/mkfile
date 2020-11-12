</$objtype/mkfile

BIN=/$objtype/bin
TARG=chat
OFILES=\
	conn.$O\
	main.$O\
	msg.$O\
	util.$O\
	wind.$O\

MAN=/sys/man/1

</sys/src/cmd/mkone

man:	$TARG.man
	cp $prereq $MAN/$TARG

install:V:	man

uninstall:V:
	rm -f $BIN/$TARG $MAN/$TARG
