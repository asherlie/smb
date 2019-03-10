#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "client.h"
#include "shared.h"

struct thread_lst* cur_thread;

struct th_hash_lst init_th_hash_lst(int buckets){
      struct th_hash_lst thl;
      thl.n = 0;
      thl.bux = buckets;

      thl.threads = calloc(thl.bux, sizeof(struct thread_lst));
      thl.in_use = malloc(sizeof(int)*(thl.bux+1));
      memset(thl.in_use, -1, sizeof(int)*(thl.bux+1));

      /*
       *pthread_t pth;
       *pthread_create(&pth, NULL, &read_notif_pth, &);
       */
      return thl;
}

/* looks up a thread by its label or ref_no 
 * if both are provided, label is hashed
 * for lookup
 */
struct thread_lst* thread_lookup(struct th_hash_lst thl, char* th_name, int ref_no){
      if(!th_name){
            for(int i = 0; thl.in_use[i] != -1; ++i){
                  for(struct thread_lst* cur = thl.threads[thl.in_use[i]]; cur;
                      cur = cur->next){
                        if(cur->ref_no == ref_no)return cur;
                  }
            }
            return NULL;
      }
      int ind = *th_name % thl.bux;
      if(!thl.threads[ind])return NULL;
      for(struct thread_lst* cur = thl.threads[ind]; cur; cur = cur->next)
            if(strstr(thl.threads[ind]->label, th_name))return thl.threads[ind];
      return NULL;
}

/* return existence of ref_no */
_Bool add_thread_thl(struct th_hash_lst* thl, int ref_no, char* name){
      /* int ind = ref_no % thl->bux; */
      /* TODO: should more chars be summed for hashing */
      // TODO: ref_no should be used for hashing because the most
      // expensive lookup is done using ref_no from read_notif_pth
      int ind = *name % thl->bux;
      struct thread_lst* cur;
      if(!thl->threads[ind]){
            cur = thl->threads[ind] = malloc(sizeof(struct thread_lst));
            thl->in_use[thl->n++] = ind;
      }
      else
            /* TODO: DON'T ITERATE THROUGH EVERYTHING - KEEP A PTR TO LAST */
            for(cur = thl->threads[ind]; cur; cur = cur->next)if(cur->ref_no == ref_no)return 1;
      cur->ref_no = ref_no;
      strncpy(cur->label, name, sizeof(cur->label)-1);

      /* msg stack! */
      /* TODO: this should be done in a separate init_msg_stack func */
      cur->n_msg = 0;
      cur->msg_stack_cap = 50;
      /* TODO: free */
      cur->msg_stack = malloc(sizeof(char*)*cur->msg_stack_cap);
      /* TODO: destroy this */
      pthread_mutex_init(&cur->thread_msg_stack_lck, NULL);

      cur->next = NULL;

      return 0;
}

_Bool insert_msg_msg_stack(struct thread_lst* th, char* msg){
      _Bool resz = 0;
      pthread_mutex_lock(&th->thread_msg_stack_lck);
      if(th->n_msg == th->msg_stack_cap){
            resz = 1;
            char** tmp = malloc(sizeof(char*)*th->msg_stack_cap);
            memcpy(tmp, th->msg_stack, sizeof(char*)*th->n_msg);
            free(th->msg_stack);
            th->msg_stack = tmp;
      }
      th->msg_stack[th->n_msg++] = calloc(201, 1);
      strncpy(th->msg_stack[th->n_msg], msg, 200);
      pthread_mutex_unlock(&th->thread_msg_stack_lck);
      return resz;
}

char* pop_msg_stack(struct thread_lst* th){
      char* ret = NULL;
      pthread_mutex_lock(&th->thread_msg_stack_lck);
      if(th->n_msg){
            ret = th->msg_stack[--th->n_msg];
      }
      pthread_mutex_unlock(&th->thread_msg_stack_lck);
      return ret;
}

void* read_notif_pth(void* rnp_arg_v){
      struct read_notif_pth_arg* rnp_arg = (struct read_notif_pth_arg*)rnp_arg_v;
      int ref_no, msg_type;
      char buf[201];
      struct thread_lst* cur_th = NULL;
      while(1){
            memset(buf, 0, 201);
            /* reading MSGTYPE */
            read(rnp_arg->sock, &msg_type, sizeof(int));
            #ifdef ASH_DEBUG
            printf("read msg type %i\n", msg_type);
            #endif
            /* reading ref no */
            read(rnp_arg->sock, &ref_no, sizeof(int));
            #ifdef ASH_DEBUG
            printf("read ref_no: %i\n", ref_no);
            #endif
            read(rnp_arg->sock, buf, 200);
            #ifdef ASH_DEBUG
            printf("string: \"%s\" read from buf\n", buf);
            #endif

            /* if we've received a msgtype_notif, add thread */
            if(msg_type == MSGTYPE_NOTIF)
                  add_thread_thl(rnp_arg->thl, ref_no, buf);
            /* as of now, only other msg_type is MSGTYPE_MSG */
            else{
                  // TODO: thread lookup is too slow without label
                  // TODO: should ref_no be used to hash?
                  // this is the most frequent lookup
                  cur_th = thread_lookup(*rnp_arg->thl, NULL, ref_no);
                  /* adding message to msg stack */
                  if(cur_th)insert_msg_msg_stack(cur_th, buf);
                  // just update ref_no's thread entry 
                  /* ref_no, string and uid_t must be returned to main thread
                   * to be checked cur_thread against and possibly printed
                   */
            }
      }
}

int send_mb_r(struct mb_msg mb_a, int sock){
      int ret = 1;
      #ifdef ASH_DEBUG
      printf("sending: %i %i %s\n", mb_a.mb_inf[0], mb_a.mb_inf[1], mb_a.str_arg);
      #endif
      ret &= send(sock, mb_a.mb_inf, sizeof(int)*2, 0) != -1;
      ret &= send(sock, mb_a.str_arg, 200, 0) != -1;
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

void* repl_pth(void* rnp_arg_v){
      struct read_notif_pth_arg* rnp_arg = (struct read_notif_pth_arg*)rnp_arg_v;

      char* inp = NULL, * tmp_p;
      size_t sz = 0;
      int b_read, tmp_ret;

      while((b_read = getline(&inp, &sz, stdin)) != EOF){
            inp[--b_read] = 0;
            if(*inp == '/' && b_read > 1){
                  switch(inp[1]){
                        case 't':
                              /* TODO: error handling */
                              cur_thread = thread_lookup(*rnp_arg->thl, (tmp_p = strchr(inp, ' ')+1), -1);
                              if(!cur_thread)printf("no thread containing \"%s\" was found\n", tmp_p);
                              else printf("current thread has been switched to \"%s\"\n", cur_thread->label);
                              break;
                              /* switch threads */
                        case 'c':
                              if((tmp_p = strchr(inp, ' '))){
                                    /* TODO:
                                     * possibly don't print this here, like with messages
                                     * user should only be alerted when the confirmation comes back from read_notif_pth()
                                     */
                                    if((tmp_ret = create_thread(tmp_p+1, rnp_arg->sock)))
                                          printf("thread with name \"%s\" has been created\n", tmp_p+1);
                                    #ifdef ASH_DEBUG
                                    printf("reval of create thread: %i\n", tmp_ret);
                                    printf("sent to socket: %i\n", rnp_arg->sock);
                                    #endif
                              }
                              break;
                        case 'l':
                              for(int i = 0; rnp_arg->thl->in_use[i] != -1; ++i){
                                    for(struct thread_lst* tl = rnp_arg->thl->threads[rnp_arg->thl->in_use[i]]; tl; tl = tl->next)
                                          printf("%i: %s\n", tl->ref_no, tl->label);
                              }
                              break;
                  }
            }
            /* we're sending a regular message */
            else if(!cur_thread)
                  puts("you must first enter a thread before replying");
            else
                  reply_thread(cur_thread->ref_no, inp, rnp_arg->sock);
      }
      return NULL;
}

_Bool client(char* sock_path){
      cur_thread = NULL;
      int sock = listen_sock();

      // host sometimes seg faults, write to logfile to debug
      listen(sock, 0);
      struct sockaddr_un r_addr;
      memset(&r_addr, 0, sizeof(struct sockaddr_un));
      r_addr.sun_family = AF_UNIX;
      strncpy(r_addr.sun_path, sock_path, sizeof(r_addr.sun_path));

      if(connect(sock, (struct sockaddr*)&r_addr, sizeof(struct sockaddr_un)) == -1){
            printf("failed to connect to host \"%s\"\n", sock_path);
            return 0;
      }

      struct th_hash_lst thl = init_th_hash_lst(100);

      struct read_notif_pth_arg rnpa;
      rnpa.sock = sock;
      rnpa.thl = &thl;

      pthread_t read_notif_pth_pth, repl_pth_pth;
      /* TODO: fix possible synch issues from sharing rnpa.thl */
      pthread_create(&read_notif_pth_pth, NULL, &read_notif_pth, &rnpa);
      pthread_create(&repl_pth_pth, NULL, &repl_pth, &rnpa);

      char* tmp_p;

      while(1){
            // TODO: sender info should be recvd
            // pop_msg_stack
            if(cur_thread && (tmp_p = pop_msg_stack(cur_thread)))puts(tmp_p);
            usleep(1000);
      }
}
