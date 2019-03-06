#define _GNU_SOURCE

#define MSG_CREATE_THREAD 2
#define MSG_REMOVE_THREAD 3
#define MSG_REPLY_THREAD  4

#define MSGTYPE_NOTIF     0 
#define MSGTYPE_MSG       1

struct mb_msg{
      int mb_inf[2];
      char str_arg[201];
};

int listen_sock();
