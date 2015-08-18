#include <u.h>
#include <libc.h>
#include <bio.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <control.h>

#include "msg.h"
#include "dat.h"
#include "fns.h"

Wind *mainwind;		/* server window */
Conn *conn;			/* our connection */

void windevents(Wind*);

static void
windlabel(char *label)
{
	int fd;
	fd = open("/dev/label", OWRITE);
	if(fd > 0){
		write(fd, label, strlen(label));
		close(fd);
	}
}

static void
windclose(void)
{
	int fd;
	char *del = "delete";
	fd = open("/dev/wctl", OWRITE);
	if(fd > 0){
		write(fd, del, strlen(del));
		close(fd);
	}
}

static int
windgetwindow(Image **imgp, Screen **scrp)
{
	char winid[32], winname[64];
	int fd, n;

	fd = open("/dev/winid", OREAD);
	if(fd < 0)
		return -1;
	n = read(fd, winid, 32);
	close(fd);
	if(n < 0)
		return -1;
	winid[n] = 0;
	n = atoi(winid);
	snprint(winname, sizeof winname, "/dev/wsys/%d/winname", n);
	if(gengetwindow(display, winname, imgp, scrp, Refnone) < 0)
		return -1;
	return 0;
}

void
windproc(void *v)
{
	char *name;
	Image *img;
	Screen *scr;
	Wind *w;

	name = v;
	img = nil;
	scr = nil;

	threadsetname("wind %s", name);

	/* copy ns since we fiddle with the namespace for graphics */
	rfork(RFNAMEG);

	if(newwindow("-dy 100") < 0)
		sysfatal("newwindow: %r");

	if(windgetwindow(&img, &scr) < 0)
		ctlerror("resize failed: %r");

	w = windmk(img, name, name);
	resizecontrolset(w->cs);

	windlink(mainwind, w);

	windlabel(name);

	free(name);

	windevents(w);

	windunlink(mainwind, w);
	if(chanclosing(w->event) == -1)
		chanclose(w->event);
	windfree(w);

	windclose();

	threadexits(nil);
}

enum
{
	CMexit,
	CMquery,
	CMnick,
};

Cmdtab inputtab[] =
{
	CMexit,		"x",		1,
	CMexit,		"exit",		1,
	CMquery,	"q",		2,
	CMquery,	"query",	2,
};

void
windevents(Wind *w)
{
	int r;
	char *s, *f[3], *name;
	Cmdbuf *cb;
	Cmdtab *ct;

	while((s = recvp(w->event)) != nil){
		r = tokenize(s, f, nelem(f));

		if(r == 3){
			if(strcmp(f[0], "input:") == 0){
				ctlprint(w->input, "value ''");
				if(f[2][0] == '/' && strlen(f[2]) > 1){
					cb = parsecmd(f[2]+1, strlen(f[2]+1));
					ct = lookupcmd(cb, inputtab, nelem(inputtab));
					if(ct != nil){
						switch(ct->index){
						case CMexit:
							if(w->target != nil && w->target[0] == '#')
								connwrite(conn, "PART %s", w->target);
							free(cb);
							goto done;
							break;
						case CMquery:
							if(cb->f[1][0] == '#'){
								connwrite(conn, "JOIN %s", cb->f[1]);
							}
							name = strdup(cb->f[1]);
							proccreate(windproc, name, 8192);
							break;
						}
					}
					free(cb);
				} else {
					if(w->target != nil){
						connwrite(conn, "PRIVMSG %s :%s", w->target, f[2]);
						windappend(w, f[2]);
					} else {
						connwrite(conn, "%s", f[2]);
					}
				}
			}
		}
		free(s);
	}

done:
	return;
}

/*
 * msgproc processes Msg* from connproc.
 */
void
msgthread(void *v)
{
	int r;
	char *s, buf[1024], prefix[256];
	Msg *m;
	Conn *c;
	Wind *w;

	c = v;

	threadsetname("msg");

	while((m = recvp(c->out)) != nil){
			snprint(buf, sizeof buf, "%s>", m->prefix);
			for(r = 0; r < m->nargs; r++){
				strcat(buf, " ");
				strcat(buf, m->args[r]);
			}

			if(cistrcmp(m->cmd, "PING") == 0){
				if(m->nargs > 0)
					connwrite(c, "PONG :%s", m->args[0]);
			} else if((cistrcmp(m->cmd, "NOTICE") == 0 || cistrcmp(m->cmd, "PRIVMSG") == 0) && m->nargs > 1){
				if((s = strchr(m->prefix, '!')) != nil)
					*s = 0;

				snprint(buf, sizeof buf, "%s>", m->prefix);
				for(r = 1; r < m->nargs; r++){
					strcat(buf, " ");
					strcat(buf, m->args[r]);
				}

				if(m->args[0][0] == '#'){
					w = windfind(mainwind, m->args[0]);
				} else {
					/* user privmsg */
					strcpy(prefix, m->prefix);
					if((s = strchr(prefix, '!')) != nil)
						*s = 0;

					w = windfind(mainwind, prefix);
				}

				if(w != nil){
					windappend(w, buf);
				} else {
					windappend(mainwind, buf);
				}
			} else {
				windappend(mainwind, buf);
			}

			freemsg(m);
	}
}

void
onreconnect(Conn *c)
{
	char buf[256];
	Wind *w;

	snprint(buf, sizeof buf, "connected to %s", c->dial);
	windappend(mainwind, buf);
	connwrite(c, "USER none 0 * :none");
	connwrite(c, "NICK %s-%d", getuser(), getpid());

	qlock(mainwind);
	for(w = mainwind; w != nil; w = w->next){
		if(w->target != nil && w->target[0] == '#')
			connwrite(c, "JOIN %s", w->target);
	}
	qunlock(mainwind);
}

void
threadmain(int argc, char *argv[])
{
	char *addr;

	ARGBEGIN{
	}ARGEND

	rfork(RFNOTEG);

	initdraw(0, 0, argv0);
	initcontrols();

	addr = getenv("irc");
	if(addr == nil)
		addr = "tcp!chat.freenode.net!6667";

	mainwind = windmk(screen, addr, nil);
	resizecontrolset(mainwind->cs);

	conn = connmk(addr, onreconnect);
	conn->rpid = proccreate(connproc, conn, 16*1024);
	threadcreate(msgthread, conn, 8192);

	windevents(mainwind);

	/* kills readthread */
	postnote(PNGROUP, getpid(), "die yankee pig dog");
	threadexitsall(nil);
}

void
resizecontrolset(Controlset *cs)
{
	Rectangle ins, top, body, input;
	Screen *scr;

	scr = cs->screen->screen;

	if(windgetwindow(&cs->screen, &scr) < 0)
		ctlerror("resize failed: %r");
	ins = insetrect(cs->screen->r, 10);

	top = ins;
	top.max.y = top.min.y + font->height + 2;
	chanprint(cs->ctl, "top rect %R\ntop show", top);

	input = ins;
	input.min.y = ins.max.y - font->height - 2;
	chanprint(cs->ctl, "input rect %R\ninput show", input);
	
	body = ins;
	body.min.y = top.max.y + 2;
	body.max.y = input.min.y - 2;
	chanprint(cs->ctl, "body rect %R\nbody show", body);

	chanprint(cs->ctl, "column size");
	chanprint(cs->ctl, "column show");
}
