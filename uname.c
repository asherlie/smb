#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>

#include "uname.h"



#include <stdio.h>
struct uname_table* uname_table_init(struct uname_table* table, int bux){
      if(!table)table = malloc(sizeof(struct uname_table));
      table->bux = bux;
      table->names = calloc(table->bux, sizeof(struct name_entry*));
      return table;
}

/* returns population of new index */
_Bool insert_name(uid_t uid, char* name, struct uname_table* table){
      _Bool init = 1;
      int ind = uid % table->bux;
      /* if it exists, we need to find the last entry */
      struct name_entry* cur;
      if(table->names[ind]){
            init = 0;
            for(cur = table->names[ind];
                cur->next; cur = cur->next);
            cur = cur->next = malloc(sizeof(struct name_entry));
      }
      else cur = table->names[ind] = malloc(sizeof(struct name_entry));
      cur->next = NULL;
      cur->uid = uid;
      strncpy(cur->name, name, 32);
      return init;
}

char* lookup_name(uid_t uid, struct uname_table* table){
      int ind = uid % table->bux;
      for(struct name_entry* e = table->names[ind];
          e; e = e->next){
            if(e->uid == uid)return e->name;
      }
      return NULL;
}

char* get_uname(uid_t uid, struct uname_table* table){
      char* ret;
      if((ret = lookup_name(uid, table)))return ret;
      struct passwd* pw = getpwuid(uid);
      if(!pw)return "{UNKNOWN}";//NULL;
      insert_name(uid, ret = pw->pw_name, table);
      return ret;
}

void free_uname_table(struct uname_table* table){
      for(int i = 0; i < table->bux; ++i)
            if(table->names[i])free(table->names[i]);
      free(table->names);
}
