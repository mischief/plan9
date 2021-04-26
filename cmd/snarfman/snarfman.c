#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <plumb.h>
#include <String.h>
#include <libsec.h>
#include <nuklear.h>

enum {
	SNARFMAX = 100*1024,
};

typedef struct Snarf Snarf;
struct Snarf {
	IOchunk;
	int nl;
	char ts[30];
	uchar digest[MD5dlen];
	String *frobbed;
};

/* knobs */
int maxlines = 10;
int sleeptime = 500;
int maxsnarfs = 9;

int nsnarfs = 0;
int snarfidx = 0;
Snarf **snarfs;
char *snarfloc = "/dev/snarf";
int mainstacksize = 128*1024;

void
countnl(Snarf *snf)
{
	uchar *p, *ep;

	p = snf->addr;
	ep = p + snf->len;
	snf->nl = 0;

	while(p != nil){
		p = memchr(p, '\n', ep-p);
		if(p != nil){
			p++;
			snf->nl++;
		}
	}
}

void
frobinate(Snarf *snf)
{
	char *p, *tp, *ep;
	String *str;

	p = snf->addr;
	ep = p + snf->len;
	str = s_newalloc(snf->len);

	while(p != ep){
		tp = memchr(p, '\t', ep-p);
		if(tp == nil)
			break;

		str = s_memappend(str, p, tp-p);
		str = s_append(str, "    ");
		p = tp+1;
	}

	if(p != ep)
		str = s_memappend(str, p, ep-p);

	snf->frobbed = str;
}

void
plumbsnarf(Snarf *snf)
{
	Plumbmsg m;
	static int plumbfd = -1;

	if(plumbfd == -1){
		plumbfd = plumbopen("send", OWRITE);
	}

	if(plumbfd == -1){
		fprint(2, "plumbopen: %r");
		return;
	}

	memset(&m, 0, sizeof(m));
	m.src = "snarfman";
	m.type = "text";
	m.ndata = snf->len;
	m.data = snf->addr;

	if(plumbsend(plumbfd, &m) < 0)
		fprint(2, "plumbsend: %r");
}

void
putsnarf(Snarf *snf)
{
	int fd;
	long rv;
	char *p, *ep;

	fd = open(snarfloc, OWRITE);
	if(fd == 0)
		return;

	p = snf->addr;
	ep = p + snf->len;

	while(p < ep){
		rv = write(fd, p, ep-p);
		if(rv <= 0)
			break;
		p += rv;
	}

	close(fd);
}

Snarf*
getsnarf(void)
{
	void *p;
	int fd, n;
	Snarf *snf;
	Tm *t;

	fd = open(snarfloc, OREAD);
	if(fd < 0)
		return nil;

	snf = malloc(sizeof(*snf));
	if(snf == nil){
		close(fd);
		return nil;
	}

	snf->addr = malloc(SNARFMAX);
	if(snf->addr == nil){
		free(snf);
		close(fd);
		return nil;
	}

	if((n = readn(fd, snf->addr, SNARFMAX)) < 0){
		free(snf->addr);
		free(snf);
		close(fd);
		return nil;
	}

	close(fd);

	/* shrink to fit */
	p = realloc(snf->addr, n);
	if(p == nil){
		free(snf->addr);
		free(snf);
		return nil;
	}

	snf->addr = p;
	snf->len = n;

	countnl(snf);

	md5(snf->addr, snf->len, snf->digest, nil);
	
	frobinate(snf);

	t = localtime(time(nil));
	snprint(snf->ts, sizeof(snf->ts), "%02d:%02d:%02d", t->hour, t->min, t->sec);

	return snf;
}

void
freesnarf(Snarf *snf)
{
	free(snf->addr);
	s_free(snf->frobbed);
	free(snf);
}

int
hassnarf(Snarf *snf)
{
	int i;

	for(i = 0; i < nsnarfs; i++){
		if(memcmp(snf->digest, snarfs[i]->digest, MD5dlen) == 0)
			return 1;
	}

	return 0;
}

void
upsnarf(void)
{
	Snarf *snf, *old;

	snf = getsnarf();
	if(snf == nil)
		return;

	if(snf->len == 0 || hassnarf(snf)){
		freesnarf(snf);
		return;
	}

	if(nsnarfs == maxsnarfs){
		old = snarfs[snarfidx];
		freesnarf(old);
	}

	if(nsnarfs < maxsnarfs)
		nsnarfs++;

	snarfs[snarfidx] = snf;
	snarfidx = (snarfidx + 1) % maxsnarfs;
}

static void
snarfman(struct nk_context *ctx)
{
	int i, idx, n, nl, ht;
	char *str;
	Snarf *snf;

	if(nk_begin(ctx, "Snarfman", nk_rect(0, 0, Dx(screen->r), Dy(screen->r)), 0)){
		for(i = 0; i < nsnarfs; i++){
			idx = snarfidx - 1 - i;
			if(idx < 0)
				idx = nsnarfs + idx;
			snf = snarfs[idx];
			if(snf == nil)
				continue;

			nk_layout_row_begin(ctx, NK_DYNAMIC, 0, 2);
			nk_layout_row_push(ctx, 0.2f);
			nk_label(ctx, snf->ts, NK_TEXT_LEFT);
			nk_layout_row_push(ctx, 0.49f);
			nk_value_int(ctx, "Length", snf->len);
			nk_layout_row_push(ctx, 0.15f);
			if(nk_button_label(ctx, "Plumb")){
				plumbsnarf(snf);
			}
			nk_layout_row_push(ctx, 0.15f);
			if(nk_button_label(ctx, "Put")){
				putsnarf(snf);
			}
			nk_layout_row_end(ctx);

			nl = snf->nl + 1;
			if(nl > maxlines)
				nl = maxlines;

			ht = ctx->style.font->height + ctx->style.edit.row_padding;
			nk_layout_row_begin(ctx, NK_DYNAMIC, ctx->style.edit.padding.x*2 + ht*nl, 1);

			str = s_to_c(snf->frobbed);
			n = 1 + s_len(snf->frobbed);
			nk_layout_row_push(ctx, 1.0f);
			nk_edit_string(ctx, NK_EDIT_READ_ONLY|NK_EDIT_MULTILINE, str, &n, n, nil);
			nk_layout_row_end(ctx);
		}		
	}

	if(nk_window_is_closed(ctx, "Snarfman"))
		threadexitsall(nil);

	nk_end(ctx);
}

static void
kbdthread(void *v)
{
	int fd;
	long n;
	Channel *c;
	Ioproc *io;
	IOchunk chk;

	c = v;
	io = ioproc();

	if((fd = ioopen(io, "/dev/kbd", OREAD)) < 0)
		sysfatal("open: %r");

	for(;;){
		chk.addr = malloc(64);
		if(chk.addr == nil)
			sysfatal("malloc: %r");

		n = ioread(io, fd, chk.addr, 64);
		if(n < 0)
			sysfatal("read: %r");

		chk.len = n;

		send(c, &chk);
	}
}

void
timerthread(void *v)
{
	Channel *c;
	Ioproc *io;
	vlong t;

	c = v;
	io = ioproc();
	for(;;){
		iosleep(io, sleeptime);
		t = nsec();
		send(c, &t);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-s /dev/snarf] [-n maxlines] [-d ms] [-m maxsnarfs]\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	vlong t;
	IOchunk kbdchk;
	Channel *kbd, *timer;
	Mouse m;
	Mousectl *mctl;

	struct nk_user_font nkfont;
	struct nk_context sctx;
	struct nk_context *ctx = &sctx;

	ARGBEGIN{
	case 's':
		snarfloc = strdup(EARGF(usage()));
		break;
	case 'n':
		maxlines = atoi(EARGF(usage()));
		break;
	case 'd':
		sleeptime = atoi(EARGF(usage()));
		break;
	case 'm':
		maxsnarfs = atoi(EARGF(usage()));
		if(maxsnarfs <= 1)
			usage();
		break;
	default:
		usage();
	}ARGEND

	snarfs = calloc(maxsnarfs, sizeof(Snarf*));
	if(snarfs == nil)
		sysfatal("calloc: %r");

	if(initdraw(nil, nil, "snarfman") < 0)
		sysfatal("initdraw: %r");

	kbd = chancreate(sizeof(IOchunk), 0);
	timer = chancreate(sizeof(vlong), 0);

	threadcreate(kbdthread, kbd, 8192);
	threadcreate(timerthread, timer, 8192);

	mctl = initmouse(nil, screen);
	if(mctl == nil)
		sysfatal("initmouse: %r");

	/* create nuklear font from default libdraw font */
	nk_plan9_makefont(&nkfont, font);

	/* initialize nuklear */
	if(!nk_init_default(ctx, &nkfont))
		sysfatal("nk_init: %r");

	enum { ATIMER, AKBD, AMOUSE, ARESIZE, AEND };
	Alt alts[] = {
	[ATIMER]	{ timer,			&t,			CHANRCV },
	[AKBD]		{ kbd,				&kbdchk,	CHANRCV },
	[AMOUSE]	{ mctl->c,			&m,			CHANRCV },
	[ARESIZE]	{ mctl->resizec,	nil,		CHANRCV },
	[AEND]		{ nil,				nil,		CHANEND },
	};

	for(;;){
		nk_input_begin(ctx);
		switch(alt(alts)){
		case ATIMER:
			upsnarf();
			break;
		case AKBD:
			nk_plan9_handle_kbd(ctx, kbdchk.addr, kbdchk.len);
			free(kbdchk.addr);
			kbdchk.addr = nil;
			break;
		case AMOUSE:
			nk_plan9_handle_mouse(ctx, m, screen->r.min);
			break;
		case ARESIZE:
			if(getwindow(display, Refnone) < 0)
				sysfatal("getwindow: %r");
		}

		nk_input_end(ctx);

		/* handle usual plan 9 exit key */
		if(nk_input_is_key_down(&ctx->input, NK_KEY_DEL))
			threadexitsall(nil);

		snarfman(ctx);

		/* render everything */
		draw(screen, screen->r, display->black, nil, ZP);
		nk_plan9_render(ctx, screen);
		flushimage(display, 1);
	}
}

