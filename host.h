struct notif_arg{
      int n_peers, * socks, ref_no;
      _Bool msg_buf;
      char msg[201];
};

void create_mb(char* name);
/* peers must be a -1 terminated array of sockets */
_Bool notify(int* peers, int n_peers, int ref_no, char* msg);
