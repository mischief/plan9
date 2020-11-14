#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include <mp.h>
#include <libsec.h>

#include "irc.h"

#include "dat.h"
#include "fns.h"

static Wind *mainwind;		/* server window */
static Irc *conn;			/* our connection */
static char *nick;

static void windevents(Wind*);

static void
windproc(void *v)
{
	char *name;
	Wind *w;

	name = v;

	threadsetname("wind %s", name);

	/* copy ns since we fiddle with the namespace for graphics */
	rfork(RFNAMEG);

	if(riowindow("-scroll -dy 200") < 0)
		sysfatal("newwindow: %r");

	riolabel(name);

	w = windmk(name);
	windlink(mainwind, w);

	free(name);

	windevents(w);

	windunlink(mainwind, w);
	if(chanclosing(w->event) == -1)
		chanclose(w->event);
	windfree(w);

	rioclose();
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
	CMnick,		"n",		2,
	CMnick,		"nick",		2,
};

static void
windevents(Wind *w)
{
	char *s, *name;
	Cmdbuf *cb;
	Cmdtab *ct;

	while((s = recvp(w->event)) != nil){
		if(s[0] == '/' && strlen(s) > 1){
			cb = parsecmd(s+1, strlen(s+1));
			ct = lookupcmd(cb, inputtab, nelem(inputtab));
			if(ct == nil){
				windappend(w, "* invalid command %q", cb->f[0]);
				continue;
			}

			switch(ct->index){
			case CMexit:
				free(cb);
			part:
				if(w->target != nil && w->target[0] == '#')
					ircpart(conn, w->target);
				goto done;
				break;

			case CMquery:
				if(cb->f[1][0] == '#')
					ircjoin(conn, cb->f[1]);

				name = strdup(cb->f[1]);
				proccreate(windproc, name, 8192);
				break;

			case CMnick:
				ircnick(conn, cb->f[1]);
				break;
			}
			free(cb);
		} else {
			if(w->target != nil){
				ircprivmsg(conn, w->target, s);
			} else {
				ircraw(conn, s);
			}
		}
		free(s);
	}

	/* make sure to part on rio del */
	if(w->target != nil && w->target[0] == '#')
		goto part;

done:
	return;
}

static void
on433(Irc *irc, IrcMsg*, void*)
{
	char buf[128];

	snprint(buf, sizeof buf, "%s%d", nick, 10+nrand(1000));
	windappend(mainwind, "nickname %q in use, switching to %q", nick, buf);

	ircnick(irc, buf);
}

// NOTICE/PRIVMSG
static void
msg(Irc*, IrcMsg *m, void*)
{
	int r;
	char buf[512], prefix[256];
	char *s;
	Wind *w;

	/* user privmsg */
	strcpy(prefix, m->prefix);
	if((s = strchr(prefix, '!')) != nil)
		*s = 0;

	snprint(buf, sizeof buf, "%s>", prefix);
	for(r = 1; r < m->nargs; r++){
		strcat(buf, " ");
		strcat(buf, m->args[r]);
	}

	if(m->args[0][0] == '#'){
		w = windfind(mainwind, m->args[0]);
	} else {
		w = windfind(mainwind, prefix);
	}

	if(w != nil){
		windappend(w, buf);
	} else {
		windappend(mainwind, buf);
	}
}

static int
inset(char *needle, char **haystack){
	char **p;
	for(p = haystack; *p != nil; (*p)++)
		if(cistrcmp(needle, *p) == 0)
			return 0;

	return -1;
}

static void
catchall(Irc*, IrcMsg *m, void*)
{
	int i;
	char buf[512];

	if(m->cmd[0] == '_')
		return;

	char *unwanted[]={
		IRC_PING,
		IRC_NOTICE,
		IRC_PRIVMSG,
		nil,
	};

	if(inset(m->cmd, unwanted) == 0)
		return;

	snprint(buf, sizeof buf, "%s>", m->prefix);
	for(i = 1; i < m->nargs; i++){
		strcat(buf, " ");
		strcat(buf, m->args[i]);
	}

	windappend(mainwind, buf);
}

static void
ondisconnect(Irc *, IrcMsg*, void*)
{
	windappend(mainwind, "* disconnected *");
}

static void
onconnect(Irc *irc, IrcMsg*, void*)
{
	Wind *w;

	windappend(mainwind, "* connected *");
	ircuser(irc, "none", "0", "none");
	ircnick(irc, nick);;

	qlock(mainwind);
	for(w = mainwind; w != nil; w = w->next){
		if(w->target != nil && w->target[0] == '#')
			ircjoin(irc, w->target);
	}
	qunlock(mainwind);
}

static void
ircproc(void *v)
{
	Irc *irc;

	irc = v;

	procsetname("irc %s", ircconf(irc)->address);

	ircrun(irc);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-t] [-a address]\n", argv0);
	threadexitsall("usage");
}

static int
tlsdial(Irc *, IrcConf *conf)
{
	TLSconn c;
	int fd;

	fd = dial(conf->address, nil, nil, nil);
	if(fd < 0)
		return -1;
	return tlsClient(fd, &c);
}

void
threadmain(int argc, char *argv[])
{
	IrcConf conf = { 0 };
	char *address = getenv("irc");

	ARGBEGIN{
	case 't':
		conf.dial = tlsdial;
		break;
	case 'a':
		address = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();

	quotefmtinstall();
	srand(truerand());

	rfork(RFNOTEG);

	if(address == nil)
		address = "tcp!chat.freenode.net!6667";

	conf.address = address;
	nick = getuser();

	riolabel(address);

	mainwind = windmk(nil);

	conn = ircinit(&conf);
	irchook(conn, IRC_CONNECT, onconnect);
	irchook(conn, IRC_DISCONNECT, ondisconnect);
	irchook(conn, IRC_ERR_NICKNAMEINUSE, on433);
	irchook(conn, IRC_PRIVMSG, msg);
	irchook(conn, IRC_NOTICE, msg);
	irchook(conn, nil, catchall);
	
	proccreate(ircproc, conn, 16*1024);

	procsetname("main window");

	windevents(mainwind);

	postnote(PNGROUP, getpid(), "die yankee pig dog");
	threadexitsall(nil);
}
