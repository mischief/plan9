#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>

#include "imagefile.h"

#include "lib.h"

int mainstacksize = 64 * 1024;

#define FOURCC(a,b,c,d) \
	(a | b << 8 | c << 16 | d << 24)

/*
	((u32int)d | \
	((u32int)c << 8) | \
	((u32int)b << 16) | \
	((u32int)a << 24))
*/

enum
{
	AHZ = 44100,
};

int debug;
int framerate = 15;
char *outfile = "rec.avi";
int intr = 0;
int silence = 0;

typedef struct AVIWriter AVIWriter;
struct AVIWriter
{
	int			run;
	Biobuf		*bio;
	int			afd;	/* audio source */
	int			pfd;	/* image(6) source */

	u32int		movisz;	/* offsets for index */

	AVIIndex	*idx;
	int			cidx; /* current */
	int			nidx; /* max */

	u32int		indexsz;

	int			aframes;
	int			vframes;

	Channel		*vchan; /* IOchunk* */
	int			vpid;		/* of vidproc */
};

long
Bput4(Biobuf *bio, char *c)
{
	return Bwrite(bio, c, 4);
}

long
Bput16le(Biobuf *bio, u16int u)
{
	char b[2];

	b[0] = u & 0xFF;
	b[1] = (u >> 8) & 0xFF;

	return Bwrite(bio, b, 2);
}

long
Bput32le(Biobuf *bio, u32int u)
{
	char b[4];

	b[0] = u & 0xFF;
	b[1] = (u >> 8) & 0xFF;
	b[2] = (u >> 16) & 0xFF;
	b[3] = (u >> 24) & 0xFF;

	return Bput4(bio, b);
}

long
Bput32leat(Biobuf *bio, u32int u, vlong off)
{
	vlong cur;

	cur = Boffset(bio);

	Bseek(bio, off, 0);

	if(Bput32le(bio, u) < 0)
		return -1;

	Bseek(bio, cur, 0);

	return 4;
}

#define riffenter(bio, fcc) \
	long sum = 0, roffset; \
	if(Bput4(bio, fcc) < 0) return 0; \
	roffset = Boffset(bio); \
	Bput32le(bio, 0);

#define riffexit(bio) \
	if(Boffset(bio) & 0x1) if(Bputc(bio, 0) < 0) return -1; \
	if(Bput32leat(bio, Boffset(bio) - roffset - 4, roffset) < 0) return -1; \
	return sum;

#define bioput(bio, fun, ...) do { \
		long rv = fun(bio, __VA_ARGS__); \
		if(rv < 0) \
			return -1; \
		sum += rv; \
	} while(0)

static long
putstrh(Biobuf *bio, AVIStreamHeader *strh)
{
	riffenter(bio, "strh");

	bioput(bio, Bput4, strh->type);
	bioput(bio, Bput4, strh->handler);
	bioput(bio, Bput32le, strh->flags);
	bioput(bio, Bput16le, strh->priority);
	bioput(bio, Bput16le, strh->language);
	bioput(bio, Bput32le, strh->initialframes);
	bioput(bio, Bput32le, strh->scale);
	bioput(bio, Bput32le, strh->rate);
	bioput(bio, Bput32le, strh->start);
	bioput(bio, Bput32le, strh->length);
	bioput(bio, Bput32le, strh->suggestedbuffersize);
	bioput(bio, Bput32le, strh->quality);
	bioput(bio, Bput32le, strh->samplesize);
	bioput(bio, Bput16le, strh->minx);
	bioput(bio, Bput16le, strh->miny);
	bioput(bio, Bput16le, strh->minx);
	bioput(bio, Bput16le, strh->minx);

	riffexit(bio);
}

long
putvideoheader(Biobuf *bio, BITMAPINFO *bih)
{
	riffenter(bio, "strf");

	bioput(bio, Bput32le, bih->size);
	bioput(bio, Bput32le, bih->width);
	bioput(bio, Bput32le, bih->height);
	bioput(bio, Bput16le, bih->planes);
	bioput(bio, Bput16le, bih->bitcount);
	bioput(bio, Bput32le, bih->compression);
	bioput(bio, Bput32le, bih->sizeimage);
	bioput(bio, Bput32le, bih->xppm);
	bioput(bio, Bput32le, bih->yppm);
	bioput(bio, Bput32le, bih->clrused);
	bioput(bio, Bput32le, bih->clrimportant);

	riffexit(bio);
}

static long
putaudioheader(Biobuf *bio, WAVEFORMATEX *wfh)
{
	riffenter(bio, "strf");

	bioput(bio, Bput16le, wfh->tag);
	bioput(bio, Bput16le, wfh->channels);
	bioput(bio, Bput32le, wfh->samplespersec);
	bioput(bio, Bput32le, wfh->avgbytespersec);
	bioput(bio, Bput16le, wfh->blockalign);
	bioput(bio, Bput16le, wfh->bitspersample);
	bioput(bio, Bput16le, wfh->extrasize);

	riffexit(bio);
}

static long
putstrl(Biobuf *bio, BITMAPINFO *bih, WAVEFORMATEX *wfh, AVIStreamHeader *strh)
{
	assert((bih != nil) != (wfh != nil));

	riffenter(bio, "LIST");
	bioput(bio, Bput4, "strl");

	long rv = putstrh(bio, strh);
	if(rv < 0)
		return -1;

	sum += rv;

	if(bih != nil)
		rv = putvideoheader(bio, bih);
	else if(wfh != nil)
		rv = putaudioheader(bio, wfh);

	if(rv < 0)
		return -1;

	sum += rv;

	riffexit(bio);
}

static long
putmainheader(Biobuf *bio, AVIMainHeader *h)
{
	riffenter(bio, "avih");

	bioput(bio, Bput32le, h->microsperframe);
	bioput(bio, Bput32le, h->maxbytespersec);
	bioput(bio, Bput32le, h->reserved1);
	bioput(bio, Bput32le, h->flags);
	bioput(bio, Bput32le, h->totalframes);
	bioput(bio, Bput32le, h->initialframes);
	bioput(bio, Bput32le, h->streams);
	bioput(bio, Bput32le, h->suggestedbuffersize);
	bioput(bio, Bput32le, h->width);
	bioput(bio, Bput32le, h->height);
	bioput(bio, Bput32le, h->reserved2[0]);
	bioput(bio, Bput32le, h->reserved2[1]);
	bioput(bio, Bput32le, h->reserved2[2]);
	bioput(bio, Bput32le, h->reserved2[3]);

	riffexit(bio);
}

int
aviheader(AVIWriter *a, AVIMainHeader *h, BITMAPINFO *bih, WAVEFORMATEX *wfh)
{
	long sum;
	Biobuf *bio;
	vlong roff, hdrl, hdrdat;
	AVIStreamHeader strh;

	sum = 0;
	bio = a->bio;

	Bseek(bio, 0, 0);

	bioput(bio, Bput4, "RIFF");
	roff = Boffset(bio);
	bioput(bio, Bput32le, 0xEFBEADDE);
	bioput(bio, Bput4, "AVI ");
	bioput(bio, Bput4, "LIST");

	hdrl = Boffset(bio);
	bioput(bio, Bput32le, 0xEFBEADDE);
	bioput(bio, Bput4, "hdrl");

	hdrdat = Boffset(bio);

	if(putmainheader(bio, h) < 0)
		return -1;

	memset(&strh, 0, sizeof(strh));

	memcpy(strh.type, "vids", 4);
	memcpy(strh.handler, "MJPG", 4);
	strh.flags = 0;
	strh.priority = 0;
	strh.language = 0;
	strh.initialframes = 1;
	strh.scale = 1;
	strh.rate = framerate;
	strh.start = 0;
	strh.length = a->vframes;
	strh.suggestedbuffersize = 1024*1024;
	strh.quality = 0;
	strh.minx = strh.miny = 0;
	strh.maxx = bih->width;
	strh.maxy = bih->height;

	if(putstrl(bio, bih, nil, &strh) < 0)
		return -1;

	memset(&strh, 0, sizeof(strh));

	memcpy(strh.type, "auds", 4);
	memcpy(strh.handler, "\x1\0\0\0", 4);
	strh.flags = 0;
	strh.priority = 0;
	strh.language = 0;
	strh.initialframes = 0;
	strh.scale = 1;
	strh.rate = wfh->samplespersec;
	strh.start = 0;
	strh.length = a->aframes;
	strh.suggestedbuffersize = 10*1024;
	strh.quality = 0;
	strh.minx = strh.miny = 0;
	strh.maxx = strh.maxy = 0;

	if(putstrl(bio, nil, wfh, &strh) < 0)
		return -1;

	bioput(bio, Bput32leat, Boffset(bio) - hdrdat + 4, hdrl);

	bioput(bio, Bput4, "LIST");
	bioput(bio, Bput32le, a->movisz + 4);
	bioput(bio, Bput4, "movi");

	bioput(bio, Bput32leat, Boffset(bio) - 8 + a->movisz + a->indexsz, roff);

	return sum;
}

static long
putindex(AVIWriter *a, char *fcc, u32int off, u32int len)
{
	AVIIndex *p;

	p = &a->idx[a->cidx];

	memcpy(p->id, fcc, 4);
	p->flags = 0x10; /* keyframe */
	p->offset = off;
	p->length = len;

	a->cidx++;

	if(a->cidx == a->nidx){
		a->nidx += 1000;
		p = realloc(a->idx, a->nidx * sizeof(*p));
		if(p == nil)
			return -1;
		a->idx = p;
	}

	return 0;
}

long
aviwritemovi(AVIWriter *a, char *cc, void *buf, long len)
{
	long sum;

	sum = 0;

	bioput(a->bio, Bput4, cc);
	bioput(a->bio, Bput32le, len);
	bioput(a->bio, Bwrite, buf, len);

	if(putindex(a, cc, a->movisz+4, len) < 0)
		return -1;

	if(len & 1){
		bioput(a->bio, Bputc, 0);
		a->movisz++;
	}

	a->movisz += 4+4+len;

	return sum;
}

long
aviwriteaudio(AVIWriter *a, char *abuf, int abuflen)
{
	long rv;
	char err[ERRMAX];

	if(silence == 1){
		memset(abuf, 0, abuflen);
		sleep(1000/framerate);
	} else {
		rv = read(a->afd, abuf, abuflen);
		if(rv != abuflen){
			rerrstr(err, sizeof(err));
			if(strcmp(err, "interrupted") == 0)
				return 0;
			return -1;
		}
	}

	a->aframes++;

	return aviwritemovi(a, "01wb", abuf, abuflen);
}

uchar zero[16];

long
aviwritevideo(AVIWriter *a)
{
	long rv;
	IOchunk c;
	uchar canary[16];
	memset(canary, 0, sizeof(canary));

	assert(a);

	switch(nbrecv(a->vchan, &c)){
		case 0: return 0;
		case -1: return -1;
	}

	if(memcmp(zero, canary, sizeof(canary)) != 0)
		abort();

	assert(a);

	a->vframes++;

	rv = aviwritemovi(a, "00dc", c.addr, c.len);
	free(c.addr);

	return rv;
}

long
aviwriteindex(AVIWriter *a)
{
	long sum;
	u32int sz;
	int i;
	AVIIndex *p;

	sum = 0;
	sz = sizeof(*a->idx) * a->cidx;

	bioput(a->bio, Bput4, "idx1");
	bioput(a->bio, Bput32le, sz);

	for(i = 0; i < a->cidx; i++){
		p = &a->idx[i];
		bioput(a->bio, Bput4, p->id);
		bioput(a->bio, Bput32le, p->flags);
		bioput(a->bio, Bput32le, p->offset);
		bioput(a->bio, Bput32le, p->length);
	}

	return sum;
}

int
mynotify(void*, char *msg)
{
	if(strcmp(msg, "interrupt") == 0){
		print("interrupted\n");
		intr = 1;
		return 1;
	}

	return 0;
}

void
vidproc(void *v)
{
	vlong now, off;
	AVIWriter *w;
	Memimage *raw, *rgb;
	void *jpegbuf;
	int jpegmax;
	Biobuf jbio;
	char *jerr;

	w = v;

	procsetname("video proc");

	rfork(RFNOTEG);

	while(w->run){
		now = nsec();

		seek(w->pfd, 0, 0);
		raw = readmemimage(w->pfd);
		if(raw == nil){
			fprint(2, "video source broken: %r\n");
			w->run = 0;
			break;
		}

		rgb = memmultichan(raw);
		if(rgb == nil){
			fprint(2, "cant convert image: %r\n");
			w->run = 0;
			break;
		}

		if(rgb != raw)
			freememimage(raw);

		jpegmax = 500*1024;
		jpegbuf = malloc(jpegmax);
		/* paranoia */
		memset(jpegbuf, 0, jpegmax);
		if(jpegbuf == nil){
			fprint(2, "out of memory for jpeg\n");
			w->run = 0;
			break;
		}

		Binits(&jbio, -1, OWRITE, jpegbuf, jpegmax);

		jerr = memwritejpg(&jbio, rgb, "", 0, 0);
		if(jerr != nil){
			fprint(2, "jpg(1) sucks: %s\n", jerr);
			w->run = 0;
			break;
		}

		off = Boffset(&jbio);

		/* stuff a few bits, because JPEG EOI doesn't seem to always be at off */
		off += 8;
		off = (off + 7) & ~7;

		IOchunk c;
		c.addr = jpegbuf;
		c.len = off;

		if(nbsend(w->vchan, &c) == 0)
			free(jpegbuf);

		freememimage(rgb);

		vlong ms = (nsec()-now)/1000000LL;

		if(debug) fprint(2, "frametime: %lld ms %lld fps\n", ms, 1000/ms);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-R framerate] [-s] -p source [output.avi]\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	Biobuf *bout;
	int afd = -1, pfd, abuflen;
	char *abuf, *picfile = nil, *audiofile = "/dev/audio";
	Memimage *img;
	AVIMainHeader mh;
	WAVEFORMATEX wfmt;
	BITMAPINFO bih;

	memimageinit();

	ARGBEGIN{
	case 'R':
		framerate = atoi(EARGF(usage()));
		break;
	case 's':
		silence = 1;
		break;
	case 'p':
		picfile = EARGF(usage());
		break;
	case 'D':
		debug++;
		break;
	default:
		usage();
	}ARGEND

	if(picfile == nil)
		usage();

	if(argc >= 2)
		usage();

	if(argc == 1)
		outfile = argv[0];

	if(framerate <= 0 || framerate >= 30)
		sysfatal("implausable frame rate: %d", framerate);

	threadnotify(mynotify, 1);

	bout = Bopen(outfile, OWRITE);
	if(bout == nil)
		sysfatal("opening %s: %r", outfile);

	abuflen = 2*2*AHZ/framerate;
	abuf = mallocz(abuflen, 1);
	if(abuf == nil)
		sysfatal("allocating audio buffer: %r");

	wfmt.tag = 1; /* PCM */
	wfmt.channels = 2;
	wfmt.samplespersec = AHZ;
	wfmt.avgbytespersec = 2*2*AHZ;
	wfmt.blockalign = 2*2;
	wfmt.bitspersample = 16;
	wfmt.extrasize = 0;

	pfd = open(picfile, OREAD);
	if(pfd < 0)
		sysfatal("open %s: %r", picfile);
	img = readmemimage(pfd);
	if(img == nil)
		sysfatal("readmemimage: %r");

	bih.size = 0;
	bih.width = Dx(img->r);
	bih.height = Dy(img->r);
	bih.planes = 1;
	bih.bitcount = 0;
	bih.compression = FOURCC('M','J','P','G');
	bih.sizeimage = 0;
	bih.xppm = 0;
	bih.yppm = 0;
	bih.clrused = 0;
	bih.clrimportant = 0;

	mh.microsperframe = 1000000/framerate;
	mh.maxbytespersec = 1000000;
	mh.reserved1 = 0;
	mh.flags = 0x110; /* indexed/interleaved */
	mh.totalframes = 0;
	mh.initialframes = 0;
	mh.streams = 2;
	mh.suggestedbuffersize = 1024*1024;
	mh.width = bih.width;
	mh.height = bih.height;
	memset(mh.reserved2, 0, sizeof(mh.reserved2));

	if(silence == 0){
		afd = open(audiofile, OREAD);
		if(afd < 0)
			sysfatal("open %s: %r", audiofile);
	}

	AVIWriter avw;

	memset(&avw, 0, sizeof(avw));
	avw.run = 1;
	avw.bio = bout;
	avw.afd = afd;
	avw.pfd = pfd;
	avw.movisz = 0;
	avw.idx = calloc(1000, sizeof(*avw.idx));
	avw.cidx = 0;
	avw.nidx = 1000;
	avw.vchan = chancreate(sizeof(IOchunk), 4);

	avw.vpid = proccreate(vidproc, &avw, mainstacksize);

	if(aviheader(&avw, &mh, &bih, &wfmt) < 0)
		sysfatal("aviheader: %r");

	print("press del to stop recording\n");

	while(avw.run == 1 && intr == 0){
		if(aviwriteaudio(&avw, abuf, abuflen) < 0)
			sysfatal("write audio: %r");

		if(aviwritevideo(&avw) < 0)
			sysfatal("write video: %r");
	}

	print("shutting down and finalizing video..\n");

	avw.run = 0;
	waitpid();

	mh.totalframes = avw.vframes;

	/* finalize index */
	long indexsize = aviwriteindex(&avw);
	if(indexsize < 0)
		sysfatal("aviwriteindex: %r");

	avw.indexsz = indexsize;

	if(aviheader(&avw, &mh, &bih, &wfmt) < 0)
		sysfatal("aviheader: %r");

	Bterm(bout);

	threadexitsall(nil);
}
