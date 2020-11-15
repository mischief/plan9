</$objtype/mkfile

TARG=avi

OFILES=main.$O riff.$O avi.$O player.$O decproc.$O readjpg.$O totruecolor.$O torgbv.$O
CLEANFILES=imagefile.h

BIN=/$objtype/bin

</sys/src/cmd/mkone

main.$O decproc.$O readjpg.$O totruecolor.$O torgbv.$O: imagefile.h

%.$O: /sys/src/cmd/jpg/%.c
	$CC $CFLAGS /sys/src/cmd/jpg/$stem.c

imagefile.h: /sys/src/cmd/jpg/imagefile.h
	cp $prereq $target 

uninstall:V:
	rm -f $BIN/$TARG
