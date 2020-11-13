#include <u.h>
#include <libc.h>
#include <thread.h>
#include "usb.h"

/*
	driver for first-generation xbox controllers.
	only tested with the 'Radica Gamester' arcade joystick.

	usage:
		xbox 10 | games/snes ....
*/

enum {
	XboxCSP		= CSP(88, 66, 0),

	DUp			= 0x01,
	DDown		= 0x02,
	DLeft		= 0x04,
	DRight		= 0x08,
	Start		= 0x10,
	Back		= 0x20,
};

#pragma pack on
typedef struct XboxMsg XboxMsg;
struct XboxMsg {
	u8int	id;
	u8int	length;
	u8int	butlo, buthi;
	u8int	a, b, x, y;
	u8int	black, white;
	u8int	lt, rt;
	s16int	lsh, lsv;
	s16int	rsh, rsv;
};
#pragma pack off

static int debug;

static void
sethipri(void)
{
	char fn[64];
	int fd;

	snprint(fn, sizeof(fn), "/proc/%d/ctl", getpid());
	fd = open(fn, OWRITE);
	if(fd < 0)
		return;
	fprint(fd, "pri 13");
	close(fd);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-d] devid\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char* argv[])
{
	int i, n, kbdin;
	char *p, *e, buf[128];
	Dev *d, *dep;
	Ep *ep;
	Usbdev *ud;
	XboxMsg msg;

	ARGBEGIN{
	case 'd':
		debug++;
		break;
	case 'D':
		usbdebug++;
		break;
	default:
		usage();
	}ARGEND;

	if(argc != 1)
		usage();

	fmtinstall('H', encodefmt);

	kbdin = open("/dev/kbdin", OWRITE);
	if(kbdin < 0)
		sysfatal("open: %r");

	d = getdev(*argv);
	if(d == nil)
		sysfatal("getdev: %r");

	ud = d->usb;
	ep = nil;

	for(i = 0; i < nelem(ud->ep); i++){
		if((ep = ud->ep[i]) == nil)
			continue;
		if(ep->type == Eintr && ep->dir == Ein && ep->iface->csp == XboxCSP)
			break;
	}

	if(ep == nil)
		sysfatal("no suitable endpoint found");

	dep = openep(d, ep->id);
	if(dep == nil)
		sysfatal("%s: openep %d: %r", d->dir, ep->id);

	if(opendevdata(dep, OREAD) < 0)
		sysfatal("%s: opendevdata: %r", dep->dir);

	threadsetname("xbox %s", dep->dir);
	sethipri();

	for(;;){
		memset(&msg, 0, sizeof(msg));
		werrstr("zero read");
		n = read(dep->dfd, &msg, dep->maxpkt);
		if(n <= 0)
			sysfatal("%s: read: %r", dep->dir);
		if(n != sizeof(msg))
			sysfatal("%s: weird xbox message", dep->dir);

		if(debug)
			fprint(2, "read %s %d %.*H\n", dep->dir, n, n, &msg);

		p = buf;
		e = p + sizeof(buf);

		p[0] = 0;

		/* D-pad */
		if(msg.butlo & DUp)
			p = seprint(p, e, "up ");
		if(msg.butlo & DDown)
			p = seprint(p, e, "down ");
		if(msg.butlo & DLeft)
			p = seprint(p, e, "left ");
		if(msg.butlo & DRight)
			p = seprint(p, e, "right ");

		/* start */
		if(msg.butlo & Start)
			p = seprint(p, e, "start ");

		/* back */
		if(msg.butlo & Back)
			p = seprint(p, e, "control ");

		/* stupid button mapping from XBox controller to the nintendo emulators */
		if(msg.a > 0)
			p = seprint(p, e, "b ");
		if(msg.b > 0)
			p = seprint(p, e, "a ");
		if(msg.x > 0)
			p = seprint(p, e, "y ");
		if(msg.y > 0)
			p = seprint(p, e, "x ");

		if(msg.lt > 0)
			p = seprint(p, e, "l1 ");
		if(msg.rt > 0)
			p = seprint(p, e, "r1 ");

		print("joy1 %s\n", buf);
	}
}
