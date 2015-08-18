#include <u.h>
#include <libc.h>

u64int jenkinshash(char *key, int len)
{
	u64int hash, i;
	for(hash = i = 0; i < len; ++i){
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	return hash;
}

void
fwrites(char *f, char *s)
{
	int fd;
	fd = open(f, OWRITE);
	if(fd > 0){
		write(fd, s, strlen(s));
		close(fd);
	}
}

void
riolabel(char *label)
{
	fwrites("/dev/label", label);
}

void
rioclose(void)
{
	fwrites("/dev/wctl", "delete");
}

/* copied from /sys/src/libdraw/newwindow.c to avoid linking in libdraw. */
int
riowindow(char *str)
{
	int fd;
	char *wsys;
	char buf[256];

	wsys = getenv("wsys");
	if(wsys == nil)
		return -1;
	fd = open(wsys, ORDWR);
	if(fd < 0){
		free(wsys);
		return -1;
	}
	rfork(RFNAMEG);
	unmount(wsys, "/dev");	/* drop reference to old window */
	free(wsys);
	if(str)
		snprint(buf, sizeof buf, "new %s", str);
	else
		strcpy(buf, "new");
	if(mount(fd, -1, "/mnt/wsys", MREPL, buf) < 0)
		return mount(fd, -1, "/dev", MBEFORE, buf);
	return bind("/mnt/wsys", "/dev", MBEFORE);
}