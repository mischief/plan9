typedef struct Wind Wind;
struct Wind
{
	QLock;
	u64int		id;
	int 		type;		/* 1 = no controls */
	char		*target;

	Channel		*event;		/* char* */

	int			kfd;		/* rio cons */
	int			kpid;		/* keyboard child */

	Wind		*prev;
	Wind		*next;
};

typedef struct Conn Conn;
struct Conn
{
	QLock;
	char	*dial;
	void	(*reconnect)(Conn*);
	char	*nick;
	int		fd;
	Channel *out;	/* char* */
	int		rpid;
	Ioproc	*wp;
};
