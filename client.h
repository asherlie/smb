#include <limits.h>
#include <pthread.h>

#include "ash_table.h"

struct msg_queue_entry{
      char msg[201];
      uid_t sender;
};

/* hashed array of thread linked lists */
struct room_lst{
      uid_t creator;
      int ref_no, n_msg, msg_queue_cap, msg_queue_base_sz;

      char label[50];

      /* base ptr is stored for reallocs since offset is changed 
       * upon entry removal 
       */
      struct msg_queue_entry* msg_queue_base;
      struct msg_queue_entry* msg_queue;

      /* this THREAD reads notifications sent from host.c */
      // don't need this thread here - just need one to read
      // updates from host
       pthread_mutex_t room_msg_queue_lck;

      /* bookend_rm will only be set in first and last entries of an ind
       * wasting sizeof(struct room_lst*)*(bux-2) bytes... how do i sleep at night
       */
      struct room_lst* next, * bookend_rm;
};

struct rm_hash_lst{
      uid_t me;
      char board_path[PATH_MAX+1];
      struct room_lst** rooms;

      /* n is used only to keep track of in_use */
      int bux, n, * in_use;

      /* an ash_table whose data fields are set to struct room_lst*'s 
       * to allow for fast lookup by ref_no
       */
      struct ash_table* ref_no_lookup;
};

/* passed as parameter to read_notif_pth() as well as repl_pth(), used to
 * share information between the two
 *
 * read_notif_pth() performs the following:
 *    adding rooms
 *    room lookups
 *    uname lookups
 *    setting duration info
 *
 * repl_pth() performs the following:
 *    room lookups
 *    uname lookups
 *    info about pending requests
 *    accessing duration info
 */
struct client_pth_arg{
      /* TODO: there should be a separate lock for each critical member of this struct */
      pthread_mutex_t cpa_lock;

      /* n_mem_req and dur_req are used to keep track of whether or not to print
       * alerts when they are received
       */
      _Bool n_mem_req, dur_req;

      /* dur stores most recent duration alert, dur_recvd
       * stores the time it was received
       */
      int dur;
      time_t dur_recvd;

      int sock;
      struct rm_hash_lst* rml;
      struct ash_table* uname_table;
};

_Bool client(char* sock_path);
