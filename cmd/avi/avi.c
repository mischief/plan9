#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>
#include <thread.h>
#include <keyboard.h>

#include "imagefile.h"

#include "riff.h"
#include "lib.h"
#include "fns.h"
#include "player.h"

int mainstacksize = 128*1024;
int debug = 0;

void
copyout(Biobuf *bio, long off, long len, int out)
{
	char *buf;

	Bseek(bio, off, 0);

	buf = mallocz(len, 1);
	if(buf == nil)
		sysfatal("mallocz: %r");

	if(Bread(bio, buf, len) != len)
		sysfatal("readn: %r");

	if(write(out, buf, len) != len)
		sysfatal("write: %r");

	free(buf);
}

/*
static void
playavi(Biobuf *bio)
{
	vlong start, dt;
	AVIIndex *idxp;

	for(curindex = 0; curindex < indexlen; curindex++){
		idxp = &index[curindex];

		start = nsec();
		//pindent(),print("index %.4s flags %08ub offset %ud length %ud\n", idxp->id, idxp->flags, idxp->offset, idxp->length);

		// offset is relative to 'movi' start, +4 to skip size
		if(memcmp(idxp->id, "00dc", 4) == 0){
			//putjpg(movioff+idxp->offset+4);
			//copyout(bio, movioff+idxp->offset+4, idxp->length, video);
		}

		//if(memcmp(idxp->id, "01wb", 4) == 0)
			//copyout(bio, movioff+idxp->offset+4, idxp->length, audio);

		dt = (nsec() - start)/1000000;
		fprint(2, "dt=%lldms\n", dt);
		doevent();
	}
}
*/

static void
usage(void)
{
	fprint(2, "usage: %s [-D] file.avi\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	AVI *avi;
	AVIPlayer *player;

	ARGBEGIN{
	case 'D':
		debug++;
		break;
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	memimageinit();
	fmtinstall('H', encodefmt);

	avi = aviopen(argv[0]);
	if(avi == nil)
		sysfatal("aviopen: %r");

	if(debug)
		avidump(avi);

	/* we only support a MJPG stream and 16 bit stereo PCM @ 44100hz. */
	if(memcmp(avi->stream[0].handler, "MJPG", 4) != 0)
		sysfatal("expected MJPG video, got '%.4s'", avi->stream[0].handler);
	if(memcmp(avi->stream[1].handler, "\x1\0\0\0", 4) != 0)
		sysfatal("expected 16-bit 2 channel PCM audio stream @ 44100hz, got '%.4s'", avi->stream[1].handler);

	if(avi->waveformat.channels != 2 ||
		avi->waveformat.bitspersample != 16 ||
		avi->waveformat.samplespersec != 44100)
		sysfatal("can't handle audio format, expected 16-bit 2 channel PCM audo @ 44100hz\n");


/*
	decin = chancreate(sizeof(Decreq*), 1);
	decout = chancreate(sizeof(Decreq*), 1);

	if((decoder = decproc(fd, decin, decout)) == nil)
		sysfatal("decproc: %r");
*/

	player = playerinit(avi);
	if(player == nil)
		sysfatal("playerinit: %r");

	playerrun(player);

	threadexitsall(nil);
}

/*
void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		sysfatal("getwindow: %r");
}
*/