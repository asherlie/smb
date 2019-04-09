#include <sys/types.h>

struct ash_entry{
      int ref;
      /* man useradd - usernames are <= 32 chars */
      char name[33];
      void* data;
      struct ash_entry* next;
};

struct ash_table{
      int bux;
      struct ash_entry** names;
};

_Bool insert_ash_table(int ref, char* name, void* data, struct ash_table* table);
char* lookup_str_ash_table(int ref, struct ash_table* table);
struct ash_table* ash_table_init(struct ash_table* table, int bux);
void free_ash_table(struct ash_table* table);
