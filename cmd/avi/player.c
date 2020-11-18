#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>

#include "lib.h"
#include "fns.h"
#include "player.h"

enum {
	/* number of AVI chunks processed at once */
	BATCH = 10,
};

typedef struct FPS FPS;

struct FPS {
	vlong ot, snap[32];
	float fps;
	ulong frames;
};

static void
fpsup(FPS *fps)
{
	int i, x, n;
	float dt, newfps;
	vlong ns;

	ns = nsec();

	dt = (ns - fps->ot)/1000000000.0;
	fps->ot = ns;

	n = nelem(fps->snap);

	fps->snap[fps->frames % n] = 1.0/dt;

	newfps = 0;
	x = fps->frames;
	if(x >= nelem(fps->snap))
		x = nelem(fps->snap);

	for(i = 0; i < x; i++)
		newfps += fps->snap[i];

	newfps /= nelem(fps->snap);

	fps->frames++;
	fps->fps = newfps;
}

struct AVIPlayer {
	AVI *avi;
	// into index;
	long cindex;

	Decproc **dec;
	int ndec;
	int cdec;

	Mousectl *mouse;
	Keyboardctl *keyboard;

	Image *buffer;

	int afd;

	FPS fps;
};

static int
getndec(void)
{
	int ndec;
	char *e;

	e = getenv("NPROC");
	if(e == nil)
		sysfatal("set $NPROC");

	ndec = atoi(e);
	if(ndec < 1)
		sysfatal("invalid $NPROC");

	if(ndec > 8)
		ndec = 8;

	return ndec;
}

static int
memtoimage(Image *i, Memimage *mi)
{
	int c;

	c = bytesperline(mi->r, mi->depth) * Dy(mi->r);

	return loadimage(i, i->r, byteaddr(mi, ZP), c);
}

static void
putimg(Image *buffer, Memimage *mimg)
{
	if(memtoimage(buffer, mimg) < 0){
		fprint(2, "memtoimage: %r\n");
		return;
	}

	draw(screen, screen->clipr, buffer, nil, ZP);
	flushimage(display, 1);
}

static void
putpcm(AVIPlayer *player, uchar *buf, int nbuf)
{
	int rv;

	rv = write(player->afd, buf, nbuf);
	if(rv != nbuf)
		fprint(2, "putpcm: %r");
}

AVIPlayer*
playerinit(AVI *avi)
{
	int i, ndec;
	char wind[128];
	Rectangle vr;
	AVIPlayer *player;

	ndec = getndec();
	player = mallocz(sizeof(AVIPlayer), 1);
	if(player == nil)
		return nil;

	player->afd = open("/dev/audio", OWRITE);
	if(player->afd < 0)
		goto err;
		
	snprint(wind, sizeof(wind), "-dx %ud -dy %ud", avi->header.width+8, avi->header.height+8);
	if(newwindow(wind) < 0)
		goto err;

	if(initdraw(nil, nil, argv0) < 0)
		goto err;

	if((player->mouse = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");

	if((player->keyboard = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	vr = Rect(0, 0, avi->header.width, avi->header.height);

	/* JPG decoder gives us RGB24 */
	player->buffer = allocimage(display, vr, RGB24, 0, DNofill);
	if(player->buffer == nil)
		goto err;

	player->avi = avi;
	player->ndec = ndec;
	player->dec = mallocz(ndec * sizeof(Decproc*), 1);
	if(player->dec == nil)
		goto err;

	for(i = 0; i < ndec; i++){
		player->dec[i] = decproc(avi);
		if(player->dec[i] == nil)
			goto err;
	}

	return player;

err:
	free(player->dec);
	/* TODO: cleanup failed decproc */

	if(player->afd >= 0)
		close(player->afd);
	free(player);
	return nil;
}

static void
playerevent(AVIPlayer *p)
{
	long idx;
	Rune r;

	enum { MOUSE, RESIZE, KEYBOARD, END };
	Alt alts[] = {
	[MOUSE]		{ p->mouse->c,			&p->mouse->Mouse,	CHANRCV },
	[RESIZE]	{ p->mouse->resizec,	nil,				CHANRCV },
	[KEYBOARD]	{ p->keyboard->c,		&r,					CHANRCV },
	[END]		{ nil,					nil,				CHANNOBLK },
	};

	switch(alt(alts)){
	case MOUSE:
		break;
	case RESIZE:
		if(getwindow(display, Refnone) < 0)
			sysfatal("getwindow: %r");
		break;
	case KEYBOARD:
		switch(r){
		case Kdel:
			threadexitsall(nil);
		case Kleft:
			idx = 30;
			while(idx > 0 && p->cindex > 0){
				p->cindex -= BATCH;
				idx--;
			}
			break;
		case Kright:
			idx = 30;
			while(idx > 0 && p->cindex < p->avi->nindex){
				p->cindex += BATCH;
				idx--;
			}
			break;
		case '1': case '2': case '3': case '4': case '5':
		case '6': case '7': case '8': case '9':
			p->cindex = (p->avi->nindex*10*(r - '0' - 1))/100;
			break;
		case '0':
			p->cindex = (p->avi->nindex*90)/100;
			break;
		}
		break;
	case END:
		break;		
	}
}

static void
loadnext(AVIPlayer *p)
{
	Channel *ic;
	Decreq req;

	ic = p->dec[p->cdec]->in;
	p->cdec = (p->cdec+1) % p->ndec;

	req.index = p->cindex;
	req.count = BATCH;
	assert(send(ic, &req) == 1);

	p->cindex += BATCH;
}

void
playerrun(AVIPlayer *p)
{
	int i, b;
	Decresp *resp;
	Dec *d;
	Channel *oc;

	b = 0;

	/* preload decoders */
	for(i = 0; i < p->ndec; i++)
		loadnext(p);

	while(p->cindex < p->avi->nindex){
		oc = p->dec[(p->cdec)%p->ndec]->out;
		assert(recv(oc, &resp) == 1);
		loadnext(p);

		if(resp->err[0] != 0)
			sysfatal("decoder error: %s", resp->err);

		for(i = 0; i < resp->ndec; i++){
			d = &resp->dec[i];
			switch(d->type){
			case DINVALID:
				//fprint(2, "skipping unknown chunk\n");
				break;
			case DAUDIO:
				assert(d->buf != nil);
				putpcm(p, d->buf, d->nbuf);
				free(d->buf);
				break;
			case DIMAGE:
				assert(d->image != nil);
				putimg(p->buffer, d->image);
				freememimage(d->image);

				fpsup(&p->fps);
/*
				if(b)
					fprint(2, "\b\b\b\b\b\b\b\b\b\b");
				b = 1;
				fprint(2, "FPS: %05.2f", p->fps.fps);
*/
				break;
			}

			/* check events */
			playerevent(p);
		}

		free(resp->dec);
		free(resp);
	}
}
