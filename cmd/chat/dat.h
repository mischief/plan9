enum
{
	NHIST	= 1000,
};

typedef struct Wind Wind;
struct Wind
{
	QLock;
	u64int		id;
	Mousectl	*mouse;
	Keyboardctl	*keyboard;
	Controlset	*cs;
	Control		*column;
	Control		*top;
	Control		*body;
	Control		*input;

	Channel		*event;		/* char* */

	int			blines;
	char		*target;

	Wind		*prev;
	Wind		*next;
};

typedef struct Conn Conn;
struct Conn
{
	QLock;
	char	*dial;
	void	(*reconnect)(Conn*);
	int		fd;
	Channel *out;	/* char* */
	int		rpid;
	Ioproc	*wp;
};
