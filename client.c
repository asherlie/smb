#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "client.h"
#include "shared.h"

struct th_hash_lst init_th_hash_lst(int buckets){
      struct th_hash_lst thl;
      thl.n = 0;
      thl.bux = buckets;

      thl.threads = calloc(thl.bux, sizeof(struct thread_lst));
      thl.in_use = malloc(sizeof(int)*thl.bux);

      /*
       *pthread_t pth;
       *pthread_create(&pth, NULL, &read_notif_pth, &);
       */
      return thl;
}

/* looks up a thread by its label */
struct thread_lst* thread_lookup(struct th_hash_lst thl, char* th_name){
      int ind = *th_name % thl.bux;
      if(!thl.threads[ind])return NULL;
      for(struct thread_lst* cur = thl.threads[ind]; cur->next; cur = cur->next)
            if(strstr(thl.threads[ind]->label, th_name))return thl.threads[ind];
      return NULL;
}

/* return existence of ref_no */
_Bool add_thread_thl(struct th_hash_lst* thl, int ref_no, char* name){
      /* int ind = ref_no % thl->bux; */
      /* TODO: should more chars be summed for hashing */
      int ind = *name % thl->bux;
      struct thread_lst* cur;
      if(!thl->threads[ind])
            cur = thl->threads[ind] = malloc(sizeof(struct thread_lst));
      else
            /* TODO: DON'T ITERATE THROUGH EVERYTHING - KEEP A PTR TO LAST */
            for(cur = thl->threads[ind]; cur->next; cur = cur->next)if(cur->ref_no == ref_no)return 1;
      cur->ref_no = ref_no;
      strncpy(cur->label, name, sizeof(cur->label)-1);
      cur->next = NULL;
      return 0;
}

void* read_notif_pth(void* rnp_arg_v){
      struct read_notif_pth_arg* rnp_arg = (struct read_notif_pth_arg*)rnp_arg_v;
      int ref_no, msg_type;
      char buf[201];
      while(1){
            memset(buf, 0, 201);
            /* reading MSGTYPE */
            read(rnp_arg->sock, &msg_type, sizeof(int));
            /* reading ref no */
            read(rnp_arg->sock, &ref_no, sizeof(int));
            read(rnp_arg->sock, buf, 200);

            /* if we've received a msgtype_notif, add thread */
            if(msg_type == MSGTYPE_NOTIF)
                  add_thread_thl(rnp_arg->thl, ref_no, buf);
            else{
                  /* ref_no, string and uid_t must be returned to main thread
                   * to be checked cur_thread against and possibly printed
                   */
            }
      }
}

int send_mb_r(struct mb_msg mb_a, int sock){
      int ret = 1;
      ret &= send(sock, mb_a.mb_inf, sizeof(int)*2, 0);
      ret &= send(sock, mb_a.str_arg, 200, 0);
      return ret;
}

/* returns thread ref no */
int create_thread(char* th_name, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_CREATE_THREAD;
      mb_a.mb_inf[1] = -1;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, th_name, 200);
      return send_mb_r(mb_a, sock);
}

int reply_thread(int th_ref_no, char* msg, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_REPLY_THREAD;
      mb_a.mb_inf[1] = th_ref_no;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, msg, 200);
      return send_mb_r(mb_a, sock);
}

_Bool client(char* sock_path){
      int sock = listen_sock();
      struct sockaddr_un r_addr;
      memset(&r_addr, 0, sizeof(struct sockaddr_un));
      r_addr.sun_family = AF_UNIX;
      strncpy(r_addr.sun_path, sock_path, sizeof(r_addr.sun_path));

      if(connect(sock, (struct sockaddr*)&r_addr, sizeof(struct sockaddr_un)) == -1)
            return 0;

      struct th_hash_lst thl = init_th_hash_lst(100);

      struct read_notif_pth_arg rnpa;
      rnpa.sock = sock;
      rnpa.thl = &thl;

      pthread_t read_notif_pth_pth;
      pthread_create(&read_notif_pth_pth, NULL, &read_notif_pth, &rnpa);

      int cur_thread = -1;

      char* inp = NULL, * tmp_p;
      size_t sz = 0;
      int b_read;

/*
how should the client print messages? using sockets or reading from text file?
leaning towards sockets
each new thread needs to be spread to every user
host could spawn a notify thread each time
iterates thru a quick -1 terminated arr of sockets
passed as the pthread arg
sends a message to each contaning ref num and 200 chars
each thread in client now needs to listen() and spawn a thread to read()
no need to accept() - this is handled by host
just continuously read() an int and 200 char str
with each new read(), we add thread ref num and name to our struct
host doesn't even need a complex struct, come to think of it, it can just
authenticate users and send out thread ref nums and names
this struct can be built client-side 

maybe don't even need struct, each client can just read() and if cur_thread
!= ref_no, don't print anything
otherwise, print uid_t and message

all switch thread does is change cur_thread
it will lookup ref_num from thread name string
the struct that stores thread names and ref_nums
is the only necessary struct - it will be mutex locked
and shared between the client read thread and client while loop below

TODO: write notify function compatible with pthread_create:
      maintain peer sock array from host side

TODO: write thread name ref_num struct:
      char* th_name; int ref_num;
      pthread_mutex_lock lck;

TODO: spawn a read() thread for each client:
      we'll be reading thread ref_no, message contents
      thread should update the above struct
*/

      while((b_read = getline(&inp, &sz, stdin)) != EOF){
            inp[--b_read] = 0;
            if(*inp == '/' && b_read > 1){
                  switch(inp[1]){
                        case 't':
                              /* TODO: error handlign */
                              cur_thread = thread_lookup(thl, strchr(inp, ' ')+1)->ref_no;
                              break;
                              /* switch threads */
                        case 'c':
                              if((tmp_p = strchr(inp, ' ')))
                                    create_thread(tmp_p+1, sock);
                              break;
                  }
            }
      }
      return 1;
}
