#include "shared.h"
#include "host.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/un.h>

int u_ref_no = 0;

/* this function does nothing is ASH_DEBUG is not defined */
void log_f(char* msg){
      #ifdef LOGF_DB
      FILE* fp = fopen("LOGFILE", "a");
      fprintf(fp, "%s%s%s\n", ANSI_RED, msg, ANSI_NON);
      fclose(fp);
      return;
      #endif
      #ifdef ASH_DEBUG
      puts(msg);
      #endif
      /* silence warnings in case ASH_DEBUG isn't defined */
      (void)msg;
}

void log_f_int(int i){
      char num[20] = {0};
      sprintf(num, "%i", i);
      log_f(num);
}

/* TODO: take these out of global space */

/* TODO: throw this in a struct */
int* peers, n_peers, peer_cap;
pthread_mutex_t peer_mut;

/* used to keep track of name update requests */
struct rname_up_cont ruc;

uid_t get_peer_cred(int p_sock){
      uid_t uid;
      #ifdef SO_PEERCRED
      socklen_t len = sizeof(struct ucred);
      struct ucred cred;
      memset(&cred, 0, sizeof(struct ucred));
      getsockopt(p_sock, SOL_SOCKET, SO_PEERCRED, &cred, &len);
      uid = cred.uid;
      #else
      gid_t gid;
      getpeereid(p_sock, &uid, &gid);
      #endif
      return uid;
}

/* TODO: this doesn't need its own thread */
/* arg->socks will be maintained in the host loop @ create_mb */
void* notify_pth(void* v_arg){
      log_f("notify_pth called");
      struct notif_arg* arg = (struct notif_arg*)v_arg;
      uid_t s_cred;

      pthread_mutex_lock(&peer_mut); 

      for(int i = 0; i < arg->n_peers; ++i){
            if(arg->socks[i] == -1)continue;
            /* sends ref no, message contents
             * message contents will be NULL
             * if this is a room creation
             * notification
             */
            log_f("sending credentials");
            /* TODO: is this sendi!ng incorrect creds? */
            s_cred = get_peer_cred(arg->socks[i]);
            if(send(arg->socks[i], &s_cred, sizeof(uid_t), 0) <= 0){
                  arg->socks[i] = -1;
                  break;
            }
            /* MSGTYPE */
            log_f("sending msgtype");
            if(send(arg->socks[i], &arg->msg_type, sizeof(int), 0) <= 0){
                  arg->socks[i] = -1;
                  break;
            }
            /* sending ref_no */
            log_f("ref no");
            if(send(arg->socks[i], &arg->ref_no, sizeof(int), 0) <= 0){
                  arg->socks[i] = -1;
                  break;
            }
            log_f("msg");
            if(send(arg->socks[i], arg->msg, 200, 0) <= 0){
                  arg->socks[i] = -1;
                  break;
            }
      }
      pthread_mutex_unlock(&peer_mut); 
      log_f("returning notify_pth");
      return NULL;
}

_Bool spread_msg(int* peers, int n_peers, int ref_no, char* msg){
      struct notif_arg arg;
      arg.socks = peers;
      arg.n_peers = n_peers;
      arg.ref_no = ref_no;
      arg.msg_buf = msg;
      arg.msg_type = MSGTYPE_MSG;
      memset(arg.msg, 0, 201);

      /* as of now, even if no meaningful data is being sent,
       * 200 empty bytes will be sent
       */
      if(arg.msg_buf)strncpy(arg.msg, msg, 200);
      log_f("spread_msg is about to send: ");
      log_f(msg);

      pthread_t pth;
      pthread_create(&pth, NULL, &notify_pth, &arg);
      return !pthread_join(pth, NULL);
}

_Bool spread_thread_notif(int* peers, int n_peers, int ref_no, char* label){
      log_f("spread_thread_notif called");
      struct notif_arg arg;
      arg.socks = peers;
      arg.n_peers = n_peers;
      arg.ref_no = ref_no;
      arg.msg_buf = label;
      arg.msg_type = MSGTYPE_NOTIF;
      memset(arg.msg, 0, 201);
      /* only 50 chars are used */
      strncpy(arg.msg, label, 50);

      /* TODO: this doesn't need to occur in a separate thread */
      pthread_t pth;
      pthread_create(&pth, NULL, &notify_pth, &arg);
      log_f("about to join");
      return !pthread_join(pth, NULL);
}

_Bool pass_rname_up_req(int* peers, int n_peers, int ref_no, int sender_sock){
      log_f("pass_rname_up_req called");
      struct notif_arg arg;
      /*
       * *socks should be first nonzero sock that isn't requester 
       * socks are added sequentially
       */
      _Bool success = 0;
      for(int i = 0; i < n_peers; ++i){
            if(peers[i] != -1 && peers[i] != sender_sock){
                  arg.socks = &peers[i];
                  success = 1;
                  break;
            }
      }
      if(!success)return 0;
      if(ruc.n == ruc.cap){
            ruc.cap *= 2;
            struct sock_pair* tmp_sp = malloc(sizeof(struct sock_pair)*ruc.cap);
            memcpy(tmp_sp, ruc.sp, sizeof(struct sock_pair)*ruc.n);
            free(ruc.sp);
            ruc.sp = tmp_sp;
      }

      /* adding sender_sock to ruc as requester,
       * paired with *arg.socks as sender
       */
      ruc.sp[ruc.n].req = sender_sock;
      ruc.sp[ruc.n++].snd = *arg.socks;

      arg.n_peers = 1;
      arg.ref_no = ref_no;
      arg.msg_buf = 0;
      arg.msg_type = MSG_RNAME_UP_REQ;
      memset(arg.msg, 0, 201);
      /* only 50 chars are used */

      pthread_t pth;
      pthread_create(&pth, NULL, &notify_pth, &arg);
      return !pthread_join(pth, NULL);
}

/* need to find a way to know who requested update */
_Bool pass_rname_up_inf(int ref_no, int sender_sock, char* label, struct rname_up_cont ruc){
      struct notif_arg arg;
      _Bool found = 0;
      int i;
      for(i = 0; i < ruc.n; ++i){
            /* TODO: handle case where multiple requesters requested from sender */
            /* TODO: just make it so that only one request struct can be in place at a time */
            if(ruc.sp[i].snd == sender_sock){
                  arg.socks = &ruc.sp[i].req;
                  found = 1;
                  break;
            }
      }
      if(!found)return 0;
      arg.n_peers = 1;
      arg.ref_no = ref_no;
      arg.msg_buf = label;
      arg.msg_type = MSG_RNAME_UP_INF;
      memset(arg.msg, 0, 201);
      strncpy(arg.msg, label, 50);

      pthread_t pth;
      pthread_create(&pth, NULL, &notify_pth, &arg);
      _Bool ret = !pthread_join(pth, NULL);

      --ruc.n;
      for(int j = i; j < ruc.n; ++j)ruc.sp[j] = ruc.sp[j+1];

      return ret;
}

int assign_ref_no(){
      /*
       * the only other place u_ref_no's value is changed is
       * within a lock on peer_mut
       * this mutex is being used to avoid creating an additional
       * one
       */
      pthread_mutex_lock(&peer_mut);
      int ret = u_ref_no;
      ++u_ref_no;
      pthread_mutex_unlock(&peer_mut);
      return ret;
}

/* TODO: add petition functionality!! */
/* pea should be compatible with no alterations */
_Bool mb_handler(int mb_type, int ref_no, char* str_arg, int sender_sock){
      switch(mb_type){
            case MSG_CREATE_THREAD:
                  log_f("room created with string:");
                  log_f(str_arg);
                  log_f("end_str");
                  spread_thread_notif(peers, n_peers, assign_ref_no(), str_arg);
                  break;
            /* TODO: thread removal */
            case MSG_REMOVE_THREAD:
                  /* only she who created a room can delete it */
                  log_f("room with following number removed");
                  log_f_int(ref_no);
                  break;
            case MSG_REPLY_THREAD:
                  spread_msg(peers, n_peers, ref_no, str_arg);
                  break;
            case MSG_RNAME_UP_REQ:
                  pass_rname_up_req(peers, n_peers, ref_no, sender_sock);
                  break;
            /* pass along room name to she who requested it */
            case MSG_RNAME_UP_INF:
                  /* sender sock is sender in this case */
                  pass_rname_up_inf(ref_no, sender_sock, str_arg, ruc);
                  break;
            default: return 0;
      }
      log_f("the following str_arg was recvd");
      log_f(str_arg);
      return 1;
}

void init_peers(_Bool init_mut){
      peer_cap = 20;
      n_peers = 0;
      peers = malloc(sizeof(int)*peer_cap);
      if(!init_mut)return;
      /* TODO: destroy this */
      pthread_mutex_init(&peer_mut, NULL);
}

void init_host(){
      init_peers(1);
      ruc.n = 0;
      ruc.cap = 50;
      ruc.sp = malloc(sizeof(struct sock_pair)*ruc.cap);
}

void* read_cl_pth(void* peer_sock_v){
      int* peer_sock = ((int*)peer_sock_v);
      int mb_inf[2] = {-1, -1}; char str_buf[201];

      while(*peer_sock >= 0){
            memset(str_buf, 0, 201);
            /* TODO: should be locking peer_mut but
             * peers won't be able to be added in
             * add_host bc read() blocks
             * it's ok not to for now because we're checking
             * the return value of all read() calls
             */
            if(read(*peer_sock, mb_inf, sizeof(int)*2) <= 0)break;
            if(read(*peer_sock, str_buf, 200) <= 0)break;

            log_f("read mb_inf: ");
            log_f_int(mb_inf[0]);
            log_f_int(mb_inf[1]);
            log_f("read str_buf: ");
            log_f(str_buf);
            mb_handler(mb_inf[0], mb_inf[1], str_buf, *peer_sock);
      }
      /* setting peers[x] to 0 to avoid resizing/rearranging indices
       * of this array, since add_host uses offsets into peers as param
       * for this function 
       *
       * peers[x] is checked in notify_pth
       */

      /* *peer_sock is an entry in peers */
      *peer_sock = -1;

      /* EXPERIMENTAL FEATURE */
      /* if all peers have disconnected, u_ref_no and peers are reset */
      /* i'm hesitant to access peers from here but... */
      pthread_mutex_lock(&peer_mut);
      _Bool reinit = 1;
      for(int i = 0; i < n_peers; ++i)
            if(peers[i] >= 0){
                  reinit = 0;
                  break;
            }
      if(reinit){
            free(peers);
            init_peers(0);
            u_ref_no = 0;
      }
      pthread_mutex_unlock(&peer_mut);
      return NULL;
}

void add_host(int sock){
      pthread_mutex_lock(&peer_mut);
      if(n_peers == peer_cap){
            peer_cap *= 2;
            int* tmp_peers = malloc(sizeof(int)*peer_cap);
            memcpy(tmp_peers, peers, n_peers);
            free(peers);
            peers = tmp_peers;
      }
      peers[n_peers++] = sock;

      pthread_mutex_unlock(&peer_mut);

      pthread_t read_cl_pth_pth;
      /* is this too hacky? should i just malloc some mem? */
      /* this means that i need to guarantee peers doesn't have entries removed/rearranged */
      pthread_create(&read_cl_pth_pth, NULL, &read_cl_pth, (peers+n_peers)-1);
      pthread_detach(read_cl_pth_pth);
}

void* add_host_pth(void* local_sock_v){
      int sock = *((int*)local_sock_v);
      while(1)add_host(accept(sock, NULL, NULL));
}

/* creates an mb in the working directory */
_Bool create_mb(char* name){
      /* checking for existence of socket */
      struct stat st;
      memset(&st, 0, sizeof(struct stat));
      stat(name, &st);
      if(st.st_ino != 0){
            printf("mb: \"%s\" already exists\n", name);
            return 0;
      }
      printf("%s[BOARD_CREATE %s]%s\n", ANSI_RED, name, ANSI_NON);
      #ifndef ASH_DEBUG
      pid_t pid = fork();
      if(pid > 0){
            printf("mb spawned at pid: %i\n", pid);
            exit(EXIT_SUCCESS);
      }
      #endif
      int sock = listen_sock();
      if(sock == -1)return 0;

      struct sockaddr_un addr;
      memset(&addr, 0, sizeof(struct sockaddr_un));
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, name, sizeof(addr.sun_path));

      if(bind(sock, (struct sockaddr*)&addr, SUN_LEN(&addr)) == -1
      || listen(sock, 0) == -1)return 0;

      chmod(addr.sun_path, 0777);

      init_host();
      /* TODO: find way to safely detach/stop thread */
      pthread_t add_host_pth_pth;
      pthread_create(&add_host_pth_pth, NULL, &add_host_pth, &sock);

      /* this will keep host waiting indefinitely */
      pthread_join(add_host_pth_pth, NULL);
      // pthread_detach(add_host_pth_pth);
      
      return 1;
}
