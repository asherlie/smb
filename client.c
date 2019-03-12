#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "client.h"
#include "shared.h"

struct room_lst* cur_room;

struct rm_hash_lst init_rm_hash_lst(int buckets){
      struct rm_hash_lst rml;
      rml.n = 0;
      rml.bux = buckets;

      rml.rooms = calloc(rml.bux, sizeof(struct room_lst));
      rml.in_use = malloc(sizeof(int)*(rml.bux+1));
      memset(rml.in_use, -1, sizeof(int)*(rml.bux+1));

      /*
       *pthread_t pth;
       *pthread_create(&pth, NULL, &read_notif_pth, &);
       */
      return rml;
}

/* looks up a thread by its label or ref_no 
 * if both are provided, label is hashed
 * for lookup
 */
struct room_lst* room_lookup(struct rm_hash_lst rml, char* rm_name, int ref_no){
      if(!rm_name){
            for(int i = 0; rml.in_use[i] != -1; ++i){
                  for(struct room_lst* cur = rml.rooms[rml.in_use[i]]; cur;
                      cur = cur->next){
                        if(cur->ref_no == ref_no)return cur;
                  }
            }
            return NULL;
      }
      int ind = *rm_name % rml.bux;
      if(!rml.rooms[ind])return NULL;
      for(struct room_lst* cur = rml.rooms[ind]; cur; cur = cur->next)
            /* TODO: substrings in the middle of cur->label should be searchable */
            if(strstr(cur->label, rm_name))return cur;
      return NULL;
}

/* return existence of ref_no */
struct room_lst* add_room_rml(struct rm_hash_lst* rml, int ref_no, char* name, uid_t creator){
      /* int ind = ref_no % rml->bux; */
      /* TODO: should more chars be summed for hashing */
      // TODO: ref_no should be used for hashing because the most
      // expensive lookup is done using ref_no from read_notif_pth
      int ind = *name % rml->bux;
      struct room_lst* cur;
      if(!rml->rooms[ind]){
            cur = rml->rooms[ind] = malloc(sizeof(struct room_lst));
            rml->in_use[rml->n++] = ind;
      }
      else{
            /* TODO: DON'T ITERATE THROUGH EVERYTHING - KEEP A PTR TO LAST */
            /* we're stopping short so that cur is not always == NULL after this */
            struct room_lst* tmp_cur;
            for(tmp_cur = rml->rooms[ind]; tmp_cur->next; tmp_cur = tmp_cur->next)
                  if(tmp_cur->ref_no == ref_no)return NULL;
            cur = tmp_cur->next = malloc(sizeof(struct room_lst));
      }
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
      /* TODO: destroy this */
      pthread_mutex_init(&cur->room_msg_queue_lck, NULL);

      cur->next = NULL;

      return cur;
}

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
            *sender = (*rm->msg_queue).sender;
            strncpy(msg, (*rm->msg_queue).msg, 200);
            ++rm->msg_queue; ++rm->msg_queue_cap; --rm->n_msg;
            ret = 1;
      }
      pthread_mutex_unlock(&rm->room_msg_queue_lck);
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
      struct room_lst* cur_r = NULL;
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

            /* if we've received a msgtype_notif, add room */
            if(msg_type == MSGTYPE_NOTIF){
                  add_room_rml(rnp_arg->rml, ref_no, buf, uid);
                  printf("%s%i%s: %s[ROOM_CREATE %s]%s\n", ANSI_GRE, uid, ANSI_NON, ANSI_RED, buf, ANSI_NON);
            }
            /* as of now, only other msg_type is MSGTYPE_MSG */
            else{
                  // TODO: room lookup is too slow without label
                  // TODO: should ref_no be used to hash?
                  // this is the most frequent lookup
                  cur_r = room_lookup(*rnp_arg->rml, NULL, ref_no);
                  if(!cur_r){
                        cur_r = add_room_rml(rnp_arg->rml, ref_no, "{UNKNOWN_LABEL}", uid);
                        printf("%s%i%s: %s[LEGACY_ROOM_CREATE {UNKNOWN_LABEL}]%s\n", ANSI_GRE, uid, ANSI_NON, ANSI_RED, ANSI_NON);
                  }
                  /* adding message to msg stack */
                  /* if the above code is being used, no need to check cur_r */
                  // if(cur_r)insert_msg_msg_queue(cur_r, buf, uid);
                  insert_msg_msg_queue(cur_r, buf, uid);
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
int create_room(char* rm_name, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_CREATE_THREAD;
      mb_a.mb_inf[1] = -1;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, rm_name, 200);
      return send_mb_r(mb_a, sock);
}

int reply_room(int th_ref_no, char* msg, int sock){
      struct mb_msg mb_a;
      mb_a.mb_inf[0] = MSG_REPLY_THREAD;
      mb_a.mb_inf[1] = th_ref_no;
      memset(mb_a.str_arg, 0, 201);
      strncpy(mb_a.str_arg, msg, 200);
      return send_mb_r(mb_a, sock);
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
            "/[e]xit:\n"
            "/e[x]it:\n"
            "  exits current room\n"
      , ANSI_BLU, ANSI_NON);
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
                        /* both /join and /room will join an existing room */
                        case 'j':
                        case 'r':
                              /* TODO: rooms should be joinable by ref_no */
                              if(!(tmp_p = strchr(inp, ' ')))break;
                              cur_room = room_lookup(*rnp_arg->rml, tmp_p+1, -1);
                              if(!cur_room)printf("%sno room containing \"%s\" was found%s\n", ANSI_RED, tmp_p+1, ANSI_NON);
                              else printf("%scurrent room has been switched to \"%s\"%s\n", ANSI_MGNTA, cur_room->label, ANSI_NON);
                              break;
                        /* go to next room with same first character in label */
                        case 'n':
                              if(!cur_room || !cur_room->next)break;
                              cur_room = cur_room->next;
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
                                          printf("%i: \"%s%s%s\": %i\n", rl->creator, (rl == cur_room) ? ANSI_BLU : ANSI_NON, rl->label, ANSI_NON, rl->ref_no);
                              }
                              break;
                        case 'w':
                              if(!cur_room)
                                    printf("%syou have not yet joined a room%s\n", ANSI_MGNTA, ANSI_NON);
                              else 
                                    printf("%scurrent room is \"%s\"%s\n", ANSI_MGNTA, cur_room->label, ANSI_NON);
                              break;
                        /* exit or e[x]it */
                        case 'e':
                        case 'x':
                              if(!cur_room)break;
                              printf("%syou have left \"%s\"%s\n", ANSI_MGNTA, cur_room->label, ANSI_NON);
                              cur_room = NULL;
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

_Bool client(char* sock_path){
      cur_room = NULL;
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

      printf("%swelcome to **%s%s%s** %s\n", ANSI_BLU, ANSI_MGNTA, sock_path, ANSI_BLU, ANSI_NON);
      p_help();

      struct rm_hash_lst rml = init_rm_hash_lst(100);

      struct read_notif_pth_arg rnpa;
      rnpa.sock = sock;
      rnpa.rml = &rml;

      pthread_t read_notif_pth_pth, repl_pth_pth;
      /* TODO: fix possible synch issues from sharing rnpa.rml */
      pthread_create(&read_notif_pth_pth, NULL, &read_notif_pth, &rnpa);
      pthread_create(&repl_pth_pth, NULL, &repl_pth, &rnpa);

      /* doesn't need to be memset(*, 0, *)'d - this is handled by insert_msg_msg_queue */
      char tmp_p[201];
      uid_t s_uid;

      while(1){
            // pop_msg_queue
            // if(cur_room && (tmp_p = pop_msg_queue(cur_room)))puts(tmp_p);
            if(cur_room && pop_msg_queue(cur_room, tmp_p, &s_uid))printf("%s%i%s: %s\n", ANSI_GRE, s_uid, ANSI_NON, tmp_p);
            usleep(1000);
      }
}
