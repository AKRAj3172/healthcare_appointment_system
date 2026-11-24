
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ID_LEN        16         
#define KEY_LEN       48
#define INPUT_LEN     128
#define MAX_NAME      80
#define MAX_PHONE     30
#define MAX_EMAIL     100
#define MAX_INFO      200
#define MAX_SPEC      60
#define MAX_DAYS      7
#define SLOT_MINUTES  60
#define INITIAL_BUCKETS 31



typedef struct {
    char patient_id[ID_LEN];
    char name[MAX_NAME];
    int  age;
    char phone[MAX_PHONE];
    char email[MAX_EMAIL];
    char medical_info[MAX_INFO];
} Patient;

typedef struct {
    char doctor_id[ID_LEN];
    char name[MAX_NAME];
    char specialization[MAX_SPEC];
    char available_days[MAX_DAYS][15];
    int  day_count;
} Doctor;

typedef struct BSTNode {
    int  key;
    int  height;
    char time_label[16];
    int  duration;
    struct BSTNode *left, *right;
} BSTNode;

typedef struct {
    char doctor_id[ID_LEN];
    char date[11];                 
    BSTNode *root;                
    pthread_mutex_t mutex;
} DaySchedule;

typedef struct {
    char appointment_id[ID_LEN];
    char patient_id[ID_LEN];
    char doctor_id[ID_LEN];
    char date[11];
    char time_label[16];
    int  key;                    
    char status[12];             
    pthread_mutex_t lock;          
} Appointment;

typedef struct HashNode {
    char key[KEY_LEN];
    void *value;
    struct HashNode *next;
} HashNode;

typedef struct {
    HashNode **buckets;
    size_t capacity;
    size_t count;
    pthread_mutex_t lock;
} HashMap;

typedef struct { char key[KEY_LEN]; void *value; } Entry;

typedef struct {
    HashMap patients;
    HashMap doctors;
    HashMap appointments;
    HashMap schedules;             
    HashMap patient_slots;         
    unsigned appointment_counter;
    pthread_mutex_t counter_lock;
    pthread_mutex_t schedule_create_lock;
} SystemState;

static SystemState app;

typedef enum {
    ST_OK = 0, ST_NO_PATIENT, ST_NO_DOCTOR, ST_BAD_DATE, ST_BAD_TIME,
    ST_DOCTOR_OFF, ST_NOT_OFFERED, ST_SLOT_TAKEN, ST_PATIENT_CLASH,
    ST_NO_APPT, ST_NOT_ACTIVE, ST_SAME_SLOT, ST_NOMEM
} Status;

static const char *status_text(Status s) {
    switch (s) {
    case ST_OK:           return "OK";
    case ST_NO_PATIENT:   return "Patient is not registered.";
    case ST_NO_DOCTOR:    return "Doctor not found.";
    case ST_BAD_DATE:     return "Invalid date. Use a real calendar date, e.g. 2026-11-20.";
    case ST_BAD_TIME:     return "Invalid time. Example: 02:00 PM";
    case ST_DOCTOR_OFF:   return "Doctor does not work on that weekday.";
    case ST_NOT_OFFERED:  return "That time is not one of the offered slots (09:00-12:00, 02:00-04:00).";
    case ST_SLOT_TAKEN:   return "Slot is already booked.";
    case ST_PATIENT_CLASH:return "Patient already has another appointment at that date/time.";
    case ST_NO_APPT:      return "Appointment not found.";
    case ST_NOT_ACTIVE:   return "Appointment is not active (already cancelled).";
    case ST_SAME_SLOT:    return "New slot is the same as the current slot.";
    case ST_NOMEM:        return "Out of memory.";
    }
    return "Unknown error.";
}


static void die_oom(void) { perror("malloc"); exit(EXIT_FAILURE); }

static void safe_copy(char *dest, const char *src, size_t size) {
    if (size == 0) return;
    size_t n = strnlen(src, size - 1);
    memcpy(dest, src, n);
    dest[n] = '\0';
}

static void trim(char *s) {
    size_t n = strlen(s), start = 0;
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}

static int input_closed = 0;     

static int read_line(const char *prompt, char *out, size_t size) {
    if (prompt) { printf("%s", prompt); fflush(stdout); }
    if (!fgets(out, (int)size, stdin)) {
        out[0] = '\0';
        input_closed = 1;
        return 0;
    }
    size_t n = strlen(out);
    if (n > 0 && out[n - 1] != '\n' && n == size - 1) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
    trim(out);
    return 1;
}

static int read_int(const char *prompt, int *out) {
    char buf[INPUT_LEN];
    while (1) {
        if (!read_line(prompt, buf, sizeof buf)) return 0;
        char *end;
        long v = strtol(buf, &end, 10);
        if (buf[0] && *end == '\0' && v >= -1000000 && v <= 1000000) {
            *out = (int)v;
            return 1;
        }
        printf("Invalid number. Try again.\n");
    }
}

static int norm_id(const char *in, char *out, size_t size) {
    while (isspace((unsigned char)*in)) in++;
    size_t n = 0;
    while (*in && !isspace((unsigned char)*in)) {
        if (!isalnum((unsigned char)*in) || n + 1 >= size) return 0;
        out[n++] = (char)toupper((unsigned char)*in++);
    }
    while (isspace((unsigned char)*in)) in++;
    if (*in || n == 0) return 0;
    out[n] = '\0';
    return 1;
}

static int time_to_minutes(const char *label) {
    int h, m;
    char ap[3], extra;
    if (sscanf(label, " %d:%d %2s %c", &h, &m, ap, &extra) != 3) return -1;
    for (int i = 0; ap[i]; i++) ap[i] = (char)toupper((unsigned char)ap[i]);
    if (h < 1 || h > 12 || m < 0 || m > 59) return -1;
    if (strcmp(ap, "AM") == 0)      { if (h == 12) h = 0; }
    else if (strcmp(ap, "PM") == 0) { if (h != 12) h += 12; }
    else return -1;
    return h * 60 + m;
}

static void minutes_to_label(int mins, char *out, size_t size) {
    int h24 = mins / 60, m = mins % 60;
    int h12 = h24 % 12 == 0 ? 12 : h24 % 12;
    snprintf(out, size, "%02d:%02d %s", h12, m, h24 >= 12 ? "PM" : "AM");
}

static int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int days_in_month(int y, int m) {
    static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

static void format_date(char out[11], int y, int m, int d) {
    char tmp[40];
    snprintf(tmp, sizeof tmp, "%04d-%02d-%02d", y, m, d);
    safe_copy(out, tmp, 11);
}

static int parse_date(const char *in, char out[11]) {
    int y, m, d;
    char extra;
    if (sscanf(in, " %d-%d-%d %c", &y, &m, &d, &extra) != 3) return 0;
    if (y < 1900 || y > 2200 || m < 1 || m > 12) return 0;
    if (d < 1 || d > days_in_month(y, m)) return 0;
    format_date(out, y, m, d);
    return 1;
}

static void next_day(char date[11]) {
    int y = 2000, m = 1, d = 1;
    if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return;
    if (++d > days_in_month(y, m)) { d = 1; if (++m > 12) { m = 1; y++; } }
    format_date(date, y, m, d);
}

static const char *weekday_from_date(const char *date) {
    static const char *names[] = {"Sunday","Monday","Tuesday","Wednesday",
                                  "Thursday","Friday","Saturday"};
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    int y, m, d;
    if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3) return "Invalid";
    if (m < 3) y--;
    return names[(y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7];
}

static const int OFFERED[] = {540, 600, 660, 720, 840, 900, 960};
#define OFFERED_COUNT ((int)(sizeof OFFERED / sizeof OFFERED[0]))

static int is_offered_slot(int key) {
    for (int i = 0; i < OFFERED_COUNT; i++) if (OFFERED[i] == key) return 1;
    return 0;
}

static volatile int g_race_amplifier = 0;
static void race_window(void) {
    if (g_race_amplifier) {
        struct timespec ts = {0, 20000};          
        nanosleep(&ts, NULL);
    }
}


static unsigned long hash_str(const char *k) {
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*k++)) h = ((h << 5) + h) ^ (unsigned long)c;
    return h;
}

static void hashmap_init(HashMap *m, size_t cap) {
    m->buckets = (HashNode **)calloc(cap, sizeof(HashNode *));
    if (!m->buckets) die_oom();
    m->capacity = cap;
    m->count = 0;
    pthread_mutex_init(&m->lock, NULL);
}

static void hashmap_rehash_locked(HashMap *m) {
    size_t ncap = m->capacity * 2 + 1;
    HashNode **nb = (HashNode **)calloc(ncap, sizeof(HashNode *));
    if (!nb) return;                         
    for (size_t i = 0; i < m->capacity; i++) {
        HashNode *n = m->buckets[i];
        while (n) {
            HashNode *next = n->next;
            size_t idx = hash_str(n->key) % ncap;
            n->next = nb[idx];
            nb[idx] = n;
            n = next;
        }
    }
    free(m->buckets);
    m->buckets = nb;
    m->capacity = ncap;
}


static int hashmap_put_impl(HashMap *m, const char *key, void *value, int overwrite) {
    pthread_mutex_lock(&m->lock);
    size_t idx = hash_str(key) % m->capacity;
    for (HashNode *n = m->buckets[idx]; n; n = n->next) {
        if (strcmp(n->key, key) == 0) {
            if (overwrite) n->value = value;
            pthread_mutex_unlock(&m->lock);
            return 0;
        }
    }
    HashNode *node = (HashNode *)malloc(sizeof *node);
    if (!node) die_oom();
    safe_copy(node->key, key, sizeof node->key);
    node->value = value;
    node->next = m->buckets[idx];
    m->buckets[idx] = node;
    m->count++;
    if (m->count * 4 > m->capacity * 3) hashmap_rehash_locked(m);
    pthread_mutex_unlock(&m->lock);
    return 1;
}

static void hashmap_put(HashMap *m, const char *k, void *v)      { hashmap_put_impl(m, k, v, 1); }
static int  hashmap_put_if_absent(HashMap *m, const char *k, void *v) { return hashmap_put_impl(m, k, v, 0); }

static void *hashmap_get(HashMap *m, const char *key) {
    pthread_mutex_lock(&m->lock);
    void *res = NULL;
    for (HashNode *n = m->buckets[hash_str(key) % m->capacity]; n; n = n->next)
        if (strcmp(n->key, key) == 0) { res = n->value; break; }
    pthread_mutex_unlock(&m->lock);
    return res;
}

static int hashmap_remove(HashMap *m, const char *key) {
    pthread_mutex_lock(&m->lock);
    size_t idx = hash_str(key) % m->capacity;
    HashNode *cur = m->buckets[idx], *prev = NULL;
    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            if (prev) prev->next = cur->next; else m->buckets[idx] = cur->next;
            free(cur);
            m->count--;
            pthread_mutex_unlock(&m->lock);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&m->lock);
    return 0;
}

static int entry_cmp(const void *a, const void *b) {
    const Entry *x = (const Entry *)a, *y = (const Entry *)b;
    size_t lx = strlen(x->key), ly = strlen(y->key);
    if (lx != ly) return lx < ly ? -1 : 1;    
    return strcmp(x->key, y->key);
}

static size_t hashmap_snapshot(HashMap *m, Entry **out) {
    pthread_mutex_lock(&m->lock);
    size_t n = m->count, k = 0;
    Entry *arr = (Entry *)malloc((n ? n : 1) * sizeof *arr);
    if (!arr) die_oom();
    for (size_t i = 0; i < m->capacity; i++)
        for (HashNode *h = m->buckets[i]; h; h = h->next) {
            safe_copy(arr[k].key, h->key, KEY_LEN);
            arr[k].value = h->value;
            k++;
        }
    pthread_mutex_unlock(&m->lock);
    qsort(arr, k, sizeof *arr, entry_cmp);
    *out = arr;
    return k;
}

static void hashmap_destroy(HashMap *m, void (*free_value)(void *)) {
    for (size_t i = 0; i < m->capacity; i++) {
        HashNode *n = m->buckets[i];
        while (n) {
            HashNode *next = n->next;
            if (free_value) free_value(n->value);
            free(n);
            n = next;
        }
    }
    free(m->buckets);
    m->buckets = NULL;
    m->count = 0;
    pthread_mutex_destroy(&m->lock);
}


static int ht(const BSTNode *n) { return n ? n->height : 0; }

static void update_height(BSTNode *n) {
    int a = ht(n->left), b = ht(n->right);
    n->height = 1 + (a > b ? a : b);
}

static BSTNode *rotate_right(BSTNode *y) {
    BSTNode *x = y->left;
    y->left = x->right;
    x->right = y;
    update_height(y);
    update_height(x);
    return x;
}

static BSTNode *rotate_left(BSTNode *x) {
    BSTNode *y = x->right;
    x->right = y->left;
    y->left = x;
    update_height(x);
    update_height(y);
    return y;
}

static BSTNode *rebalance(BSTNode *n) {
    update_height(n);
    int bf = ht(n->left) - ht(n->right);
    if (bf > 1) {
        if (ht(n->left->left) < ht(n->left->right)) n->left = rotate_left(n->left);
        return rotate_right(n);
    }
    if (bf < -1) {
        if (ht(n->right->right) < ht(n->right->left)) n->right = rotate_right(n->right);
        return rotate_left(n);
    }
    return n;
}

static BSTNode *bst_create_node(int key, const char *label, int duration) {
    BSTNode *node = (BSTNode *)malloc(sizeof *node);
    if (!node) die_oom();
    node->key = key;
    node->height = 1;
    safe_copy(node->time_label, label, sizeof node->time_label);
    node->duration = duration;
    node->left = node->right = NULL;
    return node;
}

static BSTNode *bst_insert(BSTNode *root, int key, const char *label, int duration) {
    if (!root) return bst_create_node(key, label, duration);
    if (key < root->key)      root->left  = bst_insert(root->left,  key, label, duration);
    else if (key > root->key) root->right = bst_insert(root->right, key, label, duration);
    else return root;                       
    return rebalance(root);
}

static BSTNode *bst_search(BSTNode *root, int key) {
    while (root) {
        if (key == root->key) return root;
        root = key < root->key ? root->left : root->right;
    }
    return NULL;
}

static BSTNode *bst_minimum(BSTNode *root) {
    while (root && root->left) root = root->left;
    return root;
}

static BSTNode *bst_delete(BSTNode *root, int key) {
    if (!root) return NULL;
    if (key < root->key) {
        root->left = bst_delete(root->left, key);
    } else if (key > root->key) {
        root->right = bst_delete(root->right, key);
    } else {
        if (!root->left)  { BSTNode *r = root->right; free(root); return r; }
        if (!root->right) { BSTNode *l = root->left;  free(root); return l; }
        BSTNode *succ = bst_minimum(root->right);
        root->key = succ->key;
        safe_copy(root->time_label, succ->time_label, sizeof root->time_label);
        root->duration = succ->duration;
        root->right = bst_delete(root->right, succ->key);
    }
    return rebalance(root);
}

static void bst_inorder(const BSTNode *root) {
    if (!root) return;
    bst_inorder(root->left);
    printf("  %-10s (%d min, %d-minute slot)\n", root->time_label, root->key, root->duration);
    bst_inorder(root->right);
}

static int bst_count(const BSTNode *root) {
    return root ? 1 + bst_count(root->left) + bst_count(root->right) : 0;
}

static void bst_free(BSTNode *root) {
    if (!root) return;
    bst_free(root->left);
    bst_free(root->right);
    free(root);
}

static int bst_verify(const BSTNode *n, long lo, long hi, int *count) {
    if (!n) return 0;
    if (n->key <= lo || n->key >= hi) return -1;
    (*count)++;
    int l = bst_verify(n->left, lo, n->key, count);
    if (l < 0) return -1;
    int r = bst_verify(n->right, n->key, hi, count);
    if (r < 0) return -1;
    if (l - r > 1 || r - l > 1) return -1;
    int h = 1 + (l > r ? l : r);
    return h == n->height ? h : -1;
}


static void make_sched_key(char *out, size_t sz, const char *doc, const char *date) {
    snprintf(out, sz, "%s|%s", doc, date);
}

static void make_slot_key(char *out, size_t sz, const char *pid, const char *date, int key) {
    snprintf(out, sz, "%s|%s|%d", pid, date, key);
}

static DaySchedule *get_schedule(const char *doctor_id, const char *date, int create) {
    char key[KEY_LEN];
    make_sched_key(key, sizeof key, doctor_id, date);

    DaySchedule *s = (DaySchedule *)hashmap_get(&app.schedules, key);
    if (s || !create) return s;

    pthread_mutex_lock(&app.schedule_create_lock);
    s = (DaySchedule *)hashmap_get(&app.schedules, key);     
    if (!s) {
        s = (DaySchedule *)calloc(1, sizeof *s);
        if (!s) die_oom();
        safe_copy(s->doctor_id, doctor_id, sizeof s->doctor_id);
        safe_copy(s->date, date, sizeof s->date);
        pthread_mutex_init(&s->mutex, NULL);
        for (int i = 0; i < OFFERED_COUNT; i++) {
            char label[16];
            minutes_to_label(OFFERED[i], label, sizeof label);
            s->root = bst_insert(s->root, OFFERED[i], label, SLOT_MINUTES);
        }
        hashmap_put(&app.schedules, key, s);
    }
    pthread_mutex_unlock(&app.schedule_create_lock);
    return s;
}

static int doctor_available_on(const Doctor *doctor, const char *date) {
    const char *day = weekday_from_date(date);
    for (int i = 0; i < doctor->day_count; i++)
        if (strcmp(doctor->available_days[i], day) == 0) return 1;
    return 0;
}


static Patient *create_patient(const char *id, const char *name, int age,
                               const char *phone, const char *email,
                               const char *medical_info) {
    Patient *p = (Patient *)calloc(1, sizeof *p);
    if (!p) die_oom();
    safe_copy(p->patient_id, id, sizeof p->patient_id);
    safe_copy(p->name, name, sizeof p->name);
    p->age = age;
    safe_copy(p->phone, phone, sizeof p->phone);
    safe_copy(p->email, email, sizeof p->email);
    safe_copy(p->medical_info, medical_info, sizeof p->medical_info);
    return p;
}

static Doctor *create_doctor(const char *id, const char *name, const char *spec,
                             const char *d1, const char *d2, const char *d3) {
    Doctor *d = (Doctor *)calloc(1, sizeof *d);
    if (!d) die_oom();
    safe_copy(d->doctor_id, id, sizeof d->doctor_id);
    safe_copy(d->name, name, sizeof d->name);
    safe_copy(d->specialization, spec, sizeof d->specialization);
    const char *days[] = {d1, d2, d3};
    for (int i = 0; i < 3; i++)
        safe_copy(d->available_days[d->day_count++], days[i], sizeof d->available_days[0]);
    return d;
}


static Appointment *appt_new(void) {
    Appointment *a = (Appointment *)calloc(1, sizeof *a);
    if (!a) return NULL;
    pthread_mutex_init(&a->lock, NULL);
    return a;
}

static void appt_free(void *p) {
    Appointment *a = (Appointment *)p;
    pthread_mutex_destroy(&a->lock);
    free(a);
}

static unsigned next_appointment_number(void) {
    pthread_mutex_lock(&app.counter_lock);
    unsigned n = ++app.appointment_counter;
    pthread_mutex_unlock(&app.counter_lock);
    return n;
}

static Status book_appointment(const char *pid_in, const char *did_in,
                               const char *date_in, const char *time_in,
                               Appointment **out) {
    char pid[ID_LEN], did[ID_LEN], date[11], label[16];
    if (out) *out = NULL;

    if (!norm_id(pid_in, pid, sizeof pid) || !hashmap_get(&app.patients, pid)) return ST_NO_PATIENT;
    if (!norm_id(did_in, did, sizeof did)) return ST_NO_DOCTOR;
    Doctor *doctor = (Doctor *)hashmap_get(&app.doctors, did);
    if (!doctor) return ST_NO_DOCTOR;
    if (!parse_date(date_in, date)) return ST_BAD_DATE;
    int key = time_to_minutes(time_in);
    if (key < 0) return ST_BAD_TIME;
    if (!doctor_available_on(doctor, date)) return ST_DOCTOR_OFF;
    if (!is_offered_slot(key)) return ST_NOT_OFFERED;
    minutes_to_label(key, label, sizeof label);

    DaySchedule *s = get_schedule(did, date, 1);
    Appointment *a = appt_new();
    if (!a) return ST_NOMEM;

    char pkey[KEY_LEN];
    make_slot_key(pkey, sizeof pkey, pid, date, key);

    pthread_mutex_lock(&s->mutex);

    if (!bst_search(s->root, key)) {
        pthread_mutex_unlock(&s->mutex);
        appt_free(a);
        return ST_SLOT_TAKEN;
    }
    race_window();
    if (!hashmap_put_if_absent(&app.patient_slots, pkey, a)) {   
        pthread_mutex_unlock(&s->mutex);
        appt_free(a);
        return ST_PATIENT_CLASH;
    }

    s->root = bst_delete(s->root, key);

    snprintf(a->appointment_id, sizeof a->appointment_id, "APT%u", next_appointment_number());
    safe_copy(a->patient_id, pid, sizeof a->patient_id);
    safe_copy(a->doctor_id, did, sizeof a->doctor_id);
    safe_copy(a->date, date, sizeof a->date);
    safe_copy(a->time_label, label, sizeof a->time_label);
    a->key = key;
    safe_copy(a->status, "CONFIRMED", sizeof a->status);
    hashmap_put(&app.appointments, a->appointment_id, a);

    pthread_mutex_unlock(&s->mutex);

    if (out) *out = a;
    return ST_OK;
}

static Status cancel_appointment(const char *id_in, Appointment **out) {
    char id[ID_LEN];
    if (out) *out = NULL;
    if (!norm_id(id_in, id, sizeof id)) return ST_NO_APPT;
    Appointment *a = (Appointment *)hashmap_get(&app.appointments, id);
    if (!a) return ST_NO_APPT;

    pthread_mutex_lock(&a->lock);
    race_window();
    if (strcmp(a->status, "CONFIRMED") != 0) {
        pthread_mutex_unlock(&a->lock);
        return ST_NOT_ACTIVE;
    }

    DaySchedule *s = get_schedule(a->doctor_id, a->date, 1);
    char pkey[KEY_LEN];
    make_slot_key(pkey, sizeof pkey, a->patient_id, a->date, a->key);

    pthread_mutex_lock(&s->mutex);
    race_window();
    s->root = bst_insert(s->root, a->key, a->time_label, SLOT_MINUTES);
    hashmap_remove(&app.patient_slots, pkey);
    safe_copy(a->status, "CANCELLED", sizeof a->status);
    pthread_mutex_unlock(&s->mutex);

    pthread_mutex_unlock(&a->lock);
    if (out) *out = a;
    return ST_OK;
}

static Status reschedule_appointment(const char *id_in, const char *date_in,
                                     const char *time_in, Appointment **out) {
    char id[ID_LEN], date[11], label[16];
    if (out) *out = NULL;
    if (!norm_id(id_in, id, sizeof id)) return ST_NO_APPT;
    Appointment *a = (Appointment *)hashmap_get(&app.appointments, id);
    if (!a) return ST_NO_APPT;

    if (!parse_date(date_in, date)) return ST_BAD_DATE;
    int new_key = time_to_minutes(time_in);
    if (new_key < 0) return ST_BAD_TIME;
    if (!is_offered_slot(new_key)) return ST_NOT_OFFERED;
    minutes_to_label(new_key, label, sizeof label);

    pthread_mutex_lock(&a->lock);
    Status result = ST_OK;

    if (strcmp(a->status, "CONFIRMED") != 0) { result = ST_NOT_ACTIVE; goto done; }

    Doctor *doctor = (Doctor *)hashmap_get(&app.doctors, a->doctor_id);
    if (!doctor || !doctor_available_on(doctor, date)) { result = ST_DOCTOR_OFF; goto done; }
    if (strcmp(a->date, date) == 0 && a->key == new_key) { result = ST_SAME_SLOT; goto done; }

    DaySchedule *old_s = get_schedule(a->doctor_id, a->date, 1);
    DaySchedule *new_s = get_schedule(a->doctor_id, date, 1);

    DaySchedule *first  = old_s < new_s ? old_s : new_s;
    DaySchedule *second = old_s < new_s ? new_s : old_s;
    pthread_mutex_lock(&first->mutex);
    if (second != first) pthread_mutex_lock(&second->mutex);

    char old_pkey[KEY_LEN], new_pkey[KEY_LEN];
    make_slot_key(old_pkey, sizeof old_pkey, a->patient_id, a->date, a->key);
    make_slot_key(new_pkey, sizeof new_pkey, a->patient_id, date, new_key);

    race_window();
    if (!bst_search(new_s->root, new_key)) {
        result = ST_SLOT_TAKEN;
    } else if (!hashmap_put_if_absent(&app.patient_slots, new_pkey, a)) {
        result = ST_PATIENT_CLASH;
    } else {
        new_s->root = bst_delete(new_s->root, new_key);
        old_s->root = bst_insert(old_s->root, a->key, a->time_label, SLOT_MINUTES);
        hashmap_remove(&app.patient_slots, old_pkey);
        safe_copy(a->date, date, sizeof a->date);
        safe_copy(a->time_label, label, sizeof a->time_label);
        a->key = new_key;
    }

    if (second != first) pthread_mutex_unlock(&second->mutex);
    pthread_mutex_unlock(&first->mutex);

done:
    pthread_mutex_unlock(&a->lock);
    if (result == ST_OK && out) *out = a;
    return result;
}


static void print_appointment(const Appointment *a) {
    const Patient *p = (const Patient *)hashmap_get(&app.patients, a->patient_id);
    const Doctor  *d = (const Doctor  *)hashmap_get(&app.doctors,  a->doctor_id);
    printf("  Appointment ID : %s\n", a->appointment_id);
    printf("  Patient        : %s (%s)\n", p ? p->name : "?", a->patient_id);
    printf("  Doctor         : %s (%s)\n", d ? d->name : "?", d ? d->specialization : "?");
    printf("  Date           : %s (%s)\n", a->date, weekday_from_date(a->date));
    printf("  Time           : %s\n", a->time_label);
}

static void show_patients(void) {
    printf("\n================ PATIENTS ================\n");
    Entry *e;
    size_t n = hashmap_snapshot(&app.patients, &e);
    for (size_t i = 0; i < n; i++) {
        const Patient *p = (const Patient *)e[i].value;
        printf("%-6s | %-20s | Age %-3d | %s | %s\n",
               p->patient_id, p->name, p->age, p->phone, p->medical_info);
    }
    printf("Total: %zu\n", n);
    free(e);
}

static void show_doctors(void) {
    printf("\n================ DOCTORS ================\n");
    Entry *e;
    size_t n = hashmap_snapshot(&app.doctors, &e);
    for (size_t i = 0; i < n; i++) {
        const Doctor *d = (const Doctor *)e[i].value;
        printf("%-5s | %-18s | %-18s | ", d->doctor_id, d->name, d->specialization);
        for (int j = 0; j < d->day_count; j++)
            printf("%s%s", d->available_days[j], j + 1 == d->day_count ? "" : ", ");
        printf("\n");
    }
    free(e);
}

static void show_appointments(void) {
    printf("\n================ APPOINTMENTS ================\n");
    Entry *e;
    size_t n = hashmap_snapshot(&app.appointments, &e);
    if (n == 0) printf("No appointments.\n");
    for (size_t i = 0; i < n; i++) {
        const Appointment *a = (const Appointment *)e[i].value;
        printf("%-7s | Patient %-5s | Doctor %-5s | %s %s | %s\n",
               a->appointment_id, a->patient_id, a->doctor_id,
               a->date, a->time_label, a->status);
    }
    free(e);
}

static void patient_appointments_menu(void) {
    char in[INPUT_LEN], pid[ID_LEN];
    read_line("Patient ID: ", in, sizeof in);
    if (!norm_id(in, pid, sizeof pid) || !hashmap_get(&app.patients, pid)) {
        printf("ERROR: %s\n", status_text(ST_NO_PATIENT));
        return;
    }
    Entry *e;
    size_t n = hashmap_snapshot(&app.appointments, &e), shown = 0;
    printf("\nAppointments for %s:\n", pid);
    for (size_t i = 0; i < n; i++) {
        const Appointment *a = (const Appointment *)e[i].value;
        if (strcmp(a->patient_id, pid) != 0) continue;
        printf("  %-7s | %s | %s %s | %s\n", a->appointment_id, a->doctor_id,
               a->date, a->time_label, a->status);
        shown++;
    }
    if (!shown) printf("  (none)\n");
    free(e);
}

static void search_patient(void) {
    char in[INPUT_LEN], id[ID_LEN];
    read_line("Patient ID: ", in, sizeof in);
    const Patient *p = norm_id(in, id, sizeof id) ? (const Patient *)hashmap_get(&app.patients, id) : NULL;
    if (!p) { printf("Patient not found.\n"); return; }
    printf("\nPatient found using Hash Map:\n");
    printf("ID: %s\nName: %s\nAge: %d\nPhone: %s\nEmail: %s\nMedical Info: %s\n",
           p->patient_id, p->name, p->age, p->phone, p->email, p->medical_info);
}

static void show_slots(void) {
    char din[INPUT_LEN], dtin[INPUT_LEN], did[ID_LEN], date[11];
    read_line("Doctor ID: ", din, sizeof din);
    read_line("Date (YYYY-MM-DD): ", dtin, sizeof dtin);

    const Doctor *d = norm_id(din, did, sizeof did) ? (const Doctor *)hashmap_get(&app.doctors, did) : NULL;
    if (!d) { printf("Doctor not found.\n"); return; }
    if (!parse_date(dtin, date)) { printf("ERROR: %s\n", status_text(ST_BAD_DATE)); return; }
    if (!doctor_available_on(d, date)) {
        printf("%s is unavailable on %s (%s).\n", d->name, date, weekday_from_date(date));
        return;
    }

    DaySchedule *s = get_schedule(did, date, 1);
    pthread_mutex_lock(&s->mutex);
    printf("\nAvailable slots for %s on %s (%s)\n", d->name, date, weekday_from_date(date));
    printf("BST inorder traversal (chronological order):\n");
    bst_inorder(s->root);
    printf("Free slots: %d of %d | tree height: %d (AVL keeps it O(log n))\n",
           bst_count(s->root), OFFERED_COUNT, ht(s->root));
    pthread_mutex_unlock(&s->mutex);
}

static void next_available_menu(void) {
    char din[INPUT_LEN], dtin[INPUT_LEN], did[ID_LEN], date[11];
    read_line("Doctor ID: ", din, sizeof din);
    read_line("Search from date (YYYY-MM-DD): ", dtin, sizeof dtin);

    const Doctor *d = norm_id(din, did, sizeof did) ? (const Doctor *)hashmap_get(&app.doctors, did) : NULL;
    if (!d) { printf("Doctor not found.\n"); return; }
    if (!parse_date(dtin, date)) { printf("ERROR: %s\n", status_text(ST_BAD_DATE)); return; }

    for (int i = 0; i < 30; i++, next_day(date)) {
        if (!doctor_available_on(d, date)) continue;
        DaySchedule *s = get_schedule(did, date, 1);
        pthread_mutex_lock(&s->mutex);
        BSTNode *m = bst_minimum(s->root);        
        if (m) {
            printf("Earliest free slot for %s: %s (%s) at %s\n",
                   d->name, date, weekday_from_date(date), m->time_label);
            pthread_mutex_unlock(&s->mutex);
            return;
        }
        pthread_mutex_unlock(&s->mutex);
    }
    printf("No free slots in the next 30 days.\n");
}


typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  cv;
    int go;
} StartGate;

static void gate_init(StartGate *g) {
    pthread_mutex_init(&g->m, NULL);
    pthread_cond_init(&g->cv, NULL);
    g->go = 0;
}
static void gate_wait(StartGate *g) {
    pthread_mutex_lock(&g->m);
    while (!g->go) pthread_cond_wait(&g->cv, &g->m);
    pthread_mutex_unlock(&g->m);
}
static void gate_open(StartGate *g) {
    pthread_mutex_lock(&g->m);
    g->go = 1;
    pthread_cond_broadcast(&g->cv);
    pthread_mutex_unlock(&g->m);
}
static void gate_destroy(StartGate *g) {
    pthread_mutex_destroy(&g->m);
    pthread_cond_destroy(&g->cv);
}

typedef struct {
    StartGate *gate;
    const char *patient_id, *doctor_id, *date, *time;
    int verbose;
    Status status;
} BookingThreadArg;

static void *booking_thread(void *p) {
    BookingThreadArg *a = (BookingThreadArg *)p;
    gate_wait(a->gate);                                 
    a->status = book_appointment(a->patient_id, a->doctor_id, a->date, a->time, NULL);
    if (a->verbose)
        printf("  [thread %s] %s\n", a->patient_id,
               a->status == ST_OK ? "SUCCESS - slot booked" : "REJECTED");
    return NULL;
}

static void run_double_booking_demo(void) {
    const char *did = "D201", *date = "2026-11-20";
    char label[16] = "10:00 AM";
    int key = 600;

    printf("\n============= CONCURRENCY DEMO =============\n");

    DaySchedule *s = get_schedule(did, date, 1);
    pthread_mutex_lock(&s->mutex);
    if (!bst_search(s->root, key)) {
        BSTNode *m = bst_minimum(s->root);
        if (m) { key = m->key; safe_copy(label, m->time_label, sizeof label); }
        else key = -1;
    }
    pthread_mutex_unlock(&s->mutex);

    if (key < 0) {
        printf("No free slots left for %s on %s. Cancel an appointment (option 7) and retry.\n", did, date);
        return;
    }

    printf("Two patients simultaneously request %s %s with %s.\n", date, label, did);

    StartGate gate;
    gate_init(&gate);
    BookingThreadArg a = {&gate, "P104", did, date, label, 1, ST_NOMEM};
    BookingThreadArg b = {&gate, "P109", did, date, label, 1, ST_NOMEM};
    pthread_t t1, t2;
    pthread_create(&t1, NULL, booking_thread, &a);
    pthread_create(&t2, NULL, booking_thread, &b);
    gate_open(&gate);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    gate_destroy(&gate);

    int wins = (a.status == ST_OK) + (b.status == ST_OK);
    printf("\nResult:\n  P104: %s (%s)\n  P109: %s (%s)\n",
           a.status == ST_OK ? "SUCCESS" : "REJECTED", status_text(a.status),
           b.status == ST_OK ? "SUCCESS" : "REJECTED", status_text(b.status));
    if (wins == 1)
        printf("VERIFIED: exactly one request succeeded - the slot mutex protects the\n"
               "BST check-and-delete critical section.\n");
    else
        printf("NOTE: %d request(s) succeeded (a patient may already hold a clashing appointment).\n", wins);
}


static void free_value(void *p) { free(p); }

static void free_schedule(void *p) {
    DaySchedule *s = (DaySchedule *)p;
    bst_free(s->root);
    pthread_mutex_destroy(&s->mutex);
    free(s);
}

static void seed_data(void) {
    hashmap_init(&app.patients, INITIAL_BUCKETS);
    hashmap_init(&app.doctors, INITIAL_BUCKETS);
    hashmap_init(&app.appointments, INITIAL_BUCKETS);
    hashmap_init(&app.schedules, INITIAL_BUCKETS);
    hashmap_init(&app.patient_slots, INITIAL_BUCKETS);
    pthread_mutex_init(&app.counter_lock, NULL);
    pthread_mutex_init(&app.schedule_create_lock, NULL);
    app.appointment_counter = 500;

    Doctor *docs[] = {
        create_doctor("D201", "Dr. A. Sharma", "Cardiology",      "Monday", "Wednesday", "Friday"),
        create_doctor("D202", "Dr. R. Verma",  "Dermatology",     "Tuesday", "Thursday", "Friday"),
        create_doctor("D203", "Dr. P. Mehta",  "Orthopedics",     "Monday", "Tuesday", "Thursday"),
        create_doctor("D204", "Dr. N. Singh",  "General Medicine","Wednesday", "Friday", "Saturday")
    };
    for (int i = 0; i < 4; i++) hashmap_put(&app.doctors, docs[i]->doctor_id, docs[i]);

    Patient *pts[] = {
        create_patient("P101", "Aarav Verma",  34, "+91-98110XXXXX", "aarav@example.com",  "Mild hypertension"),
        create_patient("P104", "Rohan Gupta",  29, "+91-98111XXXXX", "rohan@example.com",  "No known allergies"),
        create_patient("P106", "Sneha Rao",    25, "+91-98112XXXXX", "sneha@example.com",  "Migraine history"),
        create_patient("P109", "Meera Kapoor", 41, "+91-98113XXXXX", "meera@example.com",  "Routine consultation"),
        create_patient("P111", "Kavita Singh", 31, "+91-98114XXXXX", "kavita@example.com", "No known allergies")
    };
    for (int i = 0; i < 5; i++) hashmap_put(&app.patients, pts[i]->patient_id, pts[i]);
}

static void cleanup(void) {
    hashmap_destroy(&app.patients, free_value);
    hashmap_destroy(&app.doctors, free_value);
    hashmap_destroy(&app.appointments, appt_free);
    hashmap_destroy(&app.schedules, free_schedule);
    hashmap_destroy(&app.patient_slots, NULL);       
    pthread_mutex_destroy(&app.counter_lock);
    pthread_mutex_destroy(&app.schedule_create_lock);
}

static void print_menu(void) {
    printf("\n============================================================\n");
    printf("       HEALTHCARE APPOINTMENT SYSTEM - DAA PBL\n");
    printf("============================================================\n");
    printf(" 1. Register patient\n");
    printf(" 2. Search patient (Hash Map)\n");
    printf(" 3. Show patients\n");
    printf(" 4. Show doctors\n");
    printf(" 5. Show available slots (AVL/BST inorder)\n");
    printf(" 6. Book appointment\n");
    printf(" 7. Cancel appointment\n");
    printf(" 8. Reschedule appointment\n");
    printf(" 9. Show appointments\n");
    printf("10. Run double-booking concurrency demo\n");
    printf("11. Show one patient's appointments\n");
    printf("12. Find doctor's next available slot\n");
    printf(" 0. Exit\n");
    printf("============================================================\n");
}

static void register_patient_menu(void) {
    char in[INPUT_LEN], id[ID_LEN], name[MAX_NAME], phone[MAX_PHONE],
         email[MAX_EMAIL], info[MAX_INFO];
    int age;

    read_line("Patient ID: ", in, sizeof in);
    if (!norm_id(in, id, sizeof id)) {
        printf("ERROR: ID must be 1-15 letters/digits (e.g. P120).\n");
        return;
    }
    if (hashmap_get(&app.patients, id)) { printf("ERROR: Patient ID already exists.\n"); return; }

    if (!read_line("Name: ", name, sizeof name)) return;
    if (name[0] == '\0') { printf("ERROR: Name cannot be empty.\n"); return; }
    while (1) {
        if (!read_int("Age: ", &age)) return;
        if (age >= 0 && age <= 120) break;
        printf("Age must be between 0 and 120.\n");
    }
    read_line("Phone: ", phone, sizeof phone);
    read_line("Email: ", email, sizeof email);
    if (email[0] && !strchr(email, '@')) { printf("ERROR: Email looks invalid.\n"); return; }
    read_line("Medical information: ", info, sizeof info);

    Patient *p = create_patient(id, name, age, phone, email, info);
    hashmap_put(&app.patients, p->patient_id, p);
    printf("SUCCESS: Patient %s registered.\n", id);
}

static void book_menu(void) {
    char pid[INPUT_LEN], did[INPUT_LEN], date[INPUT_LEN], time[INPUT_LEN];
    read_line("Patient ID: ", pid, sizeof pid);
    read_line("Doctor ID: ", did, sizeof did);
    read_line("Date (YYYY-MM-DD): ", date, sizeof date);
    read_line("Time (e.g. 02:00 PM): ", time, sizeof time);
    if (input_closed) return;

    Appointment *a;
    Status st = book_appointment(pid, did, date, time, &a);
    if (st == ST_OK) { printf("\nSUCCESS: Appointment confirmed.\n"); print_appointment(a); }
    else printf("%s %s\n", st == ST_SLOT_TAKEN || st == ST_PATIENT_CLASH ? "REJECTED:" : "ERROR:", status_text(st));
}

static void cancel_menu(void) {
    char id[INPUT_LEN];
    read_line("Appointment ID: ", id, sizeof id);
    if (input_closed) return;
    Appointment *a;
    Status st = cancel_appointment(id, &a);
    if (st == ST_OK) printf("SUCCESS: %s cancelled; slot %s restored to BST.\n", a->appointment_id, a->time_label);
    else printf("ERROR: %s\n", status_text(st));
}

static void reschedule_menu(void) {
    char id[INPUT_LEN], date[INPUT_LEN], time[INPUT_LEN];
    read_line("Appointment ID: ", id, sizeof id);
    read_line("New date (YYYY-MM-DD): ", date, sizeof date);
    read_line("New time (e.g. 04:00 PM): ", time, sizeof time);
    if (input_closed) return;
    Appointment *a;
    Status st = reschedule_appointment(id, date, time, &a);
    if (st == ST_OK) printf("SUCCESS: %s rescheduled atomically to %s %s.\n", a->appointment_id, a->date, a->time_label);
    else if (st == ST_SLOT_TAKEN || st == ST_PATIENT_CLASH)
        printf("REJECTED: %s Original appointment preserved.\n", status_text(st));
    else printf("ERROR: %s\n", status_text(st));
}


static int checks_passed = 0, checks_failed = 0;

#define CHECK(cond, msg) do {                                             \
        if (cond) { checks_passed++; printf("  [PASS] %s\n", msg); }      \
        else      { checks_failed++; printf("  [FAIL] %s (line %d)\n", msg, __LINE__); } \
    } while (0)

static int free_slot_count(const char *did, const char *date) {
    DaySchedule *s = get_schedule(did, date, 1);
    pthread_mutex_lock(&s->mutex);
    int c = bst_count(s->root);
    pthread_mutex_unlock(&s->mutex);
    return c;
}

static void reset_state(void) { cleanup(); seed_data(); }

static void add_synthetic_patients(int n) {
    for (int i = 1; i <= n; i++) {
        char id[ID_LEN];
        snprintf(id, sizeof id, "T%02d", i);
        hashmap_put(&app.patients, id, create_patient(id, "Test Patient", 30, "-", "t@example.com", "-"));
    }
}

static int verify_invariants(const char *what) {
    Entry *sc, *ap;
    size_t ns = hashmap_snapshot(&app.schedules, &sc);
    size_t na = hashmap_snapshot(&app.appointments, &ap);
    int ok = 1;
    size_t confirmed_total = 0;

    for (size_t i = 0; i < na; i++)
        if (strcmp(((Appointment *)ap[i].value)->status, "CONFIRMED") == 0) confirmed_total++;

    for (size_t i = 0; i < ns; i++) {
        DaySchedule *s = (DaySchedule *)sc[i].value;
        int cnt = 0;
        if (bst_verify(s->root, -1, 100000, &cnt) < 0) { ok = 0; printf("    bad AVL in %s\n", sc[i].key); }
        size_t conf = 0;
        for (size_t j = 0; j < na; j++) {
            const Appointment *a = (Appointment *)ap[j].value;
            if (strcmp(a->doctor_id, s->doctor_id) == 0 && strcmp(a->date, s->date) == 0 &&
                strcmp(a->status, "CONFIRMED") == 0) conf++;
        }
        if ((size_t)cnt + conf != (size_t)OFFERED_COUNT) {
            ok = 0;
            printf("    slot leak in %s: free=%d confirmed=%zu\n", sc[i].key, cnt, conf);
        }
    }
    if (app.patient_slots.count != confirmed_total) {
        ok = 0;
        printf("    patient index has %zu entries, expected %zu\n", app.patient_slots.count, confirmed_total);
    }
    free(sc);
    free(ap);
    CHECK(ok, what);
    return ok;
}

typedef struct {
    StartGate *gate;
    int op;                     
    const char *id;
    Status status;
} CancelArg;

static void *cancel_thread(void *p) {
    CancelArg *c = (CancelArg *)p;
    gate_wait(c->gate);
    c->status = cancel_appointment(c->id, NULL);
    return NULL;
}

typedef struct {
    StartGate *gate;
    unsigned seed;
    int ops;
} ChaosArg;

static void *chaos_thread(void *p) {
    ChaosArg *c = (ChaosArg *)p;
    static const char *docs[]  = {"D201", "D202", "D203", "D204"};
    static const char *dates[] = {"2026-11-20", "2026-11-21", "2026-11-23",
                                  "2026-11-24", "2026-11-25", "2026-11-26"};
    gate_wait(c->gate);
    for (int i = 0; i < c->ops; i++) {
        char pid[ID_LEN], aid[ID_LEN], tm[16];
        snprintf(pid, sizeof pid, "T%02d", 1 + (int)(rand_r(&c->seed) % 40));
        snprintf(aid, sizeof aid, "APT%u", 501 + rand_r(&c->seed) % 150);
        minutes_to_label(OFFERED[rand_r(&c->seed) % OFFERED_COUNT], tm, sizeof tm);
        const char *doc  = docs[rand_r(&c->seed) % 4];
        const char *date = dates[rand_r(&c->seed) % 6];
        switch (rand_r(&c->seed) % 3) {
        case 0:  book_appointment(pid, doc, date, tm, NULL); break;
        case 1:  cancel_appointment(aid, NULL); break;
        default: reschedule_appointment(aid, date, tm, NULL); break;
        }
    }
    return NULL;
}

static int run_selftest(void) {
    printf("=== SELF-TEST ===\n");
    seed_data();
    g_race_amplifier = 1;   

    printf("\n[1] Date / time helpers\n");
    char d[11];
    CHECK(strcmp(weekday_from_date("2026-11-20"), "Friday") == 0,   "2026-11-20 is a Friday");
    CHECK(strcmp(weekday_from_date("2024-02-29"), "Thursday") == 0, "2024-02-29 is a Thursday");
    CHECK(strcmp(weekday_from_date("2000-01-01"), "Saturday") == 0, "2000-01-01 is a Saturday");
    CHECK(parse_date("2024-02-29", d) && !parse_date("2026-02-29", d), "leap-year handling");
    CHECK(!parse_date("2026-02-30", d) && !parse_date("2026-13-01", d) && !parse_date("garbage", d),
          "impossible dates rejected");
    CHECK(parse_date("2026-1-5", d) && strcmp(d, "2026-01-05") == 0, "date canonicalised to YYYY-MM-DD");
    CHECK(time_to_minutes("2:00pm") == 840 && time_to_minutes("12:00 AM") == 0 &&
          time_to_minutes("12:30 PM") == 750, "time parsing (incl. 12 AM / 12 PM)");
    CHECK(time_to_minutes("13:00 PM") < 0 && time_to_minutes("9:61 AM") < 0 &&
          time_to_minutes("9:00 AM junk") < 0, "bad times rejected");
    strcpy(d, "2026-12-31"); next_day(d);
    CHECK(strcmp(d, "2027-01-01") == 0, "next_day rolls over the year");

    printf("\n[2] AVL tree stays balanced\n");
    BSTNode *root = NULL;
    for (int i = 1; i <= 2000; i++) root = bst_insert(root, i, "x", 1);
    int cnt = 0, h = bst_verify(root, 0, 1000000, &cnt);
    CHECK(h > 0 && cnt == 2000 && h <= 15, "2000 sorted inserts -> valid AVL, height <= 15 (plain BST would be 2000)");
    for (int i = 1; i <= 2000; i += 2) root = bst_delete(root, i);
    cnt = 0; h = bst_verify(root, 0, 1000000, &cnt);
    CHECK(h > 0 && cnt == 1000, "1000 deletes keep tree valid and balanced");
    bst_free(root);

    printf("\n[3] Hash map\n");
    HashMap hm;
    hashmap_init(&hm, 3);
    char k[KEY_LEN];
    for (int i = 0; i < 5000; i++) { snprintf(k, sizeof k, "K%d", i); hashmap_put(&hm, k, (void *)(long)(i + 1)); }
    CHECK(hm.count == 5000 && hm.capacity > 3, "5000 inserts, map auto-rehashed");
    int found = 1;
    for (int i = 0; i < 5000; i++) { snprintf(k, sizeof k, "K%d", i); if (hashmap_get(&hm, k) != (void *)(long)(i + 1)) found = 0; }
    CHECK(found, "every key retrievable after rehash");
    CHECK(!hashmap_put_if_absent(&hm, "K7", NULL) && hashmap_remove(&hm, "K7") && !hashmap_get(&hm, "K7"),
          "put_if_absent / remove");
    hashmap_destroy(&hm, NULL);

    printf("\n[4] Booking rules\n");
    Appointment *a = NULL, *b = NULL;
    CHECK(book_appointment("p101", " d201 ", "2026-11-20", "2:00 PM", &a) == ST_OK, "valid booking (IDs/time normalised)");
    CHECK(a && strcmp(a->time_label, "02:00 PM") == 0 && strcmp(a->appointment_id, "APT501") == 0, "label normalised, ID APT501");
    CHECK(free_slot_count("D201", "2026-11-20") == 6, "booked slot removed from BST");
    CHECK(book_appointment("P104", "D201", "2026-11-20", "02:00 PM", NULL) == ST_SLOT_TAKEN, "same slot rejected");
    CHECK(book_appointment("P104", "D201", "2026-11-21", "02:00 PM", NULL) == ST_DOCTOR_OFF, "doctor off on Saturday");
    CHECK(book_appointment("P104", "D201", "2026-02-30", "02:00 PM", NULL) == ST_BAD_DATE, "impossible date rejected");
    CHECK(book_appointment("P104", "D201", "2026-11-20", "09:30 AM", NULL) == ST_NOT_OFFERED, "non-offered time rejected");
    CHECK(book_appointment("P104", "D201", "2026-11-20", "nonsense", NULL) == ST_BAD_TIME, "garbage time rejected");
    CHECK(book_appointment("P999", "D201", "2026-11-20", "10:00 AM", NULL) == ST_NO_PATIENT, "unknown patient");
    CHECK(book_appointment("P104", "D999", "2026-11-20", "10:00 AM", NULL) == ST_NO_DOCTOR, "unknown doctor");
    CHECK(free_slot_count("D201", "2026-11-20") == 6, "rejected requests do not touch the BST");
    CHECK(book_appointment("P101", "D202", "2026-11-20", "02:00 PM", NULL) == ST_PATIENT_CLASH, "patient cannot be in two places at once");
    CHECK(free_slot_count("D202", "2026-11-20") == 7, "clash rejection leaves other doctor's slot free");

    printf("\n[5] Cancel / reschedule\n");
    CHECK(cancel_appointment("apt501", NULL) == ST_OK && free_slot_count("D201", "2026-11-20") == 7, "cancel restores slot");
    CHECK(cancel_appointment("APT501", NULL) == ST_NOT_ACTIVE, "double cancel rejected");
    CHECK(cancel_appointment("APT999", NULL) == ST_NO_APPT, "unknown appointment");
    CHECK(book_appointment("P101", "D202", "2026-11-20", "02:00 PM", NULL) == ST_OK, "cancelled time can be re-booked (clash index cleared)");

    reset_state();
    CHECK(book_appointment("P101", "D201", "2026-11-20", "10:00 AM", &a) == ST_OK, "setup: P101 10:00");
    CHECK(book_appointment("P104", "D201", "2026-11-20", "11:00 AM", &b) == ST_OK, "setup: P104 11:00");
    CHECK(reschedule_appointment("APT501", "2026-11-20", "11:00 AM", NULL) == ST_SLOT_TAKEN, "reschedule into busy slot rejected");
    CHECK(strcmp(a->time_label, "10:00 AM") == 0 && free_slot_count("D201", "2026-11-20") == 5, "original appointment preserved");
    CHECK(reschedule_appointment("APT501", "2026-11-20", "04:00 PM", NULL) == ST_OK, "reschedule same day");
    CHECK(free_slot_count("D201", "2026-11-20") == 5 && strcmp(a->time_label, "04:00 PM") == 0, "old slot freed, new slot taken");
    CHECK(reschedule_appointment("APT501", "2026-11-25", "09:00 AM", NULL) == ST_OK, "reschedule to another date");
    CHECK(free_slot_count("D201", "2026-11-20") == 6 && free_slot_count("D201", "2026-11-25") == 6, "both days consistent");
    CHECK(reschedule_appointment("APT501", "2026-11-25", "09:00 AM", NULL) == ST_SAME_SLOT, "same-slot reschedule detected");
    CHECK(reschedule_appointment("APT501", "2026-11-21", "09:00 AM", NULL) == ST_DOCTOR_OFF, "reschedule to off-day rejected");
    verify_invariants("invariants hold after sequential operations");

    printf("\n[6] Concurrency (real threads)\n");
    reset_state();
    add_synthetic_patients(56);
    {  
        pthread_t th[56];
        BookingThreadArg args[56];
        char ids[56][ID_LEN], tms[7][16];
        StartGate g;
        gate_init(&g);
        for (int i = 0; i < 7; i++) minutes_to_label(OFFERED[i], tms[i], sizeof tms[i]);
        for (int i = 0; i < 56; i++) {
            snprintf(ids[i], sizeof ids[i], "T%02d", i + 1);
            args[i] = (BookingThreadArg){&g, ids[i], "D201", "2026-11-20", tms[i % 7], 0, ST_NOMEM};
            pthread_create(&th[i], NULL, booking_thread, &args[i]);
        }
        gate_open(&g);
        for (int i = 0; i < 56; i++) pthread_join(th[i], NULL);
        gate_destroy(&g);
        int wins = 0;
        for (int i = 0; i < 56; i++) wins += args[i].status == ST_OK;
        CHECK(wins == 7, "56 threads / 7 slots -> exactly 7 bookings, no double booking");
        CHECK(free_slot_count("D201", "2026-11-20") == 0, "all slots consumed exactly once");
        verify_invariants("invariants hold after booking storm");
    }
    {   
        pthread_t th[8];
        CancelArg ca[8];
        StartGate g;
        gate_init(&g);
        for (int i = 0; i < 8; i++) { ca[i] = (CancelArg){&g, 0, "APT501", ST_NOMEM}; pthread_create(&th[i], NULL, cancel_thread, &ca[i]); }
        gate_open(&g);
        int wins = 0;
        for (int i = 0; i < 8; i++) { pthread_join(th[i], NULL); wins += ca[i].status == ST_OK; }
        gate_destroy(&g);
        CHECK(wins == 1 && free_slot_count("D201", "2026-11-20") == 1, "8 concurrent cancels -> exactly one succeeds, slot restored once");
    }
    reset_state();
    {   
        pthread_t th[3];
        BookingThreadArg args[3];
        const char *docs[3] = {"D201", "D202", "D204"};
        StartGate g;
        gate_init(&g);
        for (int i = 0; i < 3; i++) {
            args[i] = (BookingThreadArg){&g, "P106", docs[i], "2026-11-20", "11:00 AM", 0, ST_NOMEM};
            pthread_create(&th[i], NULL, booking_thread, &args[i]);
        }
        gate_open(&g);
        int wins = 0;
        for (int i = 0; i < 3; i++) { pthread_join(th[i], NULL); wins += args[i].status == ST_OK; }
        gate_destroy(&g);
        CHECK(wins == 1, "same patient, 3 doctors, same time -> exactly one booking");
        CHECK(free_slot_count("D201", "2026-11-20") + free_slot_count("D202", "2026-11-20") +
              free_slot_count("D204", "2026-11-20") == 20, "losing requests leaked no slots");
    }
    reset_state();
    add_synthetic_patients(40);
    {   
        pthread_t th[12];
        ChaosArg ca[12];
        StartGate g;
        gate_init(&g);
        for (int i = 0; i < 12; i++) {
            ca[i] = (ChaosArg){&g, 1234u + (unsigned)i * 77u, 3000};
            pthread_create(&th[i], NULL, chaos_thread, &ca[i]);
        }
        gate_open(&g);
        for (int i = 0; i < 12; i++) pthread_join(th[i], NULL);
        gate_destroy(&g);
        verify_invariants("36,000 random concurrent ops: no leaked/duplicated slots, index consistent");
    }

    cleanup();
    printf("\n=== %d passed, %d failed ===\n", checks_passed, checks_failed);
    return checks_failed ? 1 : 0;
}


int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) return run_selftest();
    if (argc > 1) {
        printf("Usage: %s [--selftest]\n", argv[0]);
        return strcmp(argv[1], "--help") == 0 ? 0 : 1;
    }

    seed_data();
    printf("\nHealthcare Appointment System started.\n");
    printf("DAA concepts: AVL/BST + Separate-Chaining Hash Map + Mutex Scheduling.\n");

    while (!input_closed) {
        print_menu();
        int choice;
        if (!read_int("Enter choice: ", &choice)) break;

        switch (choice) {
        case 1:  register_patient_menu(); break;
        case 2:  search_patient(); break;
        case 3:  show_patients(); break;
        case 4:  show_doctors(); break;
        case 5:  show_slots(); break;
        case 6:  book_menu(); break;
        case 7:  cancel_menu(); break;
        case 8:  reschedule_menu(); break;
        case 9:  show_appointments(); break;
        case 10: run_double_booking_demo(); break;
        case 11: patient_appointments_menu(); break;
        case 12: next_available_menu(); break;
        case 0:  goto quit;
        default: printf("Invalid choice.\n");
        }
    }
quit:
    cleanup();
    printf("\nProgram closed.\n");
    return 0;
}
