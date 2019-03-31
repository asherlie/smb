#include <sys/types.h>

struct name_entry{
      uid_t uid;
      /* man useradd - usernames are <= 32 chars */
      char name[33];
      struct name_entry* next;
};

struct uname_table{
      int bux;
      struct name_entry** names;
};

char* get_uname(uid_t uid, struct uname_table* table);
struct uname_table* uname_table_init(struct uname_table* table, int bux);
void free_uname_table(struct uname_table* table);
