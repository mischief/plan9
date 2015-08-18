#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <thread.h>
#include <keyboard.h>
#include <mouse.h>
#include <control.h>

#include "msg.h"
#include "dat.h"
#include "fns.h"

Conn*
connmk(char *dial, void (*reconnect)(Conn*))
{
	Conn *c;

	c = mallocz(sizeof(*c), 1);
	c->dial = strdup(dial);
	c->reconnect = reconnect;
	c->fd = -1;
	c->out = chancreate(sizeof(char*), 0);
	c->wp = ioproc();
	return c;
}

int
connwrite(Conn *c, char *fmt, ...)
{
	int r;
	char *out, buf[1024];
	va_list arg;

	va_start(arg, fmt);
	out = vseprint(buf, buf+sizeof buf, fmt, arg);
	va_end(arg);
	out = seprint(out, buf+sizeof(buf), "\r\n");

retry:
	qlock(c);
	if(c->fd < 0 || (r = iowrite(c->wp, c->fd, buf, out-buf)) < 0){
		close(c->fd);
		qunlock(c);
		yield();
		goto retry;
	}
	qunlock(c);
	return r;
}

void
connproc(void *v)
{
	char *p;
	int l;
	Biobuf in;
	Msg *m;
	Conn *c;

	c = v;

reconnect:
	qlock(c);
	if(c->fd != -1){
		close(c->fd);
		c->fd = -1;
	}

	threadsetname("conn %s", c->dial);

	c->fd = iodial(c->wp, c->dial, nil, nil, nil);
	if(c->fd < 0){
		fprint(2, "dial: %r\n");
		qunlock(c);
		goto reconnect;
	}

	Binit(&in, c->fd, OREAD);

	qunlock(c);

	/* TODO(mischief): call this in a different thread */

	c->reconnect(c);

	while((p = Brdstr(&in, '\n', 1)) != nil){
		l = Blinelen(&in);

		if(l > 0 && p[l-1] == '\r')
			p[l-1] = 0;

		m = parsemsg(p);
		free(p);
		if(m == nil){
			fprint(2, "bad msg\n");
			continue;
		}

		sendp(c->out, m);
	}
goto reconnect;
}
