#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>

#include "ash_table.h"

struct ash_table* ash_table_init(struct ash_table* table, int bux){
      if(!table)table = malloc(sizeof(struct ash_table));
      table->bux = bux;
      /* this is causing an internal leak
       * the leak cannot occur from free_ash_table
       * because it frees table->names
       */
      table->names = calloc(table->bux, sizeof(struct ash_entry*));
      return table;
}

/* returns population of new index */
/* either name or data must be set */
_Bool insert_ash_table(int ref, char* name, void* data, struct ash_table* table){
      _Bool init = 1;
      int ind = ref % table->bux;
      /* if it exists, we need to find the last entry */
      struct ash_entry* cur;
      if(table->names[ind]){
            init = 0;
            for(cur = table->names[ind];
                cur->next; cur = cur->next);
            cur = cur->next = malloc(sizeof(struct ash_entry));
      }
      else cur = table->names[ind] = malloc(sizeof(struct ash_entry));
      cur->next = NULL;
      cur->ref = ref;
      if(name)strncpy(cur->name, name, 32);
      /* we don't check if(data) before setting because we want data to default to NULL */
      cur->data = data;
      return init;
}

struct ash_entry* lookup_ash_table(int ref, struct ash_table* table){
      if(ref < 0)return NULL;
      int ind = ref % table->bux;
      for(struct ash_entry* e = table->names[ind]; e; e = e->next)
            if(e->ref == ref)return e;
      return NULL;
}

char* lookup_str_ash_table(int ref, struct ash_table* table){
      struct ash_entry* e = lookup_ash_table(ref, table);
      return e ? e->name : NULL;
}

void* lookup_data_ash_table(int ref, struct ash_table* table){
      struct ash_entry* e = lookup_ash_table(ref, table);
      return e ? e->data : NULL;
}

void free_ash_table(struct ash_table* table){
      for(int i = 0; i < table->bux; ++i)
            if(table->names[i])free(table->names[i]);
      free(table->names);
}

void free_ash_table_data(struct ash_table* table){
      for(int i = 0; i < table->bux; ++i)
            if(table->names[i]){
                  free(table->names[i]->data);
                  free(table->names[i]);
            }
      free(table->names);
}
