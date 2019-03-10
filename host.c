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

void log_f(char* msg){
      #ifndef ASH_DEBUG
      FILE* fp = fopen("LOGFILE", "a");
      fputs(msg, fp);
      fputc('\n', fp);
      fclose(fp);
      return;
      #endif
      puts(msg);
}

void log_f_int(int i){
      char num[20] = {0};
      sprintf(num, "%i", i);
      log_f(num);
}

/* TODO: throw this in a struct */
int* peers, n_peers, peer_cap;
pthread_mutex_t peer_mut;

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

/* arg->socks will be maintained in the host loop @ create_mb */
void* notify_pth(void* v_arg){
      log_f("notify_pth called");
      struct notif_arg* arg = (struct notif_arg*)v_arg;
      pthread_mutex_lock(&peer_mut);
      for(int i = 0; i < arg->n_peers; ++i){
            /* sends ref no, message contents
             * message contents will be NULL
             * if this is a thread creation
             * notification
             */
            /* MSGTYPE */
            log_f("sending msgtype");
            send(arg->socks[i], &arg->msg_type, sizeof(int), 0);
            /* sending ref_no */
            log_f("ref no");
            send(arg->socks[i], &arg->ref_no, sizeof(int), 0);
            log_f("msg");
            send(arg->socks[i], arg->msg, 200, 0);
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

      pthread_t pth;
      pthread_create(&pth, NULL, &notify_pth, &arg);
      log_f("about to join");
      return !pthread_join(pth, NULL);
}

_Bool mb_handler(int mb_type, int ref_no, char* str_arg){
      switch(mb_type){
            case MSG_CREATE_THREAD:
                  log_f("thread created with string:");
                  log_f(str_arg);
                  log_f("end_str");
                  // spread_thread_notif(peers, n_peers, ref_no, str_arg);
                  spread_thread_notif(peers, n_peers, u_ref_no++, str_arg);
                  break;
            case MSG_REMOVE_THREAD:
                  /* only she who created a thread can delete it */
                  log_f("thread with following number removed");
                  log_f_int(ref_no);
                  break;
            case MSG_REPLY_THREAD:
                  spread_msg(peers, n_peers, ref_no, str_arg);
                  break;
            default: return 0;
      }
      log_f("the following str_arg was recvd");
      log_f(str_arg);
      return 1;
}

void init_host(){
      peer_cap = 20;
      n_peers = 0;
      peers = malloc(sizeof(int)*peer_cap);
      /* TODO: destroy this */
      pthread_mutex_init(&peer_mut, NULL);
}

void* read_cl_pth(void* peer_sock_v){
      int peer_sock = *((int*)peer_sock_v);
      int mb_inf[2] = {-1, -1}; char str_buf[201];

      /* TODO: zero peer array on exit */
      while(peer_sock){
            memset(str_buf, 0, 201);
            if(read(peer_sock, mb_inf, sizeof(int)*2) <= 0)break;
            if(read(peer_sock, str_buf, 200) <= 0)break;

            log_f("read mb_inf: ");
            log_f_int(mb_inf[0]);
            log_f_int(mb_inf[1]);
            log_f("read str_buf: ");
            log_f(str_buf);
            mb_handler(mb_inf[0], mb_inf[1], str_buf);
      }
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

      pthread_t read_cl_pth_pth;
      /* is this too hacky? should i just malloc some mem? */
      /* this means that i need to guarantee peers doesn't have entries removed/rearranged */
      pthread_create(&read_cl_pth_pth, NULL, &read_cl_pth, (peers+n_peers)-1);
      pthread_detach(read_cl_pth_pth);

      pthread_mutex_unlock(&peer_mut);
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
      
      init_host();
      /* TODO: find way to safely detach/stop thread */
      pthread_t add_host_pth_pth;
      pthread_create(&add_host_pth_pth, NULL, &add_host_pth, &sock);

      /* this will keep host waiting indefinitely */
      pthread_join(add_host_pth_pth, NULL);
      // pthread_detach(add_host_pth_pth);
      
      return 1;
}
