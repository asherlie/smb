#include <sys/types.h>

struct sock_pair{
      int req, snd;
};

struct rname_up_cont{
      int n, cap;
      struct sock_pair* sp;
};

struct notif_arg{
      int n_peers, * socks, ref_no, msg_type;
      uid_t sender;
      _Bool msg_buf;
      char msg[201];
};

_Bool create_mb(char* name, pid_t caller);
