#ifndef _LIST_H
#define _LIST_H

typedef struct req_list_t {
	void *key;
	void *value;
	struct req_list_t *next;
	struct req_list_t *prev;
} req_list_t;

void req_list_add(req_list_t *root, req_list_t *new_tree);
req_list_t *req_list_get_last(req_list_t *root);
req_list_t *req_list_get_first(req_list_t *root);
void req_list_remove(req_list_t *tree);
void req_list_clear(req_list_t *root);
req_list_t *req_list_set_key(req_list_t *root, const char *key, const char *value);
req_list_t *req_list_get_key(req_list_t *root, const char *key);
int req_list_check_key(req_list_t *root, const char *key, const char *value);
req_list_t *req_list_set_from_string(req_list_t *root, const char *data); //data = "key=value"
#endif
