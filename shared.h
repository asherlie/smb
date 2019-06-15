#define _GNU_SOURCE

#define MSG_CREATE_THREAD 0
#define MSG_REPLY_THREAD  1

/* these two msg types update unknown labels */
#define MSG_RNAME_UP_REQ  2
#define MSG_RNAME_UP_INF  3

/* number of members request and response */
#define MSG_N_MEM_REQ     4
#define MSG_N_MEM_INF     5

/* removal of board */
#define MSG_REMOVE_BOARD  6

/* alert for duration */
#define MSG_DUR_REQ       7
#define MSG_DUR_ALERT     8
/* alert used for debugging */
/* TODO: implement ASH_DEBUG msgs */
#ifdef ASH_DEBUG
#define MSG_DEBUG_ALERT   9
#endif

#define ANSI_RED   "\x1B[31m"
#define ANSI_NON   "\x1b[0m"
#define ANSI_GRE   "\x1b[32m"
#define ANSI_BLU   "\x1b[34m"
#define ANSI_MGNTA "\x1b[35m"

#define SMB_VER    "1.3.9"

struct mb_msg{
      /* mb_inf[
       *        0: msg type
       *        1: optional room ref no
       *        2: optional room creator uid
       *         ]
       */
      /* as of now, mb_inf[2] is used only for sharing creator of a room */
      int mb_inf[3];
      char str_arg[201];
};

int listen_sock();
_Bool strtoi(const char* str, int* i);
