#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>

#include "msg.h"
#include "dat.h"
#include "fns.h"

Wind *mainwind;		/* server window */
Conn *conn;			/* our connection */

void windevents(Wind*);

void
windproc(void *v)
{
	char *name;
	Wind *w;

	name = v;

	threadsetname("wind %s", name);

	/* copy ns since we fiddle with the namespace for graphics */
	rfork(RFNAMEG);

	if(riowindow("-dy 200") < 0)
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
};

void
windevents(Wind *w)
{
	char *s, *name;
	Cmdbuf *cb;
	Cmdtab *ct;

	while((s = recvp(w->event)) != nil){
		if(s[0] == '/' && strlen(s) > 1){
			cb = parsecmd(s+1, strlen(s+1));
			ct = lookupcmd(cb, inputtab, nelem(inputtab));
			if(ct != nil){
				switch(ct->index){
				case CMexit:
					free(cb);
				part:
					if(w->target != nil && w->target[0] == '#')
						connwrite(conn, "PART %s", w->target);
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
				connwrite(conn, "PRIVMSG %s :%s", w->target, s);
			} else {
				connwrite(conn, "%s", s);
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

	snprint(buf, sizeof buf, "reconnected to %s", c->dial);
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

	addr = getenv("irc");
	if(addr == nil)
		addr = "tcp!chat.freenode.net!6667";

	riolabel(addr);

	mainwind = windmk(nil);
	conn = connmk(addr, onreconnect);
	conn->rpid = proccreate(connproc, conn, 16*1024);
	threadcreate(msgthread, conn, 8192);

	windevents(mainwind);

	postnote(PNGROUP, getpid(), "die yankee pig dog");
	threadexitsall(nil);
}
