#define ASHIO_VER "1.1.3"

struct tabcom_entry{
      void* data_douplep;
      int data_blk_sz, data_offset, optlen;
};

struct tabcom{
      struct tabcom_entry* tbce;
      int n, cap;
};

/* tabcom operations */

struct tabcom* init_tabcom(struct tabcom* tbc);
void free_tabcom(struct tabcom* tbc);
int insert_tabcom(struct tabcom* tbc, void* data_douplep, int data_blk_sz, int data_offset, int optlen);
struct tabcom_entry pop_tabcom(struct tabcom* tbc);

/* term behavior */

void raw_mode();
void reset_term();

/* reading from stdin */

char* getline_raw(int* bytes_read, _Bool* tab, int* ignore);

char* tab_complete(struct tabcom* tbc, char iter_opts, int* bytes_read, _Bool* free_s);
