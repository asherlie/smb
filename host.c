#include "shared.h"
#include "host.h"
#include "ash_table.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <signal.h>

int u_ref_no = 0;

/* keeps track of number of boards created by each user */
/* TODO:
 *
 * should we keep track of this on a socket basis - this would
 * mean that the host will not be able to tell if a person has made
 * many other rooms if they exit and reenter smb
 *
 * this might be preferable in some ways, though, as it limits
 * the amount of information stored by host
 * 
 * although, if we're worried about the host knowing the uids of peers,
 * we need to change existing code in mb_handler()
 * as the first line of the function is:
 * `uid_t sender = get_peer_cred(sender_sock)`
 *
 * it would also be as simple to implement as a counter in read_cl_pth
 * since a thread running this is spawned with each new connection
 * this &integer would be passed into mb_handler
 */
struct ash_table uid_creation;
pthread_mutex_t uid_cre_table_lock;


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
pthread_mutex_t host_lock;

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

/* arg->socks will be maintained in the host loop @ create_mb */
/* v_arg->retval is set to 0 upon success, -1 otherwise */
/* returns whether a member has disconnected */
_Bool notify(struct notif_arg* arg){
      arg->retval = 0;

      for(int i = 0; i < arg->n_peers; ++i){
            if(arg->socks[i] == -1)continue;
            /* sends ref no, message contents
             * message contents will be NULL
             * if this is a room creation
             * notification
             */
            log_f("sending credentials");
            if(send(arg->socks[i], &arg->sender, sizeof(uid_t), 0) <= 0){
                  arg->retval = arg->socks[i] = -1;
                  continue;
            }
            /* MSGTYPE */
            log_f("sending msgtype");
            if(send(arg->socks[i], &arg->msg_type, sizeof(int), 0) <= 0){
                  arg->retval = arg->socks[i] = -1;
                  continue;
            }
            /* sending ref_no */
            log_f("ref no");
            if(send(arg->socks[i], &arg->ref_no, sizeof(int), 0) <= 0){
                  arg->retval = arg->socks[i] = -1;
                  continue;
            }
            /* sending msg */
            log_f("msg");
            if(send(arg->socks[i], arg->msg, 200, 0) <= 0){
                  arg->retval = arg->socks[i] = -1;
                  continue;
            }
      }

      return arg->retval != -1;
}

/* NUMBER OF MEMBERS IS SENT IN REF_NO */
_Bool send_mem_inf(int* peers, int n_peers){
      struct notif_arg arg;

      arg.msg_buf = 0;
      arg.msg_type = MSG_N_MEM_INF;
      arg.sender = -1;
      memset(arg.msg, 0, 201);
      arg.ref_no = 0;

      pthread_mutex_lock(&host_lock);

      arg.socks = peers;
      arg.n_peers = n_peers;
      for(int i = 0; i < n_peers; ++i)
            arg.ref_no += peers[i] != -1;

      return notify(&arg) & !pthread_mutex_unlock(&host_lock);
}

/* to get the updated duration, we'll need to
 * send an interrupt signal to this process
 * this will be caught by the while loop in create_mb()
 * and duration_adj will be updated
 */
/* CLEVER */
_Bool request_alert_dur(){
      return !kill(getpid(), SIGUSR1);
}

/* DURATION IS SENT IN REF_NO */
_Bool alert_duration(int* peers, int n_peers, int updated_dur){
      struct notif_arg arg;

      arg.msg_buf = 0;
      memset(arg.msg, 0, 201);
      arg.msg_type = MSG_DUR_ALERT;
      arg.sender = -1;

      arg.ref_no = updated_dur;

      pthread_mutex_lock(&host_lock);

      arg.socks = peers;
      arg.n_peers = n_peers;
      return notify(&arg) & !pthread_mutex_unlock(&host_lock);
}

_Bool spread_msg(int* peers, int n_peers, int ref_no, char* msg, uid_t sender_uid){
      struct notif_arg arg;

      arg.ref_no = ref_no;
      arg.msg_buf = msg;
      arg.msg_type = MSG_REPLY_ROOM;
      arg.sender = sender_uid;
      memset(arg.msg, 0, 201);

      /* as of now, even if no meaningful data is being sent,
       * 200 empty bytes will be sent
       */
      if(arg.msg_buf)strncpy(arg.msg, msg, 200);
      log_f("spread_msg is about to send: ");
      log_f(msg);

      pthread_mutex_lock(&host_lock);

      arg.socks = peers;
      arg.n_peers = n_peers;

      return notify(&arg) & !pthread_mutex_unlock(&host_lock);
}

_Bool spread_notif(int notif_type, int* peers, int n_peers,
                   int ref_no, char* label, uid_t sender_uid){
      log_f("spread_notif called");
      struct notif_arg arg;

      arg.ref_no = ref_no;
      arg.msg_buf = label;
      arg.msg_type = notif_type;
      arg.sender = sender_uid;
      memset(arg.msg, 0, 201);
      /* only 50 chars are used */
      if(arg.msg_buf)strncpy(arg.msg, label, 50);

      pthread_mutex_lock(&host_lock);

      arg.socks = peers;
      arg.n_peers = n_peers;

      return notify(&arg) & !pthread_mutex_unlock(&host_lock);
}

/* _Bool lock should be set to 1 unless the caller of
 * pass_rname_up_req() needs to lock on host_lock before
 * it calls pass_rname_up_req() and cannot release it
 * until after
 */
_Bool pass_rname_up_req(int* peers, int n_peers, int ref_no, int sender_sock, struct rname_up_cont* rupc, _Bool lock){
      log_f("pass_rname_up_req called");
      struct notif_arg arg;

      /* TODO: remove non critical sections of this function above this lock */
      /* it is critical that peers remain unchanged for this entire function */
      if(lock)pthread_mutex_lock(&host_lock);

      /*
       * *socks should point to first non -1 sock that isn't requester 
       * socks are added sequentially
       */
      _Bool success = 0;
      for(int i = 0; i < n_peers; ++i){
            if(peers[i] != -1){
                  /* since socks are added to peers sequentially,
                   * no sock added after sender_sock will be helpful 
                   */
                  if(peers[i] == sender_sock){
                        if(lock)pthread_mutex_unlock(&host_lock);
                        return 0;
                  }
                  arg.socks = &peers[i];
                  success = 1;
                  break;
            }
      }
      if(!success){
            if(lock)pthread_mutex_unlock(&host_lock);
            return 0;
      }

      /* resizing rupc if necessary */
      if(rupc->n == rupc->cap){
            rupc->cap *= 2;
            struct sock_pair* tmp_sp = malloc(sizeof(struct sock_pair)*rupc->cap);
            memcpy(tmp_sp, rupc->sp, sizeof(struct sock_pair)*rupc->n);
            free(rupc->sp);
            rupc->sp = tmp_sp;
      }

      /* adding sender_sock to ruc as requester,
       * paired with *arg.socks as sender
       */
      rupc->sp[rupc->n].req = sender_sock;
      rupc->sp[rupc->n++].snd = arg.socks;

      arg.n_peers = 1;
      arg.ref_no = ref_no;
      arg.msg_buf = 0;
      arg.msg_type = MSG_RNAME_UP_REQ;

      /* sender is unused for MSG_RNAME_UP_REQ */
      arg.sender = -1;

      memset(arg.msg, 0, 201);

      if(lock)pthread_mutex_unlock(&host_lock);
      return notify(&arg);
}

/* need to find a way to know who requested update */
_Bool pass_rname_up_inf(int ref_no, int sender_sock, char* label, uid_t creator, struct rname_up_cont* rupc){
      struct notif_arg arg;
      _Bool found = 0;
      int i;

      /* this is locked when a new peer connects */
      pthread_mutex_lock(&host_lock);

      /* this loop is finding out who we need to send info to */
      for(i = rupc->n-1; i >= 0; --i){
            /* TODO: handle case where multiple requesters requested from sender */
            /* TODO: just make it so that only one request struct can be in place at a time */
            if(*rupc->sp[i].snd == sender_sock){
                  arg.socks = &rupc->sp[i].req;
                  found = 1;
                  break;
            }
      }
      if(!found){
            pthread_mutex_unlock(&host_lock);
            return 0;
      }
      arg.n_peers = 1;
      arg.ref_no = ref_no;
      arg.msg_buf = label;
      arg.msg_type = MSG_RNAME_UP_INF;
      arg.sender = creator;
      memset(arg.msg, 0, 201);
      strncpy(arg.msg, label, 50);

      _Bool ret = notify(&arg);

      memmove(rupc->sp+i, rupc->sp+i+1, sizeof(struct sock_pair)*(--rupc->n)-i);

      pthread_mutex_unlock(&host_lock);

      return ret;
}

int assign_ref_no(){
      /*
       * the only other place u_ref_no's value is changed is
       * within a lock on host_lock
       * this mutex is being used to avoid creating an additional
       * one
       */

      pthread_mutex_lock(&host_lock);

      int ret = u_ref_no;
      ++u_ref_no;

      pthread_mutex_unlock(&host_lock);

      return ret;
}

void host_cleanup(){
      pthread_mutex_lock(&uid_cre_table_lock);

      /* we don't use free_ash_table() because we also need to free ash_table's data entry,
       * which free_ash_table doesn't do as of now
       */
      /* TODO: free_ash_table() should free data */
      for(int i = 0; i < uid_creation.bux; ++i){
            if(uid_creation.names[i]){
                  free(uid_creation.names[i]->data);
                  free(uid_creation.names[i]);
            }
      }

      pthread_mutex_unlock(&uid_cre_table_lock);

      pthread_mutex_destroy(&uid_cre_table_lock);

      pthread_mutex_lock(&host_lock);

      for(int i = 0; i < n_peers; ++i)
            if(peers[i] != -1)close(peers[i]);

      /* TODO: free this - not sure why this causes a double free error */
      /* free(ruc.sp); */

      /* ruc.sp is set to NULL to permanently stop the sleep()ing */
      ruc.sp = NULL;

      /* unlocking because not sure if UB to destroy a locked lock */
      pthread_mutex_unlock(&host_lock);

      pthread_mutex_destroy(&host_lock);
}

/* TODO: add petition functionality!! */
/* pea should be compatible with no alterations */

_Bool mb_handler(int mb_type, int ref_no, char* str_arg, int sender_sock, uid_t creator){
      uid_t sender = get_peer_cred(sender_sock);
      switch(mb_type){
            case MSG_CREATE_ROOM:
                  /* creating a scope for this block so that we can initialize a variable */
                  {

                  _Bool create = 1;

                  pthread_mutex_lock(&uid_cre_table_lock);

                  time_t* n_cre = (time_t*)lookup_data_ash_table(sender, &uid_creation);

                  /* TODO: document this behavior in readme and help menus */
                  /*
                   * n_cre stores two values:
                   * n_cre[0] = time() that begun current minute
                   * n_cre[1] = number of creations in current minute
                   */
                  time_t cur = time(NULL);
                  /* if this user has never created a board or a minute has passed */
                  if(!n_cre || cur > n_cre[0]+60){
                        if(!n_cre)insert_ash_table(sender, NULL, (n_cre = malloc(sizeof(time_t)*2)), &uid_creation);
                        n_cre[0] = cur;
                        n_cre[1] = 0;
                  }
                 
                  if((create = n_cre[1] < CRE_PER_MIN))++n_cre[1];

                  pthread_mutex_unlock(&uid_cre_table_lock);

                  /* we use _Bool create and not n_cre to determine if we can create another room
                   * because a user can be connected twice to the same board
                   * TODO: could we just use n_cre despite this because n_cre is never decremented
                   */
                  if(!create){
                        /* TODO: don't send a message each time a user tries to create a room they aren't allowed to
                         * possibly create new msgtype MSG_NO_CRE_DUR_REQ and wait to recv one to send MSG_NO_CRE_DUR
                         * or is this adding too much complexity for something that happens very infrequently?
                         */
                        spread_notif(MSG_NO_CRE_DUR, &sender_sock, 1, n_cre[0]+60, NULL, -1);
                        break;
                  }

                  log_f("room created with string:");
                  log_f(str_arg);
                  log_f("end_str");
                  spread_notif(MSG_CREATE_ROOM, peers, n_peers, assign_ref_no(), str_arg, sender);
                  }
                  break;
            case MSG_REMOVE_BOARD:
                  log_f("remove board called with following uid's");
                  log_f_int(getuid());
                  log_f_int(sender);
                  if(sender == getuid())kill(getpid(), SIGUSR2); 
                  break;
            case MSG_REPLY_ROOM:
                  spread_msg(peers, n_peers, ref_no, str_arg, sender);
                  break;
            case MSG_RNAME_UP_REQ:
                  pass_rname_up_req(peers, n_peers, ref_no, sender_sock, &ruc, 1);
                  break;
            /* pass along room name and creator to she who requested it */
            case MSG_RNAME_UP_INF:
                  /* sender sock is sender in this case */
                  pass_rname_up_inf(ref_no, sender_sock, str_arg, creator, &ruc);
                  break;
            case MSG_N_MEM_REQ:
                  /*
                   *find a way to lock on this - what if peers
                   *is changed mid call
                   */
                  /*pthread_mutex_lock(&host_lock);*/
                  send_mem_inf(peers, n_peers);
                  /*pthread_mutex_unlock(&host_lock);*/
                  break;
            case MSG_DUR_REQ:
                  request_alert_dur();
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
      pthread_mutex_init(&host_lock, NULL);
}

void init_host(){
      init_peers(1);
      ruc.n = 0;
      ruc.cap = 50;
      ruc.sp = malloc(sizeof(struct sock_pair)*ruc.cap);

      pthread_mutex_init(&uid_cre_table_lock, NULL);
      ash_table_init(&uid_creation, 20);
}

/* a thread running read_cl_pth is spawned upon each new connection */
void* read_cl_pth(void* peer_sock_v){
      int* peer_sock = ((int*)peer_sock_v);
      int mb_inf[3] = {-1, -1, -1}; char str_buf[201];

      while(*peer_sock >= 0){
            memset(str_buf, 0, 201);
            /* TODO: should be locking host_lock but
             * peers won't be able to be added in
             * add_host bc read() blocks
             * it's ok not to for now because we're checking
             * the return value of all read() calls
             */
            if(read(*peer_sock, mb_inf, sizeof(int)*3) <= 0)break;
            if(read(*peer_sock, str_buf, 200) <= 0)break;

            log_f("read mb_inf: ");
            log_f_int(mb_inf[0]);
            log_f_int(mb_inf[1]);
            log_f_int(mb_inf[2]);
            log_f("read str_buf: ");
            log_f(str_buf);
            mb_handler(mb_inf[0], mb_inf[1], str_buf, *peer_sock, mb_inf[2]);
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
      _Bool reinit = 1;

      pthread_mutex_lock(&host_lock);

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

      pthread_mutex_unlock(&host_lock);

      return NULL;
}

void add_host(int sock){

      pthread_mutex_lock(&host_lock);

      if(n_peers == peer_cap){
            peer_cap *= 2;
            int* tmp_peers = malloc(sizeof(int)*peer_cap);
            memcpy(tmp_peers, peers, sizeof(int)*n_peers);
            free(peers);
            peers = tmp_peers;
      }
      peers[n_peers++] = sock;

      pthread_mutex_unlock(&host_lock);

      pthread_t read_cl_pth_pth;
      /* is this too hacky? should i just malloc some mem?
       * this means that i need to guarantee peers doesn't have entries removed/rearranged
       */
      pthread_create(&read_cl_pth_pth, NULL, &read_cl_pth, (peers+n_peers)-1);
      pthread_detach(read_cl_pth_pth);

      pthread_mutex_lock(&host_lock);

      for(int i = 0; i < u_ref_no; ++i)
            /* even if ref_no i doesn't exist, this request can
             * be made safely, it just won't be fulfilled
             */
            pass_rname_up_req(peers, n_peers, i, sock, &ruc, 0);

      pthread_mutex_unlock(&host_lock);
}

void* add_host_pth(void* local_sock_v){
      int sock = *((int*)local_sock_v);
      while(1)add_host(accept(sock, NULL, NULL));
}

/* signal handler */
void spin(int x){(void)x;}

/* creates an mb in the working directory that will exist for duration_hrs */
/* if duration_hrs <= 0, board will exist for 5 days */
/* returns 0 if name already exists, -1 for socket issues, 
 * 1 on catastrophic error, 2 if exiting twice/success 
 */
int create_mb(char* name, int duration_hrs){
      /* checking for existence of socket */
      struct stat st;
      memset(&st, 0, sizeof(struct stat));
      stat(name, &st);
      if(st.st_ino != 0)return 0;

      /* if unspecified, sleep for 5 days */
      unsigned int duration_adj = (duration_hrs > 0) ? duration_hrs : 120;

      {int days = duration_adj/24;
      int hours = duration_adj-(days*24);
      printf("%s[BOARD_CREATE %s] %sDURATION (d:h:m): %s%i:%i:%i%s\n",
      ANSI_RED, name, ANSI_MGNTA, ANSI_BLU, days,
      hours, 0, ANSI_NON);}

      #ifndef ASH_DEBUG
      pid_t pid = fork();
      if(pid > 0){
            printf("mb spawned at pid: %i\n", pid);
            return 2;
      }
      #endif
      int sock = listen_sock();
      if(sock == -1)return -1;

      struct sockaddr_un addr;
      memset(&addr, 0, sizeof(struct sockaddr_un));
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, name, sizeof(addr.sun_path)-1);

      if(bind(sock, (struct sockaddr*)&addr, SUN_LEN(&addr)) == -1
      || listen(sock, 0) == -1)return -1;

      chmod(addr.sun_path, 0777);

      init_host();
      /* TODO: find way to safely detach/stop thread */
      pthread_t add_host_pth_pth;
      pthread_create(&add_host_pth_pth, NULL, &add_host_pth, &sock);
      pthread_detach(add_host_pth_pth);

      /* SIGUSR1 is used to interrupt calls to sleep
       * to update duration_adj 
       */
      signal(SIGUSR1, spin);

      #ifndef ASH_DEBUG
      /* if we're in debug mode, ctrl-c should kill host */
      signal(SIGINT, spin);
      #endif

      /* mb_handler will send a SIGUSR2 to getpid() upon /d */
      signal(SIGUSR2, host_cleanup);

      /* convert duration_adj to seconds for sleep() */
      duration_adj *= 3600;
      
      /* TODO: possibly just keep a global char* sockname and remove 
       * it from host_cleanup() instead of sending SIGUSR2
       * don't love the SIGUSR2 solution
       */

      /* ruc.sp is set to NULL by host_cleanup() */
      /* sleep() returns remaining sleep time if interrupted */
      while(ruc.sp && (duration_adj = sleep(duration_adj)))
            alert_duration(peers, n_peers, duration_adj);

      remove(name);

      return 1;
}
