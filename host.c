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
      struct notif_arg* arg = (struct notif_arg*)v_arg;
      pthread_mutex_lock(&peer_mut);
      for(int i = 0; i < arg->n_peers; ++i){
            /* sends ref no, message contents
             * message contents will be NULL
             * if this is a thread creation
             * notification
             */
            /* MSGTYPE */
            send(arg->socks[i], &arg->msg_type, sizeof(int), 0);
            /* sending ref_no */
            send(arg->socks[i], &arg->ref_no, sizeof(int), 0);
            send(arg->socks[i], arg->msg, 200, 0);
      }
      pthread_mutex_unlock(&peer_mut);
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

      pthread_t pth;
      pthread_create(&pth, NULL, &notify_pth, &arg);
      return !pthread_join(pth, NULL);
}

_Bool spread_thread_notif(int* peers, int n_peers, int ref_no, char* label){
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
      return !pthread_join(pth, NULL);
}

_Bool mb_handler(int mb_type, int ref_no, char* str_arg){
      switch(mb_type){
            case MSG_CREATE_THREAD:
                  puts("thread created");
                  spread_thread_notif(peers, n_peers, ref_no, str_arg);
                  break;
            case MSG_REMOVE_THREAD:
                  /* only she who created a thread can delete it */
                  printf("thread %i removed\n", ref_no);
                  break;
            default: return 0;
      }
      printf("str arg: %s recvd\n", str_arg);
      return 1;
}

void init_host(){
      peer_cap = 20;
      n_peers = 0;
      peers = malloc(sizeof(int)*peer_cap);
      /* TODO: destroy this */
      pthread_mutex_init(&peer_mut, NULL);
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
}

/* creates an mb in the working directory */
void create_mb(char* name){
      /* checking for existence of socket */
      struct stat st;
      memset(&st, 0, sizeof(struct stat));
      stat(name, &st);
      if(st.st_ino != 0){
            printf("mb: \"%s\" already exists\n", name);
            return;
      }
      pid_t pid = fork();
      if(pid > 0){
            printf("mb spawned at pid: %i\n", pid);
            exit(EXIT_SUCCESS);
      }
      int sock = listen_sock();
      if(sock == -1)return;

      struct sockaddr_un addr;
      memset(&addr, 0, sizeof(struct sockaddr_un));
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, name, sizeof(addr.sun_path));

      if(bind(sock, (struct sockaddr*)&addr, SUN_LEN(&addr)) == -1
      || listen(sock, 0) == -1)return;

      int peer_sock = -1;
      /* recvd info */
      /* str_buf contains up to 200 chars */
      int mb_inf[2] = {-1, -1}; char str_buf[201];
      
      init_host();
      
      while(1){
            memset(str_buf, 0, 201);
            add_host(peer_sock = accept(sock, NULL, NULL));
            read(peer_sock, mb_inf, sizeof(int)*2);
            read(peer_sock, str_buf, 200);

            mb_handler(mb_inf[0], mb_inf[1], str_buf);
      }
}
