struct msg_stack_entry{
      char msg[201];
      uid_t sender;
};

/* hashed array of thread linked lists */
struct thread_lst{
      uid_t creator;
      int ref_no, n_msg, msg_stack_cap;
      char label[50];

      /* base ptr is stored for reallocs since offset is changed 
       * upon entry removal 
       */
      struct msg_stack_entry* msg_stack_base;
      struct msg_stack_entry* msg_stack;

      /* this THREAD reads notifications sent from host.c */
      // don't need this thread here - just need one to read
      // updates from host
       pthread_mutex_t thread_msg_stack_lck;

      struct thread_lst* next;
};

struct th_hash_lst{
      struct thread_lst** threads;

      /* n is used only to keep track of in_use */
      int bux, n, * in_use;
};

struct read_notif_pth_arg{
      int sock;
      struct th_hash_lst* thl;
};

_Bool client(char* sock_path);
