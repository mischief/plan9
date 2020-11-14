typedef struct Wind Wind;
struct Wind
{
	QLock;
	u64int		id;
	char		*target;

	Channel		*event;		/* char* */

	int			kfd;		/* rio cons */
	int			kpid;		/* keyboard child */

	Wind		*prev;
	Wind		*next;
};
