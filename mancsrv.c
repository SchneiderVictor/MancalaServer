#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */

int port = 57773; // port to listen on
int listenfd; // file descriptor to listen to the connection of new players

// player data struct
struct player {
    int fd; // file descriptor to read/write onto
    char name[MAXNAME+1];
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits 
                        // pits[NPITS] is the end pit
    int points;
    struct player *next;
};

struct player *playerlist = NULL; // list of all active/valid players
struct player *templist = NULL; // list of all connected, yet incomplete players

extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s);  /* you need to write this one */

/*
 * Error-checking wrapper function for malloc
 *
 * returns the new pointer returned by malloc
 */
void *Malloc(size_t size) {
    void *return_value;

    if ((return_value = malloc(size)) == NULL) {
        perror("malloc");
        exit(1);
    }

    return return_value;
}

/*
 * Error-checking wrapper function for accept
 *
 * returns the new file descriptor returned by accept
 * to read/write on
 */
int Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int return_value;

    if ((return_value = accept(sockfd, addr, addrlen)) < 0) {
        perror("accept");
        close(sockfd);
        exit(1);
    }

    return return_value;
}

/*
 * Error-checking wrapper function for close
 */
void Close(int fd) {
    if (close(fd) == -1) {
        perror("close");
        exit(1);
    }
}

/*
 * Error-checking wrapper function for read
 *
 * returns the amount of bytes read
 */
ssize_t Read(int fd, void *buf, size_t count) {
    ssize_t return_value;
    
    if ((return_value = read(fd, buf, count)) == -1) {
        perror("read");
        Close(fd);
        exit(1);
    }

    return return_value;
}

/*
 * Error-checking wrapper function for write
 *
 * returns the amount of bytes written
 */
int Write(int fd, const void *buf, size_t count) {
    int write_return;
    
    if ((write_return = write(fd, buf, count)) != count) {
        perror("write");
        Close(fd);
        exit(1);
    }

    return write_return;
}

/*
 * Error-checking wrapper function for select
 */
void Select(int n_fds, fd_set *r_fds, fd_set *w_fds, fd_set *e_fds, struct timeval *timeout) {
    if (select (n_fds, r_fds, w_fds, e_fds, timeout) == -1) {
        perror("select");
        exit(1);
    }
}

/*
 * returns the last player in the list pointed by **list
 *
 * if the list is empty/NULL, returns NULL
 */
struct player *get_newest_player(struct player **list) {
    struct player *newest_player = *list;
    
    if (newest_player != NULL) {
        while (newest_player->next != NULL) {
            newest_player = newest_player->next;
        }
    }

    return newest_player;
}

/*
 * checks to see if the input name already exists within playerlist.
 * If so, returns 1, otherwise 0;
 */
int name_valid(char *name) {
    int name_len = strlen(name);
    int player_name_len;
    int longer_str;
    
    if (strlen(name) == 0) {
        return 0;
    }
    
    for (struct player *p = playerlist; p; p = p->next) {
        player_name_len = strlen(p->name);
        
        // use the longer string to compare
        if (name_len > player_name_len) {
            longer_str = name_len;
        } else {
            longer_str = player_name_len;
        }

        if (strncmp(p->name, name, longer_str) == 0) {
            return 0;
        }
    }
    
    return 1;
}

/*
 * displays the state of all the game boards to all players in playerlist
 */
void show_boards() {
    char buffer[MAXMESSAGE + 1];
    char temp[MAXMESSAGE + 1];

    printf("Displaying boards to players\n");

    for (struct player *p = playerlist; p; p = p->next) {
        // reset buffer and temp everytime as a security feature
        memset(buffer, '\0', MAXMESSAGE + 1);

        for (int i = 0; i <= NPITS; i++) {
            memset(temp, '\0', MAXMESSAGE + 1);
            if (i < NPITS) {
                if (i == 0) {
                    sprintf(temp, "%s: [0]%d ", p->name, p->pits[0]);
                } else {
                    sprintf(temp, "[%d]%d ", i, p->pits[i]);
                }
            } else {
                sprintf(temp, "[end pit]%d\r\n", p->pits[i]);
            }
            strcat(buffer, temp);
        }
        
        broadcast(buffer);
    }
}

/*
 * removes a player in list and replaces them with the next player.
 * If the player leaving is the only one, then playerlist is set to NULL
 * 
 * Uses pointers to pointers to make sure old_playe and list are kept updated
 */
void remove_player(struct player **old_player, struct player **list) {
    char msg[MAXMESSAGE + 1];
    int old_fd = (*old_player)->fd;
    char *old_name = (*old_player)->name;
    struct player *free_value = *old_player;
    
    // only close here if list == &playerlist since timing of fd use differs
    // in both cases
    if (list == &playerlist) {
        Close(old_fd);

        memset(msg, '\0', MAXMESSAGE + 1);
    }

    // if old_player is the first player...
    if ((*list)->fd == old_fd) {
        *list = (*list)->next;
        *old_player = (*list);
    } else {
        for (struct player **p = list; *p; p = &((*p)->next)) {
            // once the player before old_player is found...
            if ((*p)->next->fd == old_fd) { 
                (*p)->next = (*p)->next->next;
                
                // if the player that disconnected was the last one in playerlist...
                if ((*p)->next == NULL) {
                    *old_player = *list;
                } else {
                    *old_player = (*p)->next;
                }
                
                break;
            }
        }
    }
    
    // when an incomplete player disconnects, active players do not need to be notified    
    if (list == &playerlist) {
        printf("%s has left the game.\n", old_name);
    
        if (playerlist != NULL) {
            if (playerlist->next != NULL) {
                sprintf(msg, "%s has left the game.\r\n", old_name);
            } else {
                sprintf(msg, "%s has left the game. Waiting for more players...\r\n", old_name);
            }
        
            broadcast(msg);
            show_boards();
        } else {
            printf("All players have left. Waiting for more players...\n");
        }
    }

    // free at the end so that player names can still be printed
    free(free_value);    
}

/*
 * adds a dynamic memory-allocated struct player to the end of list
 */
void add_new_player(int fd, char *name, struct player **list) {
    int num_pebbles = compute_average_pebbles();
    struct player *new_player = Malloc(sizeof(struct player));
    struct player *last_player = get_newest_player(list);
    
    new_player->fd = fd;
    memset(new_player->name, '\0', MAXNAME + 1);
    strncpy(new_player->name, name, MAXNAME);
    new_player->next = NULL;
    
    for (int i = 0; i < 6; i++) {
        new_player->pits[i] = num_pebbles;
    }
    
    // if this is the first player for list...
    if (last_player == NULL) {
        *list = new_player;
    } else {
        last_player->next = new_player;
    }
}
/*
 * removes newline characters and null-terminates the string
 *
 * does so regardless of the format of the newline
 *
 * returns 1 if the string was properly null-terminated,
 * returns 0 otherwise (input was incomplete)
 */
int null_terminate(char *string, int size) {
    for (int i = 0; i < size; i++) {
        if (string[i] == '\r' || string[i] == '\n') {
            string[i] = '\0';
            return 1;
        }
    }

    return 0;
}

/*
 * reads input from a player and null terminates it 
 */
int read_input(int player_fd, char *input, int max_input) {
    int read_return = Read(player_fd, input, max_input);

    null_terminate(input, max_input);

    return read_return;
}

/*
 * reads name from a player and null-terminates it
 * 
 * returns 1 if a complete name was received, 0 if not,
 * and -1 if the player disconnected (before completing name)
 */
int read_name(int player_fd, char *player_name, fd_set *all_fds) {
    char partial_name[MAXNAME + 1];
    char msg[MAXMESSAGE + 1];
    int remaining_space = MAXNAME - strlen(player_name);
    int return_value = 0;

    memset(partial_name, '\0', MAXNAME + 1);

    // if the player disconnects...
    if (Read(player_fd, partial_name, MAXNAME) == 0) {
        Close(player_fd);
        FD_CLR(player_fd, all_fds);
        
        return -1;
    }

    return_value = null_terminate(partial_name, MAXNAME);
    
    strncat(player_name, partial_name, remaining_space);

    // if the name is complete and the name is invalid
    if (return_value == 1 && !name_valid(player_name)) {
        memset(msg, '\0', MAXNAME + 1);
        sprintf(msg, "That name is already invalid. Must not be blank and must not match any other\r\n");


        printf("Player input an invalid name: %s. Prompting for a new name\n", player_name);
        Write(player_fd, msg, strlen(msg)); 
        
        memset(player_name, '\0', MAXNAME);
        return_value = 0;
    }

    return return_value;
}

/*
 * Writes a message to the indicated player.
 * 
 * If the player has disconnected, the player is removed
 */
void notify_player(struct player **player, char *msg, int size) {
    char buffer[size + 1];

    memset(buffer, '\0', size + 1);
    strncpy(buffer, msg, size);

    if (Write((*player)->fd, buffer, strlen(msg)) != strlen(msg)) {
        remove_player(player, &playerlist);
    }
}

/*
 * notifies all players except the indicated player
 * 
 * uses a pointer to a pointer to accommodate other functions
 */
void notify_all_other_players(struct player **excluded_player, char *msg, int size) {
    for (struct player *p = playerlist; p; p = p->next) {
        if (p->fd != (*excluded_player)->fd) {
            notify_player(&p, msg, size);
        }
    }
}

/*
 * updates all players' counts of points
 */
void update_points() {
    int points;
    
    for (struct player *p = playerlist; p; p = p->next) {
        points = 0;

        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        
        p->points = points;
    }

}

/*
 * makes the necessary adjustments to game boards/struct players
 * based on the input move and returns 1 if the player
 * has earned an extra move, 0 if not and -1 if the move was invalid.
 * 
 * if the input move is invalid, the player is notified of
 * such and prompted to try again
 */
int make_move(struct player **cur_player, char *input) {
    int move = strtol(input, NULL, 10);
    int pebbles = (*cur_player)->pits[move];
    struct player *modded_player = *cur_player;
 
    if (pebbles == 0 || move >= NPITS || move < 0) {
        printf("Player input an invalid move: %d. Prompting for new move\n", move);
        notify_player(cur_player, "That move is invalid. Please input the index to a non-end pit (pit must have 1+ pebbles)\r\n", MAXMESSAGE);
        
        return -1;
    }

    // selected pit is emptied
    modded_player->pits[move] = 0;
    
    while (pebbles > 0) {
        move++;

        // if we are modifying the current player and we have not traversed
        // past their end pit OR we are modifying any other player and
        // we have not reached their end pit...
        if ((modded_player == *cur_player && move <= NPITS) ||
                (modded_player != *cur_player && move < NPITS)) {
            modded_player->pits[move] += 1;
        } else {
            // ...else switch players to modify and empty their first non-end pit
            if (modded_player->next != NULL) {
                modded_player = modded_player->next;
            } else {
                modded_player = playerlist;
            }
            move = 0;
            modded_player->pits[move] += 1;
        }
        pebbles--;
    }
    
    // extra_m (extra_move) is updated to 1 (indicating extra move earned)
    // if the last pebble was placed in the current player's end pit
    if (modded_player == *cur_player && move == NPITS) {
        return 1;
    }
    return 0;
}

/*
 * handles the scenario when a new player connects.
 * 
 * prompts the player for a name and adds them to the end of templist
 *
 * this player is not added to playerlist ("activated") until a complete name is received
 */
void handle_player_creation(int *max_fd, fd_set *all_fds) {
    int new_player_fd = Accept(listenfd, NULL, NULL);
    char *name = Malloc(MAXNAME + 1);
    // temp is used since a struct player pointer is required to notify connected
    // players (simpler implementation)
    struct player *temp = malloc(sizeof(struct player));
    
    memset(name, '\0', MAXNAME + 1);
    temp->fd = new_player_fd;
    
    // update the max_fd to be used by FD_ISSET
    if (new_player_fd > *max_fd) {
        *max_fd = new_player_fd;
    }

    printf("New player connected. Prompting for name\n");
    notify_player(&temp, "Welcome to Mancala. What is your name?\r\n", MAXMESSAGE);
    
    add_new_player(new_player_fd, name, &templist);
    
    FD_SET(new_player_fd, all_fds);

    free(temp);
}

/*
 * handles the case when the current player does some interaction
 * (enters a move or disconnects)
 * 
 * returns 0 if the current player != NULL, returns 1 otherwise and
 * returns -1 if extra_m == -1 (indicating an invalid input move)
 */
int handle_current_player(struct player **cur_player, fd_set *all_fds, int *prompted, int *next_p, int *extra_m) {
    int clear_value;
    int player_fd = (*cur_player)->fd;
    char input[MAXMESSAGE + 1];
    char msg[MAXMESSAGE + 1];
    
    // check to see if the player disconnected
    if (read_input(player_fd, input, MAXMESSAGE) == 0) {
        clear_value = player_fd;
        remove_player(cur_player, &playerlist);
        FD_CLR(clear_value, all_fds);
        
        // set to indicate that the (new) current player was not prompted,
        // the current player does not need to be switched and the
        // the current player did not earn a new move
        *prompted = 0;
        *next_p = 0;
        *extra_m = 0;
                      
        if (cur_player == NULL) {
            return 1;
        } else {
            return 0;
        }
    } else if (playerlist->next == NULL) {
        // if cur_player is the only player...
        notify_player(cur_player, "Waiting for more players...\r\n", MAXMESSAGE);
        return 0;
    }
    
    // if the input move is invalid...
    if ((*extra_m = make_move(cur_player, input)) == -1) {
        return -1;
    }

    // indicate it is the next player's turn
    *next_p = 1;

    printf("%s made a move: %s\n", (*cur_player)->name, input); 
    memset(msg, '\0', MAXMESSAGE + 1);
    sprintf(msg, "%s made a move: %s\r\n", (*cur_player)->name, input);
    notify_all_other_players(cur_player, msg, MAXMESSAGE);
                         
    return 0;
}

/*
 * handles the case when a player other than the current player does some interaction
 * 
 * returns 1 if opter_p == NULL; returns 0 otherwise
 */
int handle_other_players(struct player **other_p, fd_set *all_fds, int *prompted) {
    int player_fd = (*other_p)->fd;
    char input[MAXMESSAGE + 1];
    
    // check to see if the player disconnected
    if (read_input(player_fd, input, MAXMESSAGE) == 0) {
        remove_player(other_p, &playerlist);
        FD_CLR(player_fd, all_fds);
        
        *prompted = 0;
                    
        if (*other_p == NULL) {
            return 1;
        }
        return 0;
    }

    printf("%s played out of turn. Advising them to wait their turn\r\n", (*other_p)->name);
    notify_player(other_p, "Please wait your turn\r\n", MAXMESSAGE);

    return 0;
}

/*
 * handles the scenario where an incomplete ("temp") player interacts with the game
 *
 * handles the completion of their name or disconnection
 */
int handle_temp_player(struct player **temp, struct player **cur_player, int *prompted, fd_set *all_fds) {
    int read_name_val;
    
    // if they complete their name...
    if ((read_name_val = read_name((*temp)->fd, (*temp)->name, all_fds)) > 0) {
        printf("%s has joined the game\n", (*temp)->name);
        broadcast("New player joined!\r\n");
                    
        add_new_player((*temp)->fd, (*temp)->name, &playerlist); 
        remove_player(temp, &templist);
 
        if (*cur_player == NULL) {
            *cur_player = playerlist;
        }

        show_boards();

        *prompted = 0;
                    
        return 1;
    } else if (read_name_val == 0) {
        // ...else if the input does not complete the name
        printf("Pending rest of name...\n");
        
        if (strlen((*temp)->name) > 0) {
            notify_player(temp, "Pending rest of name...\r\n", MAXMESSAGE);
        }

        return 0;
    } else {
        printf("Player disconnected without entering full name. Could not be created\n");

        remove_player(temp, &templist);
        
        return 1;
    }
}

/*
 * returns 1 if there is a valid number of "active" players (in playerlist).
 * returns 0 otherwise
 */
int have_valid_num_players() {
    if (playerlist != NULL && playerlist->next != NULL) {
        return 1;
    }
    return 0;
}

/*
 * handles switching to the next player
 */
void handle_switch_player(struct player **cur_player, int extra_m, int *next_p, int *prompted) {
    // current_player is not changed if extra_move is true (== 1)
    if (!extra_m) {
        if ((*cur_player)->next != NULL) {
            *cur_player = (*cur_player)->next;
        } else {
            *cur_player = playerlist;
        }
    }

    // set that cur_player no longer needs to be switched and that
    // the next player was not prompted for their move
    *next_p = 0;
    *prompted = 0;
            
    show_boards();
}

void handle_next_prompt(struct player **cur_player, int extra_m, int *prompted) {
    char msg[MAXMESSAGE + 1];

    if (extra_m) {
	    printf("%s has earned another move.\n", (*cur_player)->name);
        notify_player(cur_player, "You earned an extra turn! Please input your move.\r\n", MAXMESSAGE);
                
        memset(msg, '\0', MAXMESSAGE + 1);
        sprintf(msg, "%s earned another turn!\r\n", (*cur_player)->name);
        notify_all_other_players(cur_player, msg, MAXMESSAGE);
    } else {
        notify_player(cur_player, "Your turn. Please input your move.\r\n", MAXMESSAGE);
    }

    printf("Prompting %s to make their move.\n", (*cur_player)->name);

    *prompted = 1;
}

void end_game() {
    char msg[MAXMESSAGE + 1];
    
    printf("Game over!\n");
    broadcast("Game over!\r\n");
    
    for (struct player *p = playerlist; p; p = p->next) {
        memset(msg, '\0', MAXMESSAGE + 1);

        printf("%s has %d points\r\n", p->name, p->points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, p->points);
        broadcast(msg);
    }
}

int main(int argc, char **argv) {
    int max_fd, extra_move;
    int next_player = 0;
    int prompted_next_player = 0;
    struct player *current_player = NULL;
    fd_set all_fds;
    
    // prepare server for listening on the correct port (as per cmd line arguments) 
    parseargs(argc, argv);
    makelistener();

    // set up the fd_set of all players + the listening fd
    max_fd = listenfd;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);

    printf("Mancala server started. Waiting for players...\n");

    while (!game_is_over()) {
        // reset dynamic_fds every loop since it will be modified by Select
        fd_set dynamic_fds = all_fds;
        extra_move = 0;
        
        Select(max_fd + 1, &dynamic_fds, NULL, NULL, NULL);
        
        // if a new player connects...
        if (FD_ISSET(listenfd, &dynamic_fds)) {
            handle_player_creation(&max_fd, &all_fds);
            
            if (!have_valid_num_players()) {
                printf("Waiting for more players...\n");
                continue;
            }
        }
        
        // loop over every "active" player in playerlist,
        // only stopping on players that interacted with the game
        // break the loop when handle_current_player() || handle_other_players() != 0
        for (struct player *p = playerlist; p; p = p->next) {
            if (p->fd == current_player->fd && FD_ISSET(p->fd, &dynamic_fds)) {
                if (handle_current_player(&current_player, &all_fds, &prompted_next_player, &next_player, &extra_move)) {
                    break;
                }
            } else if (FD_ISSET(p->fd, &dynamic_fds)) {
                if (handle_other_players(&p, &all_fds, &prompted_next_player)) {
                    break;
                }
            }
        }
        
        // if the input move was invalid, we should return to Select()
        if (extra_move == -1) {
            continue;
        }

        // loop over every player in templist,
        // only stopping on players that interacted with the game
        // break the loop if handle_temp_player() != 0
        for (struct player *t = templist; t; t = t->next) {
            if (FD_ISSET(t->fd, &dynamic_fds)) {
                if (handle_temp_player(&t, &current_player, &prompted_next_player, &all_fds)) {
                    break;
                }
            }
        }
        
        // should return to Select if we do not have enough "active" players
        if (!have_valid_num_players()) {
            continue;
        }

        update_points();

        if (next_player) {
            handle_switch_player(&current_player, extra_move, &next_player, &prompted_next_player);
        }

        // only make the preperatory prompt when no other prompts were created
        // and when there is a valid number of players
        if (!prompted_next_player && have_valid_num_players()) { 
            handle_next_prompt(&current_player, extra_move, &prompted_next_player);
        }
    }
    
    end_game();

    return 0;
}

/*
 * parses the command-line arguments and error checks them
 */
void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);  
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}

/*
 * prepares the indicated port to be listened to for new connections
 * and error checks
 */
void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}



/* 
 * calculates and returns the average number of pebbles in all "active" players'
 * non-end pits
 *
 * called BEFORE linking the new player in to the playerlist
 */
int compute_average_pebbles() { 
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (struct player *p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}

/*
 * returns 1 if any player's non-end pits are all empty;
 * returns 0 otehrwise
 */
int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}

/*
 * "broadcasts" msg to all "active" players
 */
void broadcast(char *msg) {
    if (playerlist != NULL) {
        for (struct player *p = playerlist; p; p = p->next) {
            notify_player(&p, msg, strlen(msg));
        }
    }
}
