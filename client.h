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
      int ref_no, n_msg, msg_queue_cap;

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
};

struct read_notif_pth_arg{
      _Bool n_mem_req;
      int sock;
      struct rm_hash_lst* rml;
      struct ash_table* uname_table;
};

_Bool client(char* sock_path);
