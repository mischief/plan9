#include <u.h>
#include <libc.h>
#include <bio.h>

#include "irc.h"

typedef struct Hook Hook;
struct Hook
{
	char what[64];
	IrcCb *cb;
	Hook *next;
};

struct Irc
{
	QLock;

	IrcConf *conf;

	int fd;
	Biobuf *buf;
	Hook *head;

	int run;
};

IrcMsg*
parsemsg(char *b)
{
	char *tok[5], *args[20];
	int i, n, ntok, nargs;
	IrcMsg *msg;

	if(b == nil || b[0] == 0)
		return nil;

	msg = mallocz(sizeof(*msg), 1);
	if(msg == nil)
		return nil;

	if(b[0] == ':'){
		ntok = gettokens(b, tok, 2, " ");
		if(ntok != 2)
			goto fail;
		strncpy(msg->prefix, tok[0]+1, sizeof(msg->prefix)-1);
		b = tok[1];
	}

	ntok = gettokens(b, tok, 2, ":");

	nargs = gettokens(tok[0], args, nelem(args), " ");

	strncpy(msg->cmd, args[0], sizeof(msg->cmd)-1);
	n = strlen(msg->cmd);
	for(i = 0; i < n; i++){
		msg->cmd[i] = tolower(msg->cmd[i]);
	}

	msg->nargs = nargs - 1 + (ntok > 1 ? 1 : 0);

	msg->args = mallocz(msg->nargs * sizeof(char*), 1);
	if(msg->args == nil)
		goto fail;

	for(i = 0; i < msg->nargs - (ntok > 1 ? 1 : 0); i++){
		msg->args[i] =  strdup(args[i+1]);
	}

	if(ntok > 1){
		msg->args[i] = strdup(tok[1]);
	}

/*
	fprint(2, "prefix: %s\n", msg->prefix);
	fprint(2, "command: %s\n", msg->cmd);
	for(i = 0; i < msg->nargs; i++){
		fprint(2, " %s\n", msg->args[i]);
	}
*/

	return msg;
fail:
	free(msg);
	return nil;
}

void
freemsg(IrcMsg *m)
{
	int i;

	assert(m != nil);

	if(m->nargs > 0){
		for(i = 0; i < m->nargs; i++)
			free(m->args[i]);
		free(m->args);
	}

	free(m);
}

void
pinghandler(Irc *irc, IrcMsg *msg, void*)
{
	if(msg->nargs > 0)
		ircrawf(irc, "PONG :%s", msg->args[0]);
}

Irc*
ircinit(IrcConf *conf)
{
	Irc *c;

	c = mallocz(sizeof(*c), 1);
	if(c == nil)
		return nil;

	c->conf = conf;
	c->fd = -1;
	c->run = 1;

	if(irchook(c, IRC_PING, pinghandler) < 0)
		goto bad1;

	return c;

bad1:
	free(c);
	return nil;
}

void
ircfree(Irc *irc)
{
	Hook *h, *next;

	for(h = irc->head, next = h->next; next != nil; h = next, next = next->next){
		free(h);
	}

	if(irc->buf)
		Bterm(irc->buf);
}

static void
callhook(Irc *irc, char *what, IrcMsg *msg){
	Hook *h;
	IrcMsg fake;

	if(msg == nil){
		msg = &fake;
		memset(msg, 0, sizeof(*msg));
		strcpy(msg->cmd, what);
	}

	for(h = irc->head; h != nil; h = h->next)
		if(h->what[0] == 0 || cistrcmp(what, h->what) == 0)
			h->cb(irc, msg, irc->conf->aux);
}

int
ircrun(Irc *irc)
{
	int l;
	char *p;
	IrcMsg *msg;
	Biobuf *buf;

	buf = nil;

	for(;;){
		qlock(irc);
		if(!irc->run)
			break;
		qunlock(irc);

		switch(irc->fd){
		case -1:
			callhook(irc, IRC_DIALING, nil);

			if(irc->conf->dial != nil)
				irc->fd = irc->conf->dial(irc, irc->conf);
			else
				irc->fd = dial(irc->conf->address, nil, nil, nil);

			if(irc->fd >= 0){
				buf = Bfdopen(irc->fd, OREAD);
				if(buf == nil){
					callhook(irc, IRC_OOM, nil);
					break;
				}

				callhook(irc, IRC_CONNECT, nil);
			}

			sleep(5000);

			break;
		default:
			p = Brdstr(buf, '\n', 1);
			if(p == nil){
				// eof/maybe oom?
				Bterm(buf);
				buf = nil;
				irc->fd = -1;
				callhook(irc, IRC_DISCONNECT, nil);
				break;
			}

			l = Blinelen(buf);
			if(l > 0 && p[l-1] == '\r')
				p[l-1] = 0;

			if(irc->conf->debug)
				fprint(2, "<< %s\n", p);

			msg = parsemsg(p);
			free(p);
			if(msg == nil){
				callhook(irc, IRC_OOM, nil);
				break;
			}

			if(strcmp(msg->cmd, IRC_PING) == 0){
				ircrawf(irc, "PONG: %s", msg->args[0]);
			}

			callhook(irc, msg->cmd, msg);

			freemsg(msg);

			break;
		}
	}

	if(buf != nil)
		Bterm(buf);

	return 0;
}

int
irchook(Irc *irc, char *what, IrcCb *cb)
{
	Hook *h;

	h = mallocz(sizeof(*h), 1);
	if(h == nil)
		return -1;

	if(what == nil)
		what = "";

	snprint(h->what, sizeof(h->what), "%s", what);
	h->cb = cb;
	h->next = irc->head;

	irc->head = h;

	return 0;
}

IrcConf*
ircconf(Irc *irc)
{
	return irc->conf;
}

void
ircterminate(Irc *irc)
{
	qlock(irc);
	irc->run = 0;
	close(irc->fd);
	qunlock(irc);
}

int
ircraw(Irc *irc, char *msg)
{
	int rv;

	if(irc->conf->debug)
		fprint(2, ">> %s\n", msg);

	rv = write(irc->fd, msg, strlen(msg));
	if(rv > 0)
		rv = write(irc->fd, "\r\n", 2);
	return rv;
}

int
ircrawf(Irc *irc, char *fmt, ...)
{
	char tmp[512];
	va_list arg;

	va_start(arg, fmt);
	
	tmp[0] = 0;
	vsnprint(tmp, sizeof(tmp), fmt, arg);

	return ircraw(irc, tmp);
}

int
ircquit(Irc *irc, char *why)
{
	return ircrawf(irc, "QUIT :%s", why);
}

int
ircuser(Irc *irc, char *user, char *mode, char *realname)
{
	return ircrawf(irc, "USER %s %s * :%s", user, mode, realname);
}

int
ircnick(Irc *irc, char *nickname)
{
	return ircrawf(irc, "NICK %s", nickname);
}

int
ircjoin(Irc *irc, char *channel)
{
	return ircrawf(irc, "JOIN %s", channel);
}

int
ircpart(Irc *irc, char *channel)
{
	return ircrawf(irc, "PART %s", channel);
}

int
ircprivmsg(Irc *irc, char *tgt, char *msg)
{
	return ircrawf(irc, "PRIVMSG %s :%s", tgt, msg);
}
