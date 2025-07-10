#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <hiredis/hiredis.h>
#include <uthash.h>

#define CUSTOM_PROTOCOL 18
#define BUFFER_SIZE 65535
#define MAX_EVENTS_PER_WORKER 128
#define SESSION_TIMEOUT_SECONDS 60
#define AUTH_PREFIX "AUTH:"
#define AUTH_OK_PREFIX "AUTH_OK:"
#define MAX_SESSIONS_PER_WORKER 256

// --- Data Structures ---
typedef struct Session {
    uint64_t token; // The secret token is now the key
    int udp_sock;
    struct sockaddr_in client_addr; // Last known client address
    struct sockaddr_in game_server_addr;
    time_t last_seen;
    UT_hash_handle hh;
    struct Session* next_free;
} Session;

typedef struct {
    Session* free_list_head;
    Session sessions[MAX_SESSIONS_PER_WORKER];
} SessionPool;

typedef struct {
    int thread_id;
    int epoll_fd;
    int raw_sock;
    redisContext* redis_ctx;
    Session* active_sessions_hash;
    SessionPool session_pool;
} WorkerData;

// --- Memory Pool Functions (same as before) ---
void pool_init(SessionPool* pool) { /* ... */ }
Session* pool_get(SessionPool* pool) { /* ... */ }
void pool_release(SessionPool* pool, Session* session) { /* ... */ }

// --- Forward Declarations ---
void* worker_thread_main(void* arg);
void handle_auth_request(WorkerData* data, const char* buffer, const struct sockaddr_in* client_addr);
void handle_client_packet(WorkerData* data, const char* buffer, int len, const struct sockaddr_in* client_addr);

// --- Main Entry Point (same as before) ---
int main() { /* ... */ }

// --- Packet Handlers (Updated Logic) ---

// Handles the initial AUTH:<uuid> packet from a client
void handle_auth_request(WorkerData* data, const char* buffer, const struct sockaddr_in* client_addr) {
    char uuid[128];
    sscanf(buffer, "AUTH:%s", uuid);

        // The key in Redis now includes a prefix, e.g., "user:<uuid>"
    redisReply *reply = redisCommand(data->redis_ctx, "EXISTS user:%s", uuid);
    if (reply == NULL || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return; // Invalid or expired UUID
    }
    freeReplyObject(reply);

    // --- UUID is valid, create a new session and token ---
    Session* session = pool_get(&data->session_pool);
    if (!session) return; // Pool empty

    // Generate a secure random token
    uint64_t token;
    arc4random_buf(&token, sizeof(token));
    if (token == 0) token = 1; // Ensure token is never 0

    session->token = token;
    session->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (session->udp_sock < 0) {
        pool_release(&data->session_pool, session);
        return;
    }

    memcpy(&session->client_addr, client_addr, sizeof(struct sockaddr_in));
    session->last_seen = time(NULL);

    HASH_ADD_INT(data->active_sessions_hash, token, session);

    struct epoll_event event = {0};
    event.events = EPOLLIN;
    event.data.ptr = session;
    epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, session->udp_sock, &event);

    // --- Send AUTH_OK:<token> back to the client ---
    char response_buffer[128];
    int response_len = sprintf(response_buffer, "%s%llu", AUTH_OK_PREFIX, (unsigned long long)token);
    
    // We send the reply via the raw socket. The client will capture it.
    // A more robust design might use a dedicated port for auth, but this works.
    sendto(data->raw_sock, response_buffer, response_len, 0, (struct sockaddr*)client_addr, sizeof(struct sockaddr_in));
    
    printf("[Thread %d] Authenticated client %s, granted token %llu\n", data->thread_id, inet_ntoa(client_addr->sin_addr), (unsigned long long)token);
}

// Handles subsequent game packets that contain a token
void handle_client_packet(WorkerData* data, const char* buffer, int len, const struct sockaddr_in* client_addr) {
    if (len < sizeof(uint64_t)) return; // Packet is too small to contain a token

    uint64_t token = *(uint64_t*)buffer;
    Session* session = NULL;
    HASH_FIND_INT(data->active_sessions_hash, &token, session);

    if (!session) return; // Invalid token, drop packet

    // --- CRITICAL: Handle IP Address Change ---
    if (session->client_addr.sin_addr.s_addr != client_addr->sin_addr.s_addr || 
        session->client_addr.sin_port != client_addr->sin_port) {
        printf("[Thread %d] Client IP changed for token %llu. Updating %s:%d -> %s:%d\n", 
            data->thread_id, (unsigned long long)token, 
            inet_ntoa(session->client_addr.sin_addr), ntohs(session->client_addr.sin_port),
            inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
        memcpy(&session->client_addr, client_addr, sizeof(struct sockaddr_in));
    }

    session->last_seen = time(NULL);

    // The rest of the payload is the original game packet
    const char* inner_packet = buffer + sizeof(uint64_t);
    int inner_len = len - sizeof(uint64_t);

    struct iphdr* inner_ip_header = (struct iphdr*)inner_packet;
    struct udphdr* inner_udp_header = (struct udphdr*)((char*)inner_ip_header + (inner_ip_header->ihl * 4));

    // Update game server address if it's the first packet
    if (session->game_server_addr.sin_port == 0) {
        session->game_server_addr.sin_family = AF_INET;
        session->game_server_addr.sin_addr.s_addr = inner_ip_header->daddr;
        session->game_server_addr.sin_port = inner_udp_header->dest;
    }

    char* game_data = (char*)inner_udp_header + sizeof(struct udphdr);
    int game_data_len = ntohs(inner_udp_header->len) - sizeof(struct udphdr);

    sendto(session->udp_sock, game_data, game_data_len, 0, (struct sockaddr*)&session->game_server_addr, sizeof(session->game_server_addr));
}

// --- Worker Thread Main Logic (Updated) ---
void* worker_thread_main(void* arg) {
    // ... (setup is the same as before) ...

    while (1) {
        int n_events = epoll_wait(data->epoll_fd, events, MAX_EVENTS_PER_WORKER, 1000);
        for (int i = 0; i < n_events; i++) {
            if (events[i].data.ptr == NULL) { // Packet from raw socket
                char buffer[BUFFER_SIZE];
                struct sockaddr_in addr; socklen_t addr_len = sizeof(addr);
                int len = recvfrom(data->raw_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&addr, &addr_len);
                if (len > 0) {
                    // Check if it's an auth packet or a game packet
                    if (len > 5 && strncmp(buffer, AUTH_PREFIX, 5) == 0) {
                        handle_auth_request(data, buffer, &addr);
                    } else {
                        // It must be a game packet containing a token
                        handle_client_packet(data, buffer, len, &addr);
                    }
                }
            } else { // Packet from a game server UDP socket
                // ... (this logic is the same as before, no changes needed) ...
            }
        }
        // ... (cleanup logic is the same as before) ...
    }
    return NULL;
}

// Dummy implementations for the functions not fully shown, to make it a single block
void pool_init(SessionPool* pool) { pool->free_list_head = NULL; for (int i = 0; i < MAX_SESSIONS_PER_WORKER; i++) { pool->sessions[i].next_free = pool->free_list_head; pool->free_list_head = &pool->sessions[i]; } }
Session* pool_get(SessionPool* pool) { if (!pool->free_list_head) return NULL; Session* s = pool->free_list_head; pool->free_list_head = s->next_free; return s; }
void pool_release(SessionPool* pool, Session* session) { session->next_free = pool->free_list_head; pool->free_list_head = session; }
int main() { int n = sysconf(_SC_NPROCESSORS_ONLN); pthread_t t[n]; for(int i=0;i<n;i++){ WorkerData* d = malloc(sizeof(WorkerData)); d->thread_id=i; pthread_create(&t[i],NULL,worker_thread_main,d); } for(int i=0;i<n;i++){ pthread_join(t[i],NULL); } return 0; }