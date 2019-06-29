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
#include "ashio.h"

struct room_lst* cur_room;

struct rm_hash_lst init_rm_hash_lst(int buckets){
      struct rm_hash_lst rml;
      rml.me = getuid();
      rml.n = 0;
      rml.bux = buckets;

      rml.rooms = calloc(rml.bux, sizeof(struct room_lst*));
      rml.in_use = malloc(sizeof(int)*(rml.bux+1));
      memset(rml.in_use, -1, sizeof(int)*(rml.bux+1));

      rml.ref_no_lookup = malloc(sizeof(struct ash_table));
      ash_table_init(rml.ref_no_lookup, 100);

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
      free_ash_table(rml.ref_no_lookup, 0);
      free(rml.ref_no_lookup);
}

/* ~~~~~~~~~ msg_queue operations begin ~~~~~~~~~~~ */

void init_msg_queue(struct room_lst* rm, int q_cap){
      rm->n_msg = 0;
      rm->msg_queue_base_sz = rm->msg_queue_cap = q_cap;
      rm->msg_queue = malloc(sizeof(struct msg_queue_entry)*rm->msg_queue_cap);
      rm->msg_queue_base = rm->msg_queue;
}

_Bool insert_msg_msg_queue(struct room_lst* rm, char* msg, uid_t sender){
      _Bool resz = 0;

      pthread_mutex_lock(&rm->room_msg_queue_lck);

      if(rm->n_msg == rm->msg_queue_cap){
            resz = 1;
            rm->msg_queue_cap = (rm->msg_queue_cap) ? rm->msg_queue_cap*2 : rm->msg_queue_base_sz;
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
            /* using memccpy avoids warnings on linux 
             * also, it's a cool POSIX built in that i didn't know about
             */
            memccpy(msg, rm->msg_queue->msg, 0, 199);

            /* reasoning behind the line below
             * cap 5, n_msg 3  -> cap 4, n_msg 2
             * [0, 1, 2, _, _] -> [1, 2, _, _]
             */
            ++rm->msg_queue; --rm->msg_queue_cap; --rm->n_msg;
            ret = 1;
      }

      pthread_mutex_unlock(&rm->room_msg_queue_lck);

      return ret;
}

/* ~~~~~~~~~ msg_queue operations end ~~~~~~~~~~~ */

/* ~~~~~~~~~ room operations begin ~~~~~~~~~~~ */

/* looks up a thread by its label or ref_no 
 * if both are provided, label is hashed
 * for lookup and ref_nos are confirmed
 */
struct room_lst* room_lookup(struct rm_hash_lst rml, char* rm_name, int ref_no){
      /* TODO: i can just use another ash_table to store room_lst's hashed by *string
       * the pointers can be to the same room_lst's 
       */
      /* if this lookup fails, all hope is lost
       * this is a helpful assumption to make because it allows us to ignore ref_no for
       * the rest of this function
       */
      if(ref_no != -1)return(struct room_lst*)lookup_data_ash_table(ref_no, rml.ref_no_lookup);

      if(rm_name){
            int ind = *rm_name % rml.bux;
            /* because indices are found using first letter hashed, room cannot
             * be cheaply found when rm_name a substring of room->label but doesn't include 
             * the first letter
             * this will only occur, though, when ref_no is not provided
             */
            if(rml.rooms[ind]){
                  for(struct room_lst* cur = rml.rooms[ind]; cur; cur = cur->next)
                        /* we'll only get here if ref_no == -1 */
                        if(strstr(cur->label, rm_name))
                              return cur;
            }
      }
      /* if no ref_no and no rm_name */
      /* we can assume rm_name for the rest of this function
       */
      else return NULL;

      /* if we've fallen through && rm_name, there's still hope - it won't come cheap though  */

      for(int i = 0; rml.in_use[i] != -1; ++i)
            for(struct room_lst* cur = rml.rooms[rml.in_use[i]]; cur; cur = cur->next)
                  if(strstr(cur->label, rm_name))return cur;

      return NULL;
}


/* return existence of ref_no */
/* param rl is optional - in case rl is already malloc'd - this happens when a name is being updated */
struct room_lst* add_room_rml(struct rm_hash_lst* rml, int ref_no, char* name, uid_t creator, struct room_lst* rl){
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

            init_msg_queue(cur, 10);

            pthread_mutex_init(&cur->room_msg_queue_lck, NULL);
      }

      rml->rooms[ind]->bookend_rm = cur;

      cur->next = NULL;

      /* TODO: define a single struct that is hashable strings and ints simultaneously */
      insert_ash_table(ref_no, NULL, cur, rml->ref_no_lookup);

      return cur;
}

/* ~~~~~~~~~ room operations end ~~~~~~~~~~~ */

/* ~~~~~~~~~ communication begin ~~~~~~~~~~~ */

int send_mb_r(struct mb_msg mb_a, int sock){
      int ret = 1;
      #ifdef ASH_DEBUG
      printf("sending: %i %i %i %s\n", mb_a.mb_inf[0], mb_a.mb_inf[1], mb_a.mb_inf[2], mb_a.str_arg);
      #endif
      /* TODO: send creator as a separate uid_t rather than using an int */
      ret &= send(sock, mb_a.mb_inf, sizeof(int)*3, 0) != -1;
      ret &= send(sock, mb_a.str_arg, 200, 0) != -1;
      return ret;
}

/* returns thread ref no */
int create_room(char* rm_name, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_CREATE_ROOM;
      mb_a.mb_inf[1] = -1;
      mb_a.mb_inf[2] = -1;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, rm_name, 200);
      return send_mb_r(mb_a, sock);
}

int reply_room(int rm_ref_no, char* msg, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_REPLY_ROOM;
      mb_a.mb_inf[1] = rm_ref_no;
      mb_a.mb_inf[2] = -1;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, msg, 200);
      return send_mb_r(mb_a, sock);
}

int snd_rname_update(int rm_ref_no, char* rm_name, uid_t rm_creator, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_RNAME_UP_INF;
      mb_a.mb_inf[1] = rm_ref_no;
      /* TODO: there should be a separate uid_t entry in mb_msg
       * for creator - this shouldn't be stored in an int
       */
      mb_a.mb_inf[2] = rm_creator;

      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, rm_name, 200);

      return send_mb_r(mb_a, sock);
}

int req_n_mem(int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_N_MEM_REQ;
      mb_a.mb_inf[1] = -1;
      mb_a.mb_inf[2] = -1;
      memset(mb_a.str_arg, 0, 201);
      return send_mb_r(mb_a, sock);
}

int req_board_duration(int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_DUR_REQ;
      mb_a.mb_inf[1] = -1;
      mb_a.mb_inf[2] = -1;
      memset(mb_a.str_arg, 0, 201);
      return send_mb_r(mb_a, sock);
}

int rm_board(int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_REMOVE_BOARD;
      mb_a.mb_inf[1] = -1;
      mb_a.mb_inf[2] = -1;
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

/* ~~~~~~~~~ duration begin ~~~~~~~~~~~ */
int get_dur_secs(struct client_pth_arg* cpa){
      return cpa->dur-(time(NULL)-cpa->dur_recvd);
}

/* THIS SHOULD ONLY BE CALLED WITHIN A pthread_mutex_lock ON cpa->cpa_lock */
void print_dur(struct client_pth_arg* cpa){
      int dur = get_dur_secs(cpa);
      printf("%s%i:%.2i%s (m:s) until %s**%s%s%s** is removed%s\r\n", ANSI_RED,
      dur/60, dur%60, ANSI_MGNTA, ANSI_RED, ANSI_MGNTA, cpa->rml->board_path,
      ANSI_RED, ANSI_NON);
}

/* ~~~~~~~~~ duration end ~~~~~~~~~~~ */

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
void* read_notif_pth(void* cp_arg_v){
      struct client_pth_arg* cp_arg = (struct client_pth_arg*)cp_arg_v;
      int ref_no, msg_type; uid_t uid;
      char buf[201];
      struct room_lst* cur_r = NULL;

      while(1){
            memset(buf, 0, 201);
            /* reading uid_t of sender */
            if(read(cp_arg->sock, &uid, sizeof(uid_t)) <= 0)break;
            /* reading MSGTYPE */
            if(read(cp_arg->sock, &msg_type, sizeof(int)) <= 0)break;
            #ifdef ASH_DEBUG
            printf("read msg type %i\n", msg_type);
            #endif
            /* reading ref no */
            if(read(cp_arg->sock, &ref_no, sizeof(int)) <= 0)break;
            #ifdef ASH_DEBUG
            printf("read ref_no: %i\n", ref_no);
            #endif
            if(read(cp_arg->sock, buf, 200) <= 0)break;
            #ifdef ASH_DEBUG
            printf("string: \"%s\" read from buf\n", buf);
            #endif

            /* this should be toggled by /s */
            /* TODO: implement this */
            /* _Bool silent = 0; */
            switch(msg_type){
                  case MSG_CREATE_ROOM:

                        pthread_mutex_lock(&cp_arg->cpa_lock);

                        /* TODO: allow silent mode, where room creation notifs aren't printed
                         * /silent {on, off} can set this
                         *
                         * TODO: prevent spamming from host side, not passing on MSG_CREATE_ROOM from
                         * the same socket over x times per minute
                         */

                        add_room_rml(cp_arg->rml, ref_no, buf, uid, NULL);

                        /* TODO: implement this */
                        /* if(!silent) */
                        printf("%s%s%s: %s[ROOM_CREATE %s]%s\r\n",
                        ANSI_GRE, get_uname(uid, cp_arg->uname_table),
                        ANSI_NON, ANSI_RED, buf, ANSI_NON);

                        pthread_mutex_unlock(&cp_arg->cpa_lock);

                        break;
                  case MSG_REPLY_ROOM:
                        /* TODO: confirm that there is no need to lock here
                         * repl_pth() doesn't do any writes to rml
                         * locking here could also complicate call to insert_msg_msg_queue
                         */
                        if((cur_r = room_lookup(*cp_arg->rml, NULL, ref_no)))
                        /* adding message to msg stack */
                        /* if the above code is being used, no need to check cur_r */
                        // if(cur_r)insert_msg_msg_queue(cur_r, buf, uid);
                        insert_msg_msg_queue(cur_r, buf, uid);
                        break;

                  /* send out room name and creator if requested */

                  case MSG_RNAME_UP_REQ:
                        cur_r = room_lookup(*cp_arg->rml, NULL, ref_no);
                        if(!cur_r)break;
                        snd_rname_update(ref_no, cur_r->label, cur_r->creator, cp_arg->sock);
                        break;
                  case MSG_RNAME_UP_INF:
                        /* if room is found, we don't know what to do 
                         * this should never happen
                         * TODO: look into removing this check
                         */
                        /* TODO: lock after room_lookup and print statement */

                        pthread_mutex_lock(&cp_arg->cpa_lock);

                        if(!room_lookup(*cp_arg->rml, NULL, ref_no) &&
                          (cur_r = add_room_rml(cp_arg->rml, ref_no, buf, uid, NULL)))/* &&
                          TODO: implement this
                          !silent) */
                              printf("%s%s%s: %s[*ROOM_CREATE* %s]%s\r\n",
                              ANSI_GRE, get_uname(cur_r->creator, cp_arg->uname_table),
                              ANSI_NON, ANSI_RED, buf, ANSI_NON);

                        pthread_mutex_unlock(&cp_arg->cpa_lock);

                        break;
                  case MSG_N_MEM_INF:
                        /* TODO: should rml have a member for n_mems? */
                        /* TODO: should /l print number of members */

                        pthread_mutex_lock(&cp_arg->cpa_lock);

                        if(!cp_arg->n_mem_req){
                              pthread_mutex_unlock(&cp_arg->cpa_lock);
                              break;
                        }
                        cp_arg->n_mem_req = 0;

                        pthread_mutex_unlock(&cp_arg->cpa_lock);

                        printf("%s%i%s member%s connected to %s**%s%s%s**%s\r\n",
                        /* n_members are sent in the ref_no buf */
                        ANSI_RED, ref_no, ANSI_MGNTA, (ref_no > 1) ? "s are" : " is",
                        ANSI_RED, ANSI_MGNTA, cp_arg->rml->board_path, ANSI_RED, ANSI_NON);
                        break;
                  case MSG_DUR_ALERT:
                        /* even if we're not waiting for an alert, we can store the dur */

                        pthread_mutex_lock(&cp_arg->cpa_lock);

                        cp_arg->dur = ref_no;
                        cp_arg->dur_recvd = time(NULL);
                        if(!cp_arg->dur_req){
                              pthread_mutex_unlock(&cp_arg->cpa_lock);
                              break;
                        }
                        print_dur(cp_arg);
                        cp_arg->dur_req = 0;

                        pthread_mutex_unlock(&cp_arg->cpa_lock);

                        break;
                  case MSG_NO_CRE_DUR:
                        {
                        /* TODO: we shouldn't be sent a new message each time we try to create a room */
                        int wait_secs = ref_no-time(NULL);
                        printf("%s%i:%.2i%s (m:s) until%s you can create more rooms%s\r\n", ANSI_RED,
                        wait_secs/60, wait_secs%60, ANSI_MGNTA, ANSI_RED, ANSI_NON);
                        }
                        break;
            }
      }
      /* since getline_raw is likely waiting and only resets terminal to cooked mode on exit
       * it's smart to reset terminal here
       */
      reset_term();
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
            "/[t]ime:\n"
            "  prints the number of minutes until deletion of board\n"
            "/[j]oin [room_name]:\n"
            "/[r]oom [room_name]:\n" 
            "  join room with room_name\n"
            "/[g]oto [ref_no]:\n" 
            "  join room with reference number ref_no\n"
            "/[n]ext:\n"
            "  switch to next room with same first char as current\n"
            "/[c]reate [room_name]:\n"
            "  creates a room with name room_name\n  %s%i%s rooms can be created per user per minute\n"
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
      , ANSI_RED, CRE_PER_MIN, ANSI_NON, ANSI_BLU, ANSI_NON);
}

void p_rm_switch(struct room_lst* rm){
      printf("%scurrent room has been switched to \"%s%s%s\" (%s%i%s)%s\n", 
      ANSI_MGNTA, ANSI_RED, rm->label, ANSI_MGNTA, ANSI_RED, rm->ref_no, ANSI_MGNTA, ANSI_NON);
}

void p_cmd_err(char* cmd){
      printf("%sERR%s: unable to execute command \"%s%s%s\". it's likely that you're missing an argument%s\n",
      ANSI_RED, ANSI_MGNTA, ANSI_RED, cmd, ANSI_MGNTA, ANSI_NON);
}

void* repl_pth(void* cp_arg_v){
      struct client_pth_arg* cp_arg = (struct client_pth_arg*)cp_arg_v;
      struct room_lst* tmp_rm;

      char* inp = NULL, * tmp_p;
      int b_read, tmp_ret;
      _Bool good_msg, free_s;

      while((inp = tab_complete(
                   (cur_room) ? cur_room->msg_queue_base : NULL,
                   sizeof(struct msg_queue_entry),
                   /* offset into struct msg_queue_entry where msg can be found - should be zero */
                   (cur_room) ? (char*)cur_room->msg_queue->msg - (char*)cur_room->msg_queue : 0,
                   /* number of cached messages */
                   (cur_room) ? (cur_room->msg_queue-cur_room->msg_queue_base)+1 : 0,
                   /* char to iterate options */
                   14,
                   &b_read,
                   &free_s
                   ))){

            good_msg = 1;
            putchar('\r');

            if(*inp == '/' && b_read > 1){
                  _Bool bad_cmd = good_msg = 0;
                  switch(inp[1]){
                        #ifdef ASH_DEBUG
                        case 'p':
                              for(int i = 0; i < 10; ++i)
                                    printf("in_use[%i] == %i\n", i, cp_arg->rml->in_use[i]);
                              break;
                        #endif
                        /* both /join and /room will join an existing room */
                        case 'j':
                        case 'r':
                              if(!(tmp_p = strchr(inp, ' '))){
                                    bad_cmd = 1;
                                    break;
                              }

                              pthread_mutex_lock(&cp_arg->cpa_lock);

                              tmp_rm = room_lookup(*cp_arg->rml, tmp_p+1, -1);

                              pthread_mutex_unlock(&cp_arg->cpa_lock);

                              if(!tmp_rm){
                                    printf("%sno room containing \"%s\" was found%s\n", ANSI_RED, tmp_p+1, ANSI_NON);
                                    break;
                              }
                              cur_room = tmp_rm;
                              p_rm_switch(cur_room);
                              break;
                        /* go to room with ref_no */
                        case 'g':
                              if(!(tmp_p = strchr(inp, ' ')) || !strtoi(tmp_p+1, &tmp_ret)){
                                    bad_cmd = 1;
                                    break;
                              }

                              pthread_mutex_lock(&cp_arg->cpa_lock);

                              tmp_rm = room_lookup(*cp_arg->rml, NULL, tmp_ret);

                              pthread_mutex_unlock(&cp_arg->cpa_lock);

                              if(!tmp_rm)break;
                              cur_room = tmp_rm;
                              p_rm_switch(cur_room);
                              break;
                        /* go to next room with same first character in label */
                        case 'n':
                              if(!cur_room){
                                    bad_cmd = 1;
                                    break;
                              }
                              /* to allow for circular cycling */
                              cur_room = (cur_room->next) ? cur_room->next : cur_room->bookend_rm;
                              p_rm_switch(cur_room);
                              break;
                        case 'c':
                              if(!(tmp_p = strchr(inp, ' '))){
                                    bad_cmd = 1;
                                    break;
                              }
                              if((tmp_ret = create_room(tmp_p+1, cp_arg->sock))){/* TODO */}
                              #ifdef ASH_DEBUG
                              printf("ret val of create room: %i\n", tmp_ret);
                              printf("sent to socket: %i\n", cp_arg->sock);
                              #endif
                              break;
                        case 'l':

                              pthread_mutex_lock(&cp_arg->cpa_lock);

                              for(int i = 0; cp_arg->rml->in_use[i] != -1; ++i){
                                    for(struct room_lst* rl = cp_arg->rml->rooms[cp_arg->rml->in_use[i]]; rl; rl = rl->next)
                                          printf("%s%s%s: \"%s%s%s\": %s%i%s\n",
                                          (rl->creator == cp_arg->rml->me) ? ANSI_BLU : ANSI_NON, 
                                          get_uname(rl->creator, cp_arg->uname_table), ANSI_NON,
                                          (rl == cur_room) ? ANSI_BLU : ANSI_NON,
                                          rl->label, ANSI_NON, (rl == cur_room) ? ANSI_BLU : ANSI_NON, rl->ref_no, ANSI_NON);
                              }

                              pthread_mutex_unlock(&cp_arg->cpa_lock);

                              break;
                        case 'w':
                              if(!cur_room)
                                    printf("%syou have not yet joined a room%s\n", ANSI_MGNTA, ANSI_NON);
                              else 
                                    printf("%scurrent room (%s%i%s) is \"%s%s%s\"%s\n", ANSI_MGNTA, ANSI_RED,
                                    cur_room->ref_no, ANSI_MGNTA, ANSI_RED, cur_room->label, ANSI_MGNTA, ANSI_NON);
                              break;
                        /* number of users */
                        case 'u':
                        case '#':

                              pthread_mutex_lock(&cp_arg->cpa_lock);

                              cp_arg->n_mem_req = 1;

                              pthread_mutex_unlock(&cp_arg->cpa_lock);

                              req_n_mem(cp_arg->sock);
                              break;
                        /* exit or e[x]it */
                        case 'e':
                        case 'x':
                              if(!cur_room){
                                    bad_cmd = 1;
                                    break;
                              }
                              printf("%syou have left \"%s%s%s\" (%s%i%s)%s\n",
                              ANSI_MGNTA, ANSI_RED, cur_room->label, ANSI_MGNTA,
                              ANSI_RED, cur_room->ref_no, ANSI_MGNTA, ANSI_NON);
                              cur_room = NULL;
                              break;
                        /* time remaining */
                        case 't':

                              pthread_mutex_lock(&cp_arg->cpa_lock);

                              if(cp_arg->dur != -1 && cp_arg->dur_recvd != -1){
                                    print_dur(cp_arg);
                                    pthread_mutex_unlock(&cp_arg->cpa_lock);
                                    break;
                              }
                              cp_arg->dur_req = 1;

                              pthread_mutex_unlock(&cp_arg->cpa_lock);

                              req_board_duration(cp_arg->sock);
                              break;
                        /* sends a deletion request for current board */
                        case 'd':
                              printf("%sdeletion request has been sent -- %sauthenticating%s\n",
                              ANSI_MGNTA, ANSI_RED, ANSI_NON);
                              rm_board(cp_arg->sock);
                              break;
                        case 'h':
                              p_help();
                              break;
                        /* TODO: decide if this should be a bad cmd not a good msg */
                        default:
                              good_msg = 1;
                              break;
                  }
                  if(bad_cmd)p_cmd_err(inp);
            }
            /* we're sending a regular message */
            if(!cur_room){
                  /* we could have been sent here by a bad command breaking */
                  if(good_msg)printf("%syou must first enter a room before replying%s\n", ANSI_RED, ANSI_NON);
                  else{
                        putchar('\r');
                        for(int i = 0; i < b_read; ++i)putchar(' ');
                        putchar('\r');
                  }
            }
            else if(good_msg)
                  reply_room(cur_room->ref_no, inp, cp_arg->sock);
            if(free_s)free(inp);
      }
      kill(getpid(), SIGINT);
      return NULL;
}

void ex(int x){(void)x; run = 0;}

_Bool client(char* sock_path){
      struct ash_table ut;
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

      struct client_pth_arg cpa;
      cpa.uname_table = &ut;
      cpa.sock = sock;
      cpa.rml = &rml;
      strncpy(cpa.rml->board_path, sock_path, PATH_MAX);

      cpa.n_mem_req = cpa.dur_req = 0;
      cpa.dur = cpa.dur_recvd = -1;

      pthread_mutex_init(&cpa.cpa_lock, NULL);

      pthread_t read_notif_pth_pth, repl_pth_pth;
      /* TODO: fix possible synch issues from sharing cpa.rml */
      /* there should be an rml mutex lock - activated each time a 
       * room is added, in_use is edited, or room_lookup is called
       */
      pthread_create(&read_notif_pth_pth, NULL, &read_notif_pth, &cpa);
      pthread_create(&repl_pth_pth, NULL, &repl_pth, &cpa);

      pthread_detach(read_notif_pth_pth);
      pthread_detach(repl_pth_pth);

      /* doesn't need to be memset(*, 0, **)'d - this is handled by insert_msg_msg_queue */
      char tmp_p[201];
      uid_t s_uid;

      signal(SIGINT, ex);

      while(run){
            if(cur_room && pop_msg_queue(cur_room, tmp_p, &s_uid))
                  printf("%s%s%s: %s\r\n", ANSI_GRE, get_uname(s_uid, &ut), ANSI_NON, tmp_p);
            usleep(10000);
      }
      /* TODO: is this safe? */
      pthread_mutex_destroy(&cpa.cpa_lock);
      free_rm_hash_lst(rml);
      free_ash_table(&ut, 0);
      return 1;
}
