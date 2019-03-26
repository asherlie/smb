#include <sys/types.h>

struct name_entry{
      uid_t uid;
      char* name;
      struct name_entry* next;
};

struct uname_table{
      int bux;
      struct name_entry** names;
};

char* get_uname(uid_t uid, struct uname_table* table);
struct uname_table* uname_table_init(struct uname_table* table, int bux);
