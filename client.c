#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <signal.h>
#include <pwd.h>

#include "client.h"
#include "shared.h"

struct room_lst* cur_room;

struct rm_hash_lst init_rm_hash_lst(int buckets){
      struct rm_hash_lst rml;
      rml.me = getuid();
      rml.n = 0;
      rml.bux = buckets;

      rml.rooms = calloc(rml.bux, sizeof(struct room_lst*));
      rml.in_use = malloc(sizeof(int)*(rml.bux+1));
      memset(rml.in_use, -1, sizeof(int)*(rml.bux+1));
      return rml;
}

void free_rm_hash_lst(struct rm_hash_lst rml){
      struct room_lst* prev;
      for(int i = 0; rml.in_use[i] != -1; ++i){
            for(struct room_lst* cur = (prev = rml.rooms[rml.in_use[i]])->next; prev;
                cur = (cur) ? cur->next : cur){
                  pthread_mutex_lock(&prev->room_msg_queue_lck);
                  free(prev->msg_queue_base);
                  free(prev);
                  /* TODO: is it UB to destroy a locked mut_lock? */
                  pthread_mutex_destroy(&prev->room_msg_queue_lck);
                  prev = cur;
            }
      }
      free(rml.rooms);
      free(rml.in_use);
}

/* ~~~~~~~~~ room operations begin ~~~~~~~~~~~ */

/* looks up a thread by its label or ref_no 
 * if both are provided, label is hashed
 * for lookup and ref_nos are confirmed
 */
struct room_lst* room_lookup(struct rm_hash_lst rml, char* rm_name, int ref_no){
      /* if we don't find anything by hashing or iteratively, we should return
       * the last index of the relevant index for insertion
       */
      struct room_lst* relevant;

      if(rm_name){
            int ind = *rm_name % rml.bux;
            /* TODO: because indices are found using first letter hashed, room cannot
             * be cheaply found when rm_name a substring of room->label but doesn't include 
             * the first letter
             * TODO: switch to ref_no based hashes
             * THIS IS ANOTHER REASON TO SWITCH TO REF_NO HASHES
             */
            if(rml.rooms[ind]){
            /*if(!rml.rooms[ind])return NULL;*/
                  for(struct room_lst* cur = rml.rooms[ind]; cur; cur = cur->next)
                        if(strstr(cur->label, rm_name)){
                              if(cur->ref_no == ref_no || ref_no == -1)return cur;
                              relevant = cur;
                        }
            }
      }

      /* if we've fallen through && rm_name, there's still hope - it won't come cheap though  */

      for(int i = 0; rml.in_use[i] != -1; ++i)
            for(struct room_lst* cur = rml.rooms[rml.in_use[i]]; cur; cur = cur->next)
                  if((cur->ref_no == ref_no) || (rm_name && strstr(cur->label, rm_name)))return cur;

      return NULL;
      return relevant;
      /* TODO: relevant should be an optional param */
}


/* return existence of ref_no */
/* param rl is optional - in case rl is already malloc'd - this happens when a name is being updated */
struct room_lst* add_room_rml(struct rm_hash_lst* rml, int ref_no, char* name, uid_t creator, struct room_lst* rl){
      /* int ind = ref_no % rml->bux; */
      /* TODO: should more chars be summed for hashing */
      // TODO: ref_no should be used for hashing because the most
      // expensive lookup is done using ref_no from read_notif_pth
      int ind = *name % rml->bux;
      struct room_lst* cur;
      /* first room in bucket */
      if(!rml->rooms[ind]){
            cur = rml->rooms[ind] = (rl) ? rl : malloc(sizeof(struct room_lst));
            rml->in_use[rml->n++] = ind;
      }
      else{
            /* if rml->rooms[ind], we can assume a ptr to the last room is stored in bookend_rm */
            cur = rml->rooms[ind]->bookend_rm->next = (rl) ? rl : malloc(sizeof(struct room_lst));
            /* only first and last bookend_rm values should be set */
            rml->rooms[ind]->bookend_rm->bookend_rm = NULL;
            /* bookend_rm is set to first entry in last entry of an an ind 
             * this allows for simple circular /n behavior
             */
            cur->bookend_rm = rml->rooms[ind];
      }
      if(!rl){
            cur->creator = creator;
            cur->ref_no = ref_no;
            strncpy(cur->label, name, sizeof(cur->label)-1);

            /* msg queue! */
            /* TODO: this should be done in a separate init_msg_queue func */
            cur->n_msg = 0;
            cur->msg_queue_cap = 50;
            /* TODO: free */
            cur->msg_queue = malloc(sizeof(struct msg_queue_entry)*cur->msg_queue_cap);
            cur->msg_queue_base = cur->msg_queue;
            pthread_mutex_init(&cur->room_msg_queue_lck, NULL);
      }

      rml->rooms[ind]->bookend_rm = cur;

      cur->next = NULL;

      return cur;
}

/* returns the room_lst* that has been inserted to rml, or NULL on failure */
/* TODO: this needs a new name to reflect its insertion behavior */
struct room_lst* rename_room_rml(struct rm_hash_lst* rml, int ref_no, char* new_name, uid_t creator){
      struct room_lst* rl = room_lookup(*rml, NULL, ref_no);
      if(!rl)return add_room_rml(rml, ref_no, new_name, creator, NULL);
      /* if rl is only entry in its bucket */
      int bucket = *rl->label % rml->bux;
      if(rml->rooms[bucket] == rl){
            // in_use must be adjusted
            for(int i = 0; rml->in_use[i] != -1; ++i){
                  if(rml->in_use[i] == bucket){
                        memmove(rml->in_use+i, rml->in_use+i+1, sizeof(int)*(--rml->n)-i);
                        break;
                  }
            }
      }
      /* finding room_lst* before rl */
      struct room_lst* rl_prev = rml->rooms[bucket];
      if(rl_prev != rl){
            for(; rl_prev->next != rl; rl_prev = rl_prev->next);
            /* is this redundant? */
            if(!rl_prev || !rl_prev->next)return NULL;
            rl_prev->next = NULL;
      }
      else rml->rooms[bucket] = NULL;
      /* rl must be moved from its current bucket */
      strncpy(rl->label, new_name, sizeof(rl->label)-1);

      add_room_rml(rml, ref_no, new_name, rl->creator, rl);
      return rl;
}

/* ~~~~~~~~~ room operations end ~~~~~~~~~~~ */

/* ~~~~~~~~~ msg_queue operations begin ~~~~~~~~~~~ */

_Bool insert_msg_msg_queue(struct room_lst* rm, char* msg, uid_t sender){
      _Bool resz = 0;
      pthread_mutex_lock(&rm->room_msg_queue_lck);
      if(rm->n_msg == rm->msg_queue_cap){
            resz = 1;
            rm->msg_queue_cap *= 2;
            struct msg_queue_entry* tmp = malloc(sizeof(struct msg_queue_entry)*rm->msg_queue_cap);
            memcpy(tmp, rm->msg_queue, sizeof(struct msg_queue_entry)*rm->n_msg);
            free(rm->msg_queue_base);
            rm->msg_queue_base = rm->msg_queue = tmp;
      }

      struct msg_queue_entry tmp_entry;
      memset(tmp_entry.msg, 0, 201);
      tmp_entry.sender = sender;
      /* memcpy'ing because we want all 200 bytes
       * this lets the user avoid clearing msg
       */
      memcpy(tmp_entry.msg, msg, 200);

      rm->msg_queue[rm->n_msg++] = tmp_entry;
      pthread_mutex_unlock(&rm->room_msg_queue_lck);
      return resz;
}

_Bool pop_msg_queue(struct room_lst* rm, char* msg, uid_t* sender){
      _Bool ret = 0;
      pthread_mutex_lock(&rm->room_msg_queue_lck);
      if(rm->n_msg){
            *sender = rm->msg_queue->sender;
            /* if our char* is truncated by 1 byte, so be it */
            strncpy(msg, rm->msg_queue->msg, 199);
            ++rm->msg_queue; ++rm->msg_queue_cap; --rm->n_msg;
            ret = 1;
      }
      pthread_mutex_unlock(&rm->room_msg_queue_lck);
      return ret;
}

/* ~~~~~~~~~ msg_queue operations end ~~~~~~~~~~~ */

/* ~~~~~~~~~ communication begin ~~~~~~~~~~~ */

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
int create_room(char* rm_name, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_CREATE_THREAD;
      mb_a.mb_inf[1] = -1;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, rm_name, 200);
      return send_mb_r(mb_a, sock);
}

int reply_room(int rm_ref_no, char* msg, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_REPLY_THREAD;
      mb_a.mb_inf[1] = rm_ref_no;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, msg, 200);
      return send_mb_r(mb_a, sock);
}

int req_rname_update(int rm_ref_no, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_RNAME_UP_REQ;
      mb_a.mb_inf[1] = rm_ref_no;
      memset(mb_a.str_arg, 0, 201);
      return send_mb_r(mb_a, sock);
}

int snd_rname_update(int rm_ref_no, char* rm_name, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_RNAME_UP_INF;
      mb_a.mb_inf[1] = rm_ref_no;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, rm_name, 200);
      return send_mb_r(mb_a, sock);
}

int req_n_mem(int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_N_MEM_REQ;
      mb_a.mb_inf[1] = -1;
      memset(mb_a.str_arg, 0, 201);
      return send_mb_r(mb_a, sock);
}

int rm_board(int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_REMOVE_BOARD;
      mb_a.mb_inf[1] = -1;
      memset(mb_a.str_arg, 0, 201);
      return send_mb_r(mb_a, sock);
}

/* ~~~~~~~~~ communication end ~~~~~~~~~~~ */

/* get uname returns the username of a user given their uid */
char* get_uname(uid_t uid, struct ash_table* table){
      char* ret;
      if((ret = lookup_str_ash_table(uid, table)))return ret;
      struct passwd* pw = getpwuid(uid);
      if(!pw)return "{UNKNOWN}";
      insert_ash_table(uid, ret = pw->pw_name, NULL, table);
      return ret;
}

/* run is accessed both from read_notif_pth and client() */
/* once run is set to 0, client will safely exit */
/* run is volatile because its value can be changed by SIGINTs */
volatile _Bool run = 1;

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
      struct room_lst* cur_r = NULL;
      rnp_arg->n_mem_req = 0;
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

            switch(msg_type){
                  case MSGTYPE_NOTIF:
                        add_room_rml(rnp_arg->rml, ref_no, buf, uid, NULL);
                        printf("%s%s%s: %s[ROOM_CREATE %s]%s\n",
                        ANSI_GRE, get_uname(uid, rnp_arg->uname_table),
                        ANSI_NON, ANSI_RED, buf, ANSI_NON);
                        break;
                  case MSGTYPE_MSG:
                        // TODO: room lookup is too slow without label
                        // TODO: should ref_no be used to hash?
                        // this is the most frequent lookup
                        if((cur_r = room_lookup(*rnp_arg->rml, NULL, ref_no)))
                        /* this should never be reached with updated system */
                        /*
                         *else{
                         *      cur_r = add_room_rml(rnp_arg->rml, ref_no, "{UNKNOWN_LABEL}", uid, NULL);
                         *      req_rname_update(ref_no, rnp_arg->sock);
                         *}
                         */
                        /* adding message to msg stack */
                        /* if the above code is being used, no need to check cur_r */
                        // if(cur_r)insert_msg_msg_queue(cur_r, buf, uid);
                        insert_msg_msg_queue(cur_r, buf, uid);
                        break;

                  /* send out room name if it's requested */

                  case MSG_RNAME_UP_REQ:
                        cur_r = room_lookup(*rnp_arg->rml, NULL, ref_no);
                        if(!cur_r)break;
                        snd_rname_update(ref_no, cur_r->label, rnp_arg->sock);
                        break;
                  case MSG_RNAME_UP_INF:
                        /* TODO: confirm that no else condition is required
                         * -- make sure that this should always evaluate to 1
                         */
                        /* this form should be depracated */
                        if((cur_r = rename_room_rml(rnp_arg->rml, ref_no, buf, -1)))
                              /* TODO: should a distinction be made between ROOM_CREATE and
                               * ROOM_SHARE, for ex. printf("%s%i%s: %s[ROOM_SHARE]%s\n",);
                               */
                              printf("%s%s%s: %s[*ROOM_CREATE* %s]%s\n",
                              ANSI_GRE, get_uname(cur_r->creator, rnp_arg->uname_table),
                              ANSI_NON, ANSI_RED, buf, ANSI_NON);
                        /*else add new rm*/
                        break;
                  case MSG_N_MEM_INF:
                        /* TODO: should rml have a member for n_mems? */
                        /* TODO: should /l print number of members */
                        if(!rnp_arg->n_mem_req)break;
                        rnp_arg->n_mem_req = 0;
                        printf("%s%i%s member%s connected to %s**%s%s%s**%s\n",
                        /* n_members are sent in the ref_no buf */
                        ANSI_RED, ref_no, ANSI_MGNTA, (ref_no > 1) ? "s are" : " is",
                        ANSI_RED, ANSI_MGNTA, rnp_arg->rml->board_path, ANSI_RED, ANSI_NON);
                        break;
            }
      }
      /*printf("%slost connection to board%s\n", ANSI_RED, ANSI_NON);*/
      printf("%sboard has been removed%s\n", ANSI_RED, ANSI_NON);
      run = 0;
      return NULL;
}

void p_help(){
      printf(
            "[message]:\n"
            "  sends message to current room\n"
            "/[h]elp:\n"
            "  prints this information\n"
            "/[j]oin [room_name]:\n"
            "/[r]oom [room_name]:\n" 
            "  join room with room_name\n"
            "/[n]ext:\n"
            "  switch to next room with same first char as current\n"
            "/[c]reate [room_name]:\n"
            "  creates a room with name room_name\n"
            "/[l]ist:\n"
            "  lists all rooms, current room will be %sblue%s\n"
            "/[w]hich:\n"
            "  prints current room name and reference number\n"
            "/[u]ser:\n"
            "/[#]:\n"
            "  prints number of users in current board\n"
            "/[d]elete:\n"
            "  sends a deletion request for current board\n"
            "  this request will be fulfilled IFF you are\n"
            "  board's creator\n"
            "/[e]xit:\n"
            "/e[x]it:\n"
            "  exits current room\n"
            "ctrl-c:\n"
            "  exits this program\n"
      , ANSI_BLU, ANSI_NON);
}

void* repl_pth(void* rnp_arg_v){
      struct read_notif_pth_arg* rnp_arg = (struct read_notif_pth_arg*)rnp_arg_v;
      struct room_lst* tmp_rm;

      char* inp = NULL, * tmp_p;
      size_t sz = 0;
      int b_read, tmp_ret;

      while((b_read = getline(&inp, &sz, stdin)) != EOF){
            inp[--b_read] = 0;

            if(*inp == '/' && b_read > 1){
                  switch(inp[1]){
                        #ifdef ASH_DEBUG
                        case 'p':
                              for(int i = 0; i < 10; ++i){
                                    printf("in_use[%i] == %i\n", i, rnp_arg->rml->in_use[i]);
                              }
                              break;
                        #endif
                        /* both /join and /room will join an existing room */
                        case 'j':
                        case 'r':
                              /* TODO: rooms should be joinable by ref_no */
                              if(!(tmp_p = strchr(inp, ' ')))break;
                              tmp_rm = room_lookup(*rnp_arg->rml, tmp_p+1, -1);
                              if(!tmp_rm){
                                    printf("%sno room containing \"%s\" was found%s\n", ANSI_RED, tmp_p+1, ANSI_NON);
                                    break;
                              }
                              cur_room = tmp_rm;
                              printf("%scurrent room has been switched to \"%s\"%s\n", ANSI_MGNTA, cur_room->label, ANSI_NON);
                              break;
                        /* go to room with ref_no */
                        case 'g':
                              if(!(tmp_p = strchr(inp, ' ')) || !strtoi(tmp_p+1, &tmp_ret))break;
                              tmp_rm = room_lookup(*rnp_arg->rml, NULL, tmp_ret);
                              if(!tmp_rm)break;
                              cur_room = tmp_rm;
                              printf("%scurrent room has been switched to \"%s\"%s\n", ANSI_MGNTA, cur_room->label, ANSI_NON);
                              break;
                        /* go to next room with same first character in label */
                        case 'n':
                              if(!cur_room)break;
                              /* to allow for circular cycling */
                              cur_room = (cur_room->next) ? cur_room->next : cur_room->bookend_rm;
                              printf("%scurrent room has been switched to \"%s\"%s\n", ANSI_MGNTA, cur_room->label, ANSI_NON);
                              break;
                        case 'c':
                              if(!(tmp_p = strchr(inp, ' ')))break;
                              if((tmp_ret = create_room(tmp_p+1, rnp_arg->sock))){/* TODO */}
                              #ifdef ASH_DEBUG
                              printf("ret val of create room: %i\n", tmp_ret);
                              printf("sent to socket: %i\n", rnp_arg->sock);
                              #endif
                              break;
                        case 'l':
                              for(int i = 0; rnp_arg->rml->in_use[i] != -1; ++i){
                                    for(struct room_lst* rl = rnp_arg->rml->rooms[rnp_arg->rml->in_use[i]]; rl; rl = rl->next)
                                          printf("%s%s%s: \"%s%s%s\": %s%i%s\n",
                                          (rl->creator == rnp_arg->rml->me) ? ANSI_BLU : ANSI_NON, 
                                          get_uname(rl->creator, rnp_arg->uname_table), ANSI_NON,
                                          (rl == cur_room) ? ANSI_BLU : ANSI_NON,
                                          rl->label, ANSI_NON, (rl == cur_room) ? ANSI_BLU : ANSI_NON, rl->ref_no, ANSI_NON);
                              }
                              break;
                        case 'w':
                              if(!cur_room)
                                    printf("%syou have not yet joined a room%s\n", ANSI_MGNTA, ANSI_NON);
                              else 
                                    printf("%scurrent room is \"%s\"%s\n", ANSI_MGNTA, cur_room->label, ANSI_NON);
                              break;
                        /* number of users */
                        case 'u':
                        case '#':
                              rnp_arg->n_mem_req = 1;
                              req_n_mem(rnp_arg->sock);
                              break;
                        /* exit or e[x]it */
                        case 'e':
                        case 'x':
                              if(!cur_room)break;
                              printf("%syou have left \"%s\"%s\n", ANSI_MGNTA, cur_room->label, ANSI_NON);
                              cur_room = NULL;
                              break;
                        /* sends a deletion request for current board */
                        case 'd':
                              printf("%sdeletion request has been sent -- %sauthenticating%s\n",
                              ANSI_MGNTA, ANSI_RED, ANSI_NON);
                              rm_board(rnp_arg->sock);
                              break;
                        case 'h':
                              p_help();
                              break;
                  }
            }
            /* we're sending a regular message */
            else if(!cur_room)
                  printf("%syou must first enter a room before replying%s\n", ANSI_RED, ANSI_NON);
            else
                  reply_room(cur_room->ref_no, inp, rnp_arg->sock);
      }
      return NULL;
}

void ex(int x){(void)x; run = 0;}

_Bool client(char* sock_path){
      struct ash_table ut;
      /* TODO: free this struct */
      ash_table_init(&ut, 100);

      cur_room = NULL;
      int sock = listen_sock();

      listen(sock, 0);
      struct sockaddr_un r_addr;
      memset(&r_addr, 0, sizeof(struct sockaddr_un));
      r_addr.sun_family = AF_UNIX;
      strncpy(r_addr.sun_path, sock_path, sizeof(r_addr.sun_path)-1);

      if(connect(sock, (struct sockaddr*)&r_addr, sizeof(struct sockaddr_un)) == -1)
            /* failed to connect to host */
            /* printf("failed to connect to host \"%s\"\n", sock_path); */
            return 0;

      printf("%shello, %s%s%s%s! welcome to **%s%s%s**\n%senter \"/h\" for help at any time\n",
      ANSI_RED, ANSI_BLU, get_uname(getuid(), &ut), ANSI_NON, ANSI_RED, ANSI_MGNTA, sock_path, ANSI_RED, ANSI_NON);

      struct rm_hash_lst rml = init_rm_hash_lst(100);

      struct read_notif_pth_arg rnpa;
      rnpa.uname_table = &ut;
      rnpa.sock = sock;
      rnpa.rml = &rml;
      strncpy(rnpa.rml->board_path, sock_path, PATH_MAX);

      pthread_t read_notif_pth_pth, repl_pth_pth;
      /* TODO: fix possible synch issues from sharing rnpa.rml */
      /* there should be an rml mutex lock - activated each time a 
       * room is added, in_use is edited, or room_lookup is called
       */
      pthread_create(&read_notif_pth_pth, NULL, &read_notif_pth, &rnpa);
      pthread_create(&repl_pth_pth, NULL, &repl_pth, &rnpa);

      pthread_detach(read_notif_pth_pth);
      pthread_detach(repl_pth_pth);

      /* doesn't need to be memset(*, 0, **)'d - this is handled by insert_msg_msg_queue */
      char tmp_p[201];
      uid_t s_uid;

      signal(SIGINT, ex);

      while(run){
            if(cur_room && pop_msg_queue(cur_room, tmp_p, &s_uid))
                  printf("%s%s%s: %s\n", ANSI_GRE, get_uname(s_uid, &ut), ANSI_NON, tmp_p);
            usleep(10000);
      }
      free_rm_hash_lst(rml);
      free_ash_table(&ut);
      return 1;
}
