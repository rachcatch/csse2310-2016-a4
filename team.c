#include "shared.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <stdarg.h>

// All the things that could go wrong
enum ExitCodes {
    EXIT_ARGS = 1,
    EXIT_OPEN_SINISTER_FILE = 2,
    EXIT_SINISTER_FILE_CONTENTS = 3,
    EXIT_OPEN_TEAM_FILE = 4,
    EXIT_TEAM_FILE_CONTENTS = 5,
    EXIT_INVALID_PORT = 6,
    EXIT_CONNECT_CONTROLLER = 7,
    EXIT_CONNECT_TEAM = 8,
    EXIT_CONTROLLER_DISCO = 9,
    EXIT_TEAM_DISCO = 10,
    EXIT_BAD_MESSAGE = 19,
    EXIT_SYSTEM = 20
};

// Messages that can be received from another team
typedef enum TeamMessages {
    FIGHTMEIRL,
    HAVEATYOU,
    ISELECTYOU,
    ATTACK,
    END // Used for EOF
} TeamMsgs;

// Messages that can be received from the controller
typedef enum ControllerMessages {
    SINISTER,
    BATTLE,
    GAMEOVERMAN,
    WHERENOW
} ControllerMsgs;

/**
 * Adds the given narrative to game's array of narratives. Thread-safe.
 */
void add_narrative(Game *game, char *narrative) {
    sem_wait(&game->narrativeLock);
    game->numNarratives++;
    game->narratives = realloc(game->narratives, sizeof(char *) * 
            game->numNarratives);
    game->narratives[game->numNarratives - 1] = narrative;
    sem_post(&game->narrativeLock);
}

/**
 * Appends the format string to the narrative, replacing underscores with 
 *      spaces and increasing space for the narrative if necessary.
 */
void append_string(char **narrative, const char *format, ...) {
    int n, bufferSize = 40;
    char *segment = NULL;
    va_list args;
    // reallocate size for format string until args fit
    while (true) {
        segment = realloc(segment, sizeof(char) * bufferSize);
        va_start(args, format);
        n = vsnprintf(segment, bufferSize, format, args);
        va_end(args);
        if (n < bufferSize) {
            break; // formatted string fit into memory. All is well.
        }
        bufferSize = n + 1;
    }
    // append to narrative, replacing underscores with spaces
    for (int i = 0; i < strlen(segment); i++) {
        if (segment[i] == '_') {
            segment[i] = ' ';
        }
    }
    *narrative = realloc(*narrative, sizeof(char) * (strlen(*narrative) + 
            strlen(segment) + 1));
    strcat(*narrative, segment);
    free(segment);
}

/**
 * Returns the effectiveness of the attack against the opponent.
 * Assumes attack and opponent are valid.
 */
enum Effectiveness get_effectiveness(Attack *attack, Agent *opponent) {
    for (int i = 0; i < attack->type->numLower; i++) {
        if (strcmp(attack->type->lower[i]->name, opponent->type->name) == 0) {
            return LOW;
        }
    }
    for (int i = 0; i < attack->type->numHigher; i++) {
        if (strcmp(attack->type->higher[i]->name, opponent->type->name) == 0) {
            return HIGH;
        }
    }
    return NORMAL;
}

/**
 * Send member's attack on opponent to the write stream, and add to narrative.
 * Increments the member's attack
 */
void attack(char **narrative, Game *game, FILE *write, Member *member, 
        Member *opponent) {
    // message opposing team
    Attack *attack = member->nextAttack->attack;
    fprintf(write, "attack %s %s\n", member->agent->name, attack->name);
    fflush(write);

    // get effectiveness and update narrative
    int effectiveness = get_effectiveness(attack, opponent->agent);
    opponent->health -= effectiveness;
    append_string(narrative, "%s uses %s: %s", member->agent->name,
            attack->name, attack->type->effectiveness[effectiveness - 1]);
    if (opponent->health <= 0) {
        append_string(narrative, " - %s was eliminated.",
                opponent->agent->name);
    }
    append_string(narrative, "\n");
    // increment attack
    member->nextAttack = member->nextAttack->next;
}

/**
 * Exits the program with the given status and its corresponding error message
 */
void exit_game(int status) {
    char *message;
    switch (status) {
        case EXIT_ARGS:
            message = "Usage: 2310team controllerport teamfile\n   "
                    "or: 2310team wait teamfile sinisterfile\n   "
                    "or: 2310team challenge teamfile sinisterfile targetport";
            break;
        case EXIT_OPEN_TEAM_FILE:
            message = "Unable to access team file";
            break;
        case EXIT_TEAM_FILE_CONTENTS:
            message = "Error reading team file";
            break;
        case EXIT_OPEN_SINISTER_FILE:
            message = "Unable to access sinister file";
            break;
        case EXIT_SINISTER_FILE_CONTENTS:
            message = "Error reading sinister file";
            break;
        case EXIT_INVALID_PORT:
            message = "Invalid port number";
            break;
        case EXIT_CONNECT_CONTROLLER:
            message = "Unable to connect to controller";
            break;
        case EXIT_CONNECT_TEAM:
            message = "Unable to connect to team";
            break;
        case EXIT_CONTROLLER_DISCO:
            message = "Unexpected loss of controller";
            break;
        case EXIT_TEAM_DISCO:
            message = "Unexpected loss of team";
            break;
        case EXIT_BAD_MESSAGE:
            message = "Protocol error";
            break;
        case EXIT_SYSTEM:
            message = "System error";
            break;
        default:
            message = "Well, this is awkward.";
    }
    fprintf(stderr, "%s\n", message);
    exit(status);
}

/** 
 * Populates line with the next line read from file. 
 * length is line's initial size. line is reallocated space if needed.
 * Exits with controller disconnected or protocol error if invalid message.
 */
ControllerMsgs read_controller_msg(char *line, int length, FILE *file) {
    read_line(line, length, file);
    if (strlen(line) == 0) {
        exit_game(EXIT_CONTROLLER_DISCO);   
    }
    char *type = get_token(line, ' ');
    ControllerMsgs result = -1;
    if (strcmp(type, "sinister") == 0) {
        result = SINISTER;
    } else if (strcmp(type, "battle") == 0) {
        result = BATTLE;
    } else if (strcmp(type, "gameoverman") == 0) {
        result = GAMEOVERMAN;
    } else if (strcmp(type, "wherenow?") == 0) {
        result = WHERENOW;
    } else {
        exit_game(EXIT_BAD_MESSAGE);
    }
    free(type);
    return result;
}

/** 
 * Populates line with the next line read from file, up to the given length.
 * Exits with protocol error if invalid message.
 * Calling thread exits if EOF is found (Sends "disco" to controller first if
 *      we're in simulation mode)
 */
TeamMsgs read_team_msg(char *line, int length, FILE *file, Game *game) {
    read_line(line, length, file);
    if (strlen(line) == 0) {
        if (game->simulation) {
            fprintf(game->write, "disco\n"); // team disconnected in sim mode
            fflush(game->write);
            pthread_exit(0);
        } else {
            exit_game(EXIT_TEAM_DISCO); // team disconnected in 1v1 mode
        }
    }
    char *type = get_token(line, ' ');
    TeamMsgs result = -1;
    if (strcmp(type, "fightmeirl") == 0) {
        result = FIGHTMEIRL;
    } else if (strcmp(type, "haveatyou") == 0) {
        result = HAVEATYOU;
    } else if (strcmp(type, "iselectyou") == 0) {
        result = ISELECTYOU;
    } else if (strcmp(type, "attack") == 0) {
        result = ATTACK;
    } else {
        exit_game(EXIT_BAD_MESSAGE);
    }
    free(type);
    return result;
}

/**
 * Returns a team member whose agent is specified in an "iselectyou" message
 *      read from opposing->read.
 * Exits with protocol error if invalid message.
 */
Member *get_selected_opponent(Game *game, Team *opposing, char **narrative) {
    Member *opponent = malloc(sizeof(Member));
    opponent->health = MAX_HEALTH;

    // read iselectyou message
    char *message = malloc(sizeof(char) * BUFFER);
    if (read_team_msg(message, BUFFER, opposing->read, game) != ISELECTYOU) {
        exit_game(EXIT_BAD_MESSAGE); // not iselectyou
    }

    // get agent
    char *agentName = get_token(&message[strlen("iselectyou ")], '\0');
    free(message);
    if ((opponent->agent = get_agent(game, agentName)) == NULL) {
        exit_game(EXIT_BAD_MESSAGE);
    }
    free(agentName);

    // add to narrative
    append_string(narrative, "%s chooses %s\n", opposing->name,
            opponent->agent->name);
    return opponent;
}

/**
 * Sends a message to opposition that this team member has been selected.
 * Adds to narrative.
 * Returns a copy of the given team member with full health.
 */
Member *select_member(char **narrative, FILE *opposition, char *teamName, 
        Member *member) {
    Member *copy = malloc(sizeof(Member));
    copy->agent = member->agent;
    copy->health = MAX_HEALTH;
    copy->nextAttack = member->firstAttack;
    fprintf(opposition, "iselectyou %s\n", copy->agent->name);
    fflush(opposition);
    append_string(narrative, "%s chooses %s\n", teamName, member->agent->name);
    return copy;
}

/**
 * Reads and processes an attack from the opposing team.
 * Exits with protocol error if invalid information received.
 */
void get_attacked(Game *game, char **narrative, Member *member,
        Member *opponent, Team *opposing) {
    char *message = malloc(sizeof(char) * BUFFER);
    if (read_team_msg(message, BUFFER, opposing->read, game) != ATTACK) {
        exit_game(EXIT_BAD_MESSAGE); // attack message not received
    }

    // figure out what attack is being used on us
    char *agentName = get_token(&message[strlen("attack ")], ' ');
    Attack *attack = get_attack(game, &message[strlen("attack  ") +
            strlen(opponent->agent->name)]);
    if (attack == NULL || !legal_attack(opponent->agent, attack) ||
            strcmp(agentName, opponent->agent->name) != 0) {
        // invalid attack, invalid agent, or illegal attack for that agent
        exit_game(EXIT_BAD_MESSAGE);
    }
    free(agentName);

    // update our stats and add to narrative
    int effectiveness = get_effectiveness(attack, member->agent);
    member->health -= effectiveness;
    append_string(narrative, "%s uses %s: %s", opponent->agent->name, 
            attack->name, attack->type->effectiveness[effectiveness - 1]);
    if (member->health <= 0) {
        append_string(narrative, " - %s was eliminated.", member->agent->name);
    }
    append_string(narrative, "\n");
    free(message);
}

/**
 * Battles game->team and opposing team. goFirst should be true when game->team
 *     is to attack first, false otherwise. Battle story added to narrative.
 * Calling thread exits with protocol error if bad message found; or exits if
 *     opposing team disconnects (with status 0 in sim mode, 10 otherwise).
 */
void battle(char **narrative, Game *game, Team *opposing, bool goFirst) {
    char *msg = malloc(sizeof(char) * BUFFER);
    Team *loser = game->team;
    Member *opponent;
    if (!goFirst) {
        opponent = get_selected_opponent(game, opposing, narrative);
    }

    // i is the index of our team's agent, j is index of opposing agent
    for (int i = 0, j = 0; i < MAX_TEAM_PLAYERS && j < MAX_TEAM_PLAYERS; ++i) {
        Member *member = select_member(narrative, opposing->write, 
                game->team->name, game->team->members[i]);

        // first round only
        if (i == 0) {
            if (!goFirst) {
                get_attacked(game, narrative, member, opponent, opposing);
            } else {
                opponent = get_selected_opponent(game, opposing, narrative);
            }
        }

        // fight until our agent dies or whole opposing team dies
        while (member->health > 0) {
            attack(narrative, game, opposing->write, member, opponent);
            if (opponent->health <= 0) { 
                if (++j == MAX_TEAM_PLAYERS) {
                    loser = opposing;
                    break; 
                }
                free(opponent);
                opponent = get_selected_opponent(game, opposing, narrative);
            }
            get_attacked(game, narrative, member, opponent, opposing);
        }
        free(member);
    }

    free(msg);
    append_string(narrative, "Team %s was eliminated.\n", loser->name);
    add_narrative(game, *narrative);
}

/**
 * Goes through wait mode. Game narrative is added to game->narratives.
 * Calling thread exits if a protocol error or team disconnected error occurs.
 */
void be_challenged(Game *game, Team *opposing) {
    FILE *read = opposing->read;
    char *msg = malloc(sizeof(char) * BUFFER);
    char *narrative = malloc(sizeof(char));
    narrative[0] = '\0';

    // setup communication
    if (read_team_msg(msg, BUFFER, read, game) != FIGHTMEIRL) {
        exit_game(EXIT_BAD_MESSAGE); 
    }
    opposing->name = get_token(&msg[strlen("fightmeirl ")], 0);
    append_string(&narrative, "%s has a difference of opinion\n", opposing->name);
    fprintf(opposing->write, "haveatyou %s\n", game->team->name);
    fflush(opposing->write);

    battle(&narrative, game, opposing, false);
}

/**
 * For qsorting strings in lexicographical order
 */
int sort_strings(const void *a, const void *b) {
    return strcmp(*(char **)a, *(char **)b);
}

/**
 * Prints the game's narratives list in lexicographical order.
 * Frees narratives and resets numNarratives to 0.
 */
void print_and_free_narratives(Game *game) {
    qsort(game->narratives, game->numNarratives, sizeof(char *), sort_strings);
    for (int i = 0; i < game->numNarratives; i++) {
        printf("%s", game->narratives[i]);
        free(game->narratives[i]);
    }
    fflush(stdout);
    game->numNarratives = 0;
}

/**
 * Challenges the opposing team and adds the narrative to game->narratives upon
 *     completion.
 * Exits if a bad message is received or if opposing team disconnects.
 */
void challenge(Game *game, Team *opposing) {
    char *narrative = malloc(sizeof(char));
    narrative[0] = '\0';
    char *message = malloc(sizeof(char) * BUFFER);

    // set-up communication
    fprintf(opposing->write, "fightmeirl %s\n", game->team->name);
    fflush(opposing->write);
    if (read_team_msg(message, BUFFER, opposing->read, game) != HAVEATYOU) {
        exit_game(EXIT_BAD_MESSAGE);
    }
    opposing->name = get_token(&message[strlen("haveatyou ")], '\0');
    append_string(&narrative, "%s has a difference of opinion\n", 
            opposing->name);
    free(message);

    battle(&narrative, game, opposing, true);
}

/**
 * Runs wait mode, then either prints the resulting narrative or sends
 *     "donefighting" to the controller if in simulation mode. 
 * args should be a ThreadGame pointer.
 * Calling thread exits with NULL on success.
 * Exits if a bad message is received or if opposing team disconnects.
 */
void *wait_wrapper(void *args) {
    ThreadGame *params = (ThreadGame *)args;
    be_challenged(params->game, params->opposing);
    if (params->game->simulation) {
        fprintf(params->game->write, "donefighting\n");
        fflush(params->game->write);
    } else {
        print_and_free_narratives(params->game);
    }
    pthread_exit(NULL);
}

/**
 * Starts listening on a port, and prints port if necessary. Accepts one or
 *     multiple connections depending on if game is in simulation mode.
 * Runs through a battle with each connected team.
 * Exits if bad message received or if opposing team disconnects.
 * args should be a Game *.
 */
void *enter_wait_mode(void *args) {
    Game *game = (Game *)args;
    // start listening, and print port if necessary
    int fd = open_listen(&game->team->port);
    if (fd < 0) {
        exit_game(EXIT_SYSTEM);
    }
    if (!game->simulation) {
        printf("%d\n", game->team->port);
        fflush(stdout);
    }

    while (true) {
        ThreadGame *params = malloc(sizeof(ThreadGame));
        Team *opposing = malloc(sizeof(Team));
        // accept a connection and spawn another thread to battle
        if (accept_connection(fd, &opposing->read, &opposing->write) < 0) {
            exit_game(EXIT_CONNECT_TEAM);
        }
        params->opposing = opposing;
        params->game = game;
        pthread_t waiter;
        pthread_create(&waiter, NULL, wait_wrapper, (void *)params);
        pthread_detach(waiter);
        if (!game->simulation) {
            pthread_exit(0); // stop this thread if we're doing one battle only
        }
    }
}

/**
 * Connects to localhost on the given port. read and write will be set up to
 *     communicate over the resulting file descriptor.
 * Returns non-zero on error.
 */
int connect_to_port(int port, FILE **read, FILE **write) {
    struct sockaddr_in socketAddr;
    struct addrinfo *addressInfo;
    getaddrinfo("localhost", NULL, NULL, &addressInfo);
    struct in_addr *ipAddress = 
            &(((struct sockaddr_in *)(addressInfo->ai_addr))->sin_addr);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        exit_game(EXIT_SYSTEM);
    }

    // attempt connection
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = htons(port);
    socketAddr.sin_addr.s_addr = ipAddress->s_addr;
    if (connect(fd, (struct sockaddr *)&socketAddr, sizeof(socketAddr)) < 0) {
        return -1;
    }
    *read = fdopen(fd, "r");
    *write = fdopen(fd, "w");
    return fd;
}

/**
 * Starts a challenge on the given port.
 * Can exit with protocol error, invalid port, or team disconnected on error.
 */
void enter_challenge_mode(Game *game, int port) {
    // check port validity
    if (!valid_port(port)) {
        exit_game(EXIT_INVALID_PORT);
    }
    // set up connection to opposition
    Team *opposing = malloc(sizeof(Team));
    if (connect_to_port(port, &opposing->read, &opposing->write) < 0) {
        exit_game(EXIT_CONNECT_TEAM);
    }

    challenge(game, opposing);
}

/**
 * Starts a challenge thread on the specified port, then sends "donefighting"
 *     to the controller and exits the calling thread.
 * args is a ThreadGame *.
 * Can exit with protocol error, invalid port, or team disconnected on error.
 */
void *spawn_challenge_thread(void *args) {
    ThreadGame *params = (ThreadGame *)args;
    enter_challenge_mode(params->game, params->port);
    fprintf(params->game->write, "donefighting\n");
    fflush(params->game->write);
    pthread_exit(0);
}

/**
 * Populates member's attacks with attacks given in the line.
 * Attacks should be space-separated with no leading or trailing space.
 * Exits with invalid team file contents if attacks invalid.
 */
void read_team_attacks(char *line, Game *game, Member *member) {
    int pos = 0;
    if (pos >= strlen(line)) {
        exit_game(EXIT_TEAM_FILE_CONTENTS); // no attacks
    }

    // read attacks until we have reached the end of the line
    AttackNode *tail;
    while (pos < strlen(line)) {
        // get attack
        char *attackName = get_token_update_pos(line, ' ', &pos);
        Attack *attack = get_attack(game, attackName);
        free(attackName);
        if (attack == NULL) {
            exit_game(EXIT_TEAM_FILE_CONTENTS); // invalid attack
        } else if (!legal_attack(member->agent, attack)) {
            exit_game(EXIT_TEAM_FILE_CONTENTS); // not legal attack
        }

        // Create attack node and add it to member's list
        AttackNode *attackNode = malloc(sizeof(AttackNode));
        attackNode->attack = attack;
        if (member->firstAttack == NULL) {
            member->firstAttack = attackNode;
        } else {
            tail->next = attackNode;
        }
        tail = attackNode;
    }

    // make members attacks circularly linked
    tail->next = member->firstAttack;
    member->nextAttack = member->firstAttack;
}

/**
 * Reads the agents and their attacks from the given team file, and populates
 *      game struct with this information.
 * Exits with invalid team file contents if bad format or inconsistent with
 *      sinister file.
 */
void read_agents(FILE *file, Game *game) {
    for (int i = 0; i < MAX_TEAM_PLAYERS; i++) {
        char *line = malloc(sizeof(char) * BUFFER);
        read_line(line, BUFFER, file);
        if (line == NULL || strlen(line) == 0) {
            exit_game(EXIT_TEAM_FILE_CONTENTS); // empty line or EOF
        }

        // Set up agent
        char *agentName = get_token(line, ' ');
        Agent *agent = get_agent(game, agentName);
        free(agentName);
        if (agent == NULL) {
            exit_game(EXIT_TEAM_FILE_CONTENTS); // invalid agent
        }

        // add member and their attacks to team members
        Member *member = malloc(sizeof(Member));
        game->team->members[i] = member;
        member->agent = agent;
        member->firstAttack = NULL;
        int pos = strlen(agent->name) + 1;
        read_team_attacks(&line[pos], game, member);
        free(line);
    }
}

/**
 * Reads a line of directions from the given file, and adds these to team data.
 * Exits with invalid team file contents if invalid characters or format.
 */
void read_directions(FILE *file, Team *team) {
    int c;
    Direction *tail;
    while ((c = fgetc(file)) != EOF) {
        // look for letter
        if (c == 'N' || c == 'S' || c == 'E' || c == 'W') {
            Direction *d = malloc(sizeof(Direction));
            d->direction = c;
            d->next = NULL;
            if (team->nextMove == NULL) {
                team->nextMove = d;
            } else {
                tail->next = d;
            }
            tail = d;
        } else {
            exit_game(EXIT_TEAM_FILE_CONTENTS); // invalid char
        }

        // look for space
        c = fgetc(file);
        if (c != ' ' && c != EOF && c != '\n') {
            exit_game(EXIT_TEAM_FILE_CONTENTS); // unexpected char (not space)
        }
    }

    // make directions circularly linked
    tail->next = team->nextMove;
}

/**
 * Populates the given game struct with team info.
 */
void read_team_file(Game *game, char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        exit_game(EXIT_OPEN_TEAM_FILE); // cannot open file
    }

    // read teamname, agents, attacks
    char *name = malloc(sizeof(char) * BUFFER);
    read_line(name, BUFFER, file);
    if (name == NULL || strlen(name) == 0) {
        exit_game(EXIT_TEAM_FILE_CONTENTS);
    }
    game->team = new_team(name);
    read_agents(file, game);

    // get coords
    char coords[BUFFER];
    if (fgets(coords, BUFFER, file) == NULL) {
        exit_game(EXIT_TEAM_FILE_CONTENTS); // unexpected EOF
    }
    int pos = 0;
    game->team->pos = get_coords(coords, '\n', &pos);
    if (game->team->pos->x < 0 || game->team->pos->y < 0) {
        exit_game(EXIT_TEAM_FILE_CONTENTS); // invalid coords
    }

    // get directions
    read_directions(file, game->team);
    if (fgetc(file) != EOF) { 
        exit_game(EXIT_TEAM_FILE_CONTENTS); // too much file
    }
}

/**
 * Populates game with the data from the given sinister and team files.
 * Exits with Sinister or Team file errors if invalid data found.
 */
void parse_game_files(Game *game, FILE *sinister, char *teamFilename) {
    if (read_sinister_file(game, sinister) != 0) {
        exit_game(EXIT_SINISTER_FILE_CONTENTS);
    } 
    read_team_file(game, teamFilename);
}

/**
 * Runs through set-up communication with the controller.
 * Can exit with protocol error, sinister file contents error, team file
 *     errors, or controller disconnected error.
 */
void set_up_simulation(Game *game, char *teamFile) {
    char *message = malloc(sizeof(char) * BUFFER);
    // check for "sinister" message and read sinister file and team file
    if (read_controller_msg(message, BUFFER, game->read) != SINISTER) {
        exit_game(EXIT_BAD_MESSAGE);
    }
    free(message);
    parse_game_files(game, game->read, teamFile);
    game->team->port = 0;

    // start waiting for connections
    pthread_t waiter;
    pthread_create(&waiter, NULL, enter_wait_mode, (void *)game);
    pthread_detach(waiter);

    // let controller know we're ready once we're accepting connections
    while (game->team->port == 0) {
    }
    fprintf(game->write, "iwannaplay %d %d %s %d\n", game->team->pos->x, 
            game->team->pos->y, game->team->name, game->team->port);
    fflush(game->write);
}

/**
 * Runs through a simulation, communicating with the controller and other teams
 *     as necessary. Prints narratives at the end of each round.
 * Exits normally if gameover received.
 * Exits with bad message if invalid message from controller or team received.
 * Exits with controller disconnected if unable to read from controller.
 */
void run_simulation(Game *game) { 
    char *message = malloc(sizeof(char) * BUFFER);
    Team *team = game->team;

    while (true) {
        ControllerMsgs type = read_controller_msg(message, BUFFER, game->read);
        if (type == BATTLE) {
            int pos = strlen("battle ");
            if (pos >= strlen(message)) {
                exit_game(EXIT_BAD_MESSAGE);
            }

            // get and print our coords
            free(team->pos);
            team->pos = get_coords(message, ' ', &pos);
            if (team->pos->x < 0 || team->pos->y < 0) {
                exit_game(EXIT_BAD_MESSAGE);
            }
            printf("Team is in zone %d %d\n", team->pos->x, team->pos->y);
            fflush(stdout);
            
            // start a challenge mode thread for each port 
            while (pos < strlen(message)) {
                char *portVal = get_token_update_pos(message, ' ', &pos);
                ThreadGame *params = malloc(sizeof(ThreadGame));
                params->port = number(portVal);
                params->game = game;
                free(portVal);
                pthread_t challenger;
                pthread_create(&challenger, NULL, spawn_challenge_thread, 
                        (void *)params);
                pthread_detach(challenger);
            }
        } else if (type == GAMEOVERMAN) {
            print_and_free_narratives(game);
            exit(0); // all good 
        } else if (type == WHERENOW) {
            print_and_free_narratives(game);
            fprintf(game->write, "travel %c\n", team->nextMove->direction);
            fflush(game->write);
            team->nextMove = team->nextMove->next; 
            continue;
        } else {
            exit(EXIT_BAD_MESSAGE);
        }
    }
}

int main(int argc, char **argv) {
    // check num args and usage
    if (argc < 3 || argc > 5) {
        exit_game(EXIT_ARGS);
    } else if (argc > 3 && !(strcmp(argv[1], "wait") == 0 || 
            strcmp(argv[1], "challenge") == 0)) {
        exit_game(EXIT_ARGS);
    }
    ignore_sigpipe();
    Game *game = new_game();
    char *teamFilename = argv[2]; 

    if (argc == 3) {
        // simulation mode
        int port = number(argv[1]);
        if (!valid_port(port)) {
            exit_game(EXIT_INVALID_PORT);
        }
        if (connect_to_port(port, &game->read, &game->write) < 0) {
            exit_game(EXIT_CONNECT_CONTROLLER);
        }
        game->simulation = true;
        set_up_simulation(game, teamFilename);
        run_simulation(game);
    } else {
        // parse sinister and team files
        FILE *sinister = fopen(argv[3], "r");
        if (sinister == NULL) {
            exit_game(EXIT_OPEN_SINISTER_FILE); 
        }
        parse_game_files(game, sinister, teamFilename);
        if (fgetc(sinister) != -1) {
            exit_game(EXIT_SINISTER_FILE_CONTENTS); // extra junk in sinister
        }
        game->simulation = false;

        // enter wait or challenge mode
        if (argc == 4) {
            enter_wait_mode((void *)game);
        } else if (argc == 5) {
            enter_challenge_mode(game, number(argv[4]));
            print_and_free_narratives(game);
        }
    }
    pthread_exit(0);
}
