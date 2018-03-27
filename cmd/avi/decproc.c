#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>

#include "imagefile.h"
#include "avi.h"
#include "fns.h"

static void
freerawimage(Rawimage *r)
{
	int i;
	for(i = 0; i < r->nchans; i++)
		free(r->chans[i]);
	free(r->cmap);
	free(r);
}

static uchar*
decpcm(int fd, long offset, int n)
{
	uchar *buf;

	buf = malloc(n);
	if(buf == nil)
		return nil;

	seek(fd, offset, 0);

	if(readn(fd, buf, n) != n){
		free(buf);
		return nil;
	}

	return buf;
}

static Memimage*
decjpg(int fd, long offset)
{
	Rawimage **jpg, *c;
	Memimage *i;

	c = nil;
	i = nil;

	seek(fd, offset, 0);

	jpg = readjpg(fd, CYCbCr);
	if(jpg == nil){
		fprint(2, "readjpg: %r\n");
		return nil;
	}

	if(jpg[0] == nil)
		goto fail;

	c = totruecolor(jpg[0], CRGB24);
	if(c == nil)
		goto fail;

	i = allocmemimage(c->r, RGB24);
	if(i == nil)
		goto fail;

	if(loadmemimage(i, i->r, c->chans[0], c->chanlen) < 0){
		freememimage(i);
		i = nil;
	}

fail:
	if(c)
		freerawimage(c);
	if(jpg[0])
		freerawimage(jpg[0]);
	free(jpg);
	return i;	
}

static void
decrun(void *v)
{
	int i;
	Decproc *d = v;
	Decreq req;
	Decresp *resp;
	Dec *dec;
	AVIIndex *idxp;

	threadsetname("decproc %d", threadid());

	for(;;){
		if(recv(d->in, &req) != 1){
			fprint(2, "decproc dying");
			break;
		}

		resp = mallocz(sizeof(Decresp), 1);
		if(resp == nil)
			sysfatal("malloc: %r");
		resp->dec = mallocz(req.count * sizeof(Dec), 1);
		if(resp->dec == nil)
			sysfatal("malloc: %r");
		resp->ndec = req.count;

		for(i = 0; i < req.count; i++, req.index++){
			idxp = &d->avi->index[req.index];
			dec = &resp->dec[i];

			// XXX: no clue why 0-length chunks appear
			if(idxp->length == 0){
				dec->type = DINVALID;
				continue;
			}

			if(memcmp(idxp->id, "01wb", 4) == 0){
				dec->type = DAUDIO;
				dec->nbuf = idxp->length;
				dec->buf = decpcm(d->fd, d->avi->movi+idxp->offset+4, idxp->length);
				if(dec->buf == nil){
					rerrstr(resp->err, sizeof(resp->err));
					goto out;
				}
			}

			if(memcmp(idxp->id, "00dc", 4) == 0){
				dec->type = DIMAGE;
				dec->image = decjpg(d->fd, d->avi->movi+idxp->offset+4);
				if(dec->image == nil){
					rerrstr(resp->err, sizeof(resp->err));
					goto out;
				}
			}
		}

out:
		sendp(d->out, resp);
	}
}

Decproc*
decproc(AVI *avi)
{
	Decproc *d;

	d = mallocz(sizeof(*d), 1);
	if(d == nil)
		return nil;

	d->avi = avi;

	d->fd = open(avi->path, OREAD);
	if(d->fd < 0){
		free(d);
		return nil;
	}

	d->in = chancreate(sizeof(Decreq), 0);
	d->out = chancreate(sizeof(Decresp*), 0);

	assert(d->in != nil);
	assert(d->out != nil);

	if(proccreate(decrun, d, 16*1024) < 0)
		sysfatal("proccreate: %r");

	return d;
}
