DIRS=rc lib cmd

default:VQ:
	echo mk all, mk install, mk uninstall

all install uninstall clean nuke:QV:
	mk $MKFLAGS recurse.$target

recurse.%:QV:
	for(f in $DIRS)
		@{
			echo $f
			cd $f
			mk $MKFLAGS $stem
		}
