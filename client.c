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
_Bool add_thread_thl(struct th_hash_lst* thl, int ref_no, char* name, uid_t creator){
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
      cur->creator = creator;
      cur->ref_no = ref_no;
      strncpy(cur->label, name, sizeof(cur->label)-1);

      /* msg stack! */
      /* TODO: this should be done in a separate init_msg_queue func */
      cur->n_msg = 0;
      cur->msg_queue_cap = 50;
      /* TODO: free */
      cur->msg_queue = malloc(sizeof(struct msg_queue_entry)*cur->msg_queue_cap);
      cur->msg_queue_base = cur->msg_queue;
      /* TODO: destroy this */
      pthread_mutex_init(&cur->thread_msg_queue_lck, NULL);

      cur->next = NULL;

      return 0;
}

_Bool insert_msg_msg_queue(struct thread_lst* th, char* msg, uid_t sender){
      _Bool resz = 0;
      pthread_mutex_lock(&th->thread_msg_queue_lck);
      if(th->n_msg == th->msg_queue_cap){
            resz = 1;
            th->msg_queue_cap *= 2;
            struct msg_queue_entry* tmp = malloc(sizeof(struct msg_queue_entry)*th->msg_queue_cap);
            memcpy(tmp, th->msg_queue, sizeof(struct msg_queue_entry)*th->n_msg);
            free(th->msg_queue_base);
            th->msg_queue_base = th->msg_queue = tmp;
      }

      struct msg_queue_entry tmp_entry;
      memset(tmp_entry.msg, 0, 201);
      tmp_entry.sender = sender;
      /* memcpy'ing because we want all 200 bytes
       * this lets the user avoid clearing msg
       */
      memcpy(tmp_entry.msg, msg, 200);

      th->msg_queue[th->n_msg++] = tmp_entry;
      pthread_mutex_unlock(&th->thread_msg_queue_lck);
      return resz;
}

_Bool pop_msg_queue(struct thread_lst* th, char* msg, uid_t* sender){
      _Bool ret = 0;
      pthread_mutex_lock(&th->thread_msg_queue_lck);
      if(th->n_msg){
            *sender = (*th->msg_queue).sender;
            strncpy(msg, (*th->msg_queue).msg, 200);
            ++th->msg_queue; ++th->msg_queue_cap; --th->n_msg;
            ret = 1;
      }
      pthread_mutex_unlock(&th->thread_msg_queue_lck);
      return ret;
}

/* four reads are executed each iteration:
 *    1: uid_t sender
 *    2: int   msg_type
 *    3: int   ref_no
 *    4: char* buf
 */
void* read_notif_pth(void* rnp_arg_v){
      struct read_notif_pth_arg* rnp_arg = (struct read_notif_pth_arg*)rnp_arg_v;
      int ref_no, msg_type; uid_t uid;
      char buf[201];
      struct thread_lst* cur_th = NULL;
      while(1){
            memset(buf, 0, 201);
            /* reading uid_t of sender */
            if(read(rnp_arg->sock, &uid, sizeof(uid_t)) <= 0)break;
            /* reading MSGTYPE */
            if(read(rnp_arg->sock, &msg_type, sizeof(int)) <= 0)break;
            #ifdef ASH_DEBUG
            printf("read msg type %i\n", msg_type);
            #endif
            /* reading ref no */
            if(read(rnp_arg->sock, &ref_no, sizeof(int)) <= 0)break;
            #ifdef ASH_DEBUG
            printf("read ref_no: %i\n", ref_no);
            #endif
            if(read(rnp_arg->sock, buf, 200) <= 0)break;
            #ifdef ASH_DEBUG
            printf("string: \"%s\" read from buf\n", buf);
            #endif

            /* if we've received a msgtype_notif, add thread */
            if(msg_type == MSGTYPE_NOTIF){
                  add_thread_thl(rnp_arg->thl, ref_no, buf, uid);
                  printf("%s%i%s: %s[THREAD_CREATE %s]%s\n", ANSI_GRE, uid, ANSI_NON, ANSI_RED, buf, ANSI_NON);
            }
            /* as of now, only other msg_type is MSGTYPE_MSG */
            else{
                  // TODO: thread lookup is too slow without label
                  // TODO: should ref_no be used to hash?
                  // this is the most frequent lookup
                  cur_th = thread_lookup(*rnp_arg->thl, NULL, ref_no);
                  /* adding message to msg stack */
                  if(cur_th)insert_msg_msg_queue(cur_th, buf, uid);
                  // just update ref_no's thread entry 
                  /* ref_no, string and uid_t must be returned to main thread
                   * to be checked cur_thread against and possibly printed
                   */
            }
      }
      printf("%slost connection to board%s\n", ANSI_RED, ANSI_NON);
      return NULL;
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
                        /* both /join and /thread will join an existing thread */
                        case 'j':
                        case 't':
                              if(!(tmp_p = strchr(inp, ' ')))break;
                              cur_thread = thread_lookup(*rnp_arg->thl, tmp_p+1, -1);
                              if(!cur_thread)printf("%sno thread containing \"%s\" was found%s\n", ANSI_RED, tmp_p, ANSI_NON);
                              else printf("%scurrent thread has been switched to \"%s\"%s\n", ANSI_MGNTA, cur_thread->label, ANSI_NON);
                              break;
                              /* switch threads */
                        case 'c':
                              if((tmp_p = strchr(inp, ' '))){
                                    /* TODO:
                                     * possibly don't print this here, like with messages
                                     * user should only be alerted when the confirmation comes back from read_notif_pth()
                                     */
                                    if((tmp_ret = create_thread(tmp_p+1, rnp_arg->sock))){/* TODO */}
                                    #ifdef ASH_DEBUG
                                    printf("ret val of create thread: %i\n", tmp_ret);
                                    printf("sent to socket: %i\n", rnp_arg->sock);
                                    #endif
                              }
                              break;
                        case 'l':
                              for(int i = 0; rnp_arg->thl->in_use[i] != -1; ++i){
                                    for(struct thread_lst* tl = rnp_arg->thl->threads[rnp_arg->thl->in_use[i]]; tl; tl = tl->next)
                                          printf("%i: \"%s%s%s\"\n", tl->creator, (tl == cur_thread) ? ANSI_BLU : ANSI_NON, tl->label, ANSI_NON);
                              }
                              break;
                        case 'w':
                              if(!cur_thread)
                                    printf("%syou have not yet joined a thread%s\n", ANSI_MGNTA, ANSI_NON);
                              else 
                                    printf("%scurrent thread is \"%s\"%s\n", ANSI_MGNTA, cur_thread->label, ANSI_NON);
                              break;
                        /* exit or e[x]it */
                        case 'e':
                        case 'x':
                              if(!cur_thread)break;
                              printf("%syou have left \"%s\"%s\n", ANSI_MGNTA, cur_thread->label, ANSI_NON);
                              cur_thread = NULL;
                              break;
                  }
            }
            /* we're sending a regular message */
            else if(!cur_thread)
                  printf("%syou must first enter a thread before replying%s\n", ANSI_RED, ANSI_NON);
            else
                  reply_thread(cur_thread->ref_no, inp, rnp_arg->sock);
      }
      return NULL;
}

_Bool client(char* sock_path){
      cur_thread = NULL;
      int sock = listen_sock();

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

      /* doesn't need to be memset(*, 0, *)'d - this is handled by insert_msg_msg_queue */
      char tmp_p[201];
      uid_t s_uid;

      while(1){
            // pop_msg_queue
            // if(cur_thread && (tmp_p = pop_msg_queue(cur_thread)))puts(tmp_p);
            if(cur_thread && pop_msg_queue(cur_thread, tmp_p, &s_uid))printf("%s%i%s: %s\n", ANSI_GRE, s_uid, ANSI_NON, tmp_p);
            usleep(1000);
      }
}
