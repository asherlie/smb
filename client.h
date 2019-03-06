/* hashed array of thread linked lists */
struct thread_lst{
      int ref_no;
      char label[50];

      /* this THREAD reads notifications sent from host.c */
      // don't need this thread here - just need one to read
      // updates from host
      // pthread_mutex_t thread_lst_lck;

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
