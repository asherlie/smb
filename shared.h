#define _GNU_SOURCE

/* TODO: consolidate these two sets of macros */
#define MSG_CREATE_THREAD 2
#define MSG_REMOVE_THREAD 3
#define MSG_REPLY_THREAD  4
// #define MSG_RNAME_UP      4

#define MSGTYPE_NOTIF     0 
#define MSGTYPE_MSG       1
// #define MSGTYPE_RNAME_UP  2

/* these two msg types update unknown labels */
#define MSG_RNAME_UP_REQ  5
#define MSG_RNAME_UP_INF  6

#define MSG_JOIN_NOTIF    7
#define MSG_EXIT_NOTIF    8

#define ANSI_RED   "\x1B[31m"
#define ANSI_NON   "\x1b[0m"
#define ANSI_GRE   "\x1b[32m"
#define ANSI_BLU   "\x1b[34m"
#define ANSI_MGNTA "\x1b[35m"

struct mb_msg{
      int mb_inf[2];
      char str_arg[201];
};

int listen_sock();
