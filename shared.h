#define _GNU_SOURCE

/* TODO: consolidate macros */

#define MSGTYPE_NOTIF     0 
#define MSGTYPE_MSG       1

#define MSG_CREATE_THREAD 2
#define MSG_REPLY_THREAD  4

/* these two msg types update unknown labels */
#define MSG_RNAME_UP_REQ  5
#define MSG_RNAME_UP_INF  6

/* number of members request and response */
#define MSG_N_MEM_REQ     7
#define MSG_N_MEM_INF     8

#define MSG_REMOVE_BOARD  9

#define ANSI_RED   "\x1B[31m"
#define ANSI_NON   "\x1b[0m"
#define ANSI_GRE   "\x1b[32m"
#define ANSI_BLU   "\x1b[34m"
#define ANSI_MGNTA "\x1b[35m"

#define SMB_VER    "1.1.2"

struct mb_msg{
      int mb_inf[2];
      char str_arg[201];
};

int listen_sock();
_Bool strtoi(const char* str, int* i);
