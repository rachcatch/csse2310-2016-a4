#include "shared.h"
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <arpa/inet.h>

/**
 * Reads a section of a sinister file. 
 * Once a line is deemed valid (not blank or a comment), control is passed to 
 *      the given processLine function.
 * Returns non-zero on EOF or empty line. Returns processLine's value if it is
 *      non-zero.
 * Returns 0 if all went well.
 */
int read_section(Game *game, FILE *file, int (*processLine)(Game *, char *)) {
    while (true) {
        char *line = malloc(sizeof(char) * 80);
        read_line(line, 80, file);
        if (line == NULL || strlen(line) == 0) {
            return -1; // unexpected EOF or blank line
        } else if (strcmp(line, ".") == 0) {
            break; // end of section
        } else if (line[0] == '#') {
            free(line);
            continue; // ignore comments
        }

        int result;
        if ((result = processLine(game, line)) != 0) {
            return result;
        }
        free(line);
    }
    return 0;
}

/**
 * Returns Coords populated with the values read from the given line.
 * Line should consist of the x value, a space, the y value, and the end char.
 * Returned x and y values will be -1 if invalid line.
 */
Coords *get_coords(char *line, char end, int *pos) {
    Coords *coords = malloc(sizeof(Coords));
    char *x = get_token_update_pos(line, ' ', pos);
    char *y = get_token_update_pos(line, end, pos);
    coords->x = number(x);
    coords->y = number(y);
    free(x);
    free(y);
    return coords;
}

/**
 * Returns a type with the given name
 */
Type *new_type(char *name) {
    Type *type = malloc(sizeof(Type));
    type->name = name;
    type->lower = NULL;
    type->higher = NULL;
    type->effectiveness[0] = NULL;
    type->numLower = 0;
    type->numHigher = 0;
    return type;
}

/** 
 * Returns first part of message up to the given delimiter.
 * Returns the full message if no delimiter found.
 */
char *get_token(char *message, char delimiter) {
    int buffer = 10;
    char *result = malloc(sizeof(char) * buffer);

    int i = 0;
    while (i < strlen(message)) {
        if (i == buffer - 1) {
            buffer += 10;
            result = realloc(result, sizeof(char) * buffer);
        }
        if (message[i] == delimiter) {
            break;
        }
        result[i] = message[i];
        i++;
    }
    result[i] = '\0';

    return result;
}

/**
 * Adds the type name from the given line to the game data.
 * Returns non-zero if an error occurred.
 */
int read_type_name(Game *game, char *line) {
    char *name = malloc(sizeof(char) * (strlen(line) + 1));
    strcpy(name, line);
    char *token = get_token(name, ' ');
    if (strcmp(name, token) != 0 || strlen(name) == 0) {
        return -1; // a space appeared, or line was blank.
    }
    free(token);

    // add to game data
    game->numTypes++;
    game->types = realloc(game->types, sizeof(Type *) * game->numTypes);
    game->types[game->numTypes - 1] = new_type(name);
    return 0;
}

/**
 * Returns part of the given line from line[pos] up to the delimiter character.
 * Updates pos to the index of the next character after the delimiter.
 */
char *get_token_update_pos(char *line, char delimiter, int *pos) {
    if (*pos >= strlen(line)) {
        return NULL; // pos is not within string
    }
    char *token = get_token(&line[*pos], delimiter);
    *pos += strlen(token) + 1;
    return token;
}

/**
 * Adds the relations data from the given sinister file line to game data.
 * Returns non-zero on error.
 */
int read_relation_strings(Game *game, char *line) {
    // check the type is legit
    int pos = 0;
    char *typeName = get_token_update_pos(line, ' ', &pos);
    Type *type = get_type(game, typeName);
    if (type == NULL || type->numLower > 0 || type->numHigher > 0) {
        return -1; // invalid or duplicate type
    }
    free(typeName);

    // read relations
    while (pos < strlen(line)) {
        char *relation = get_token_update_pos(line, ' ', &pos);
        if (strlen(relation) < 2) {
            return -1; // didn't get at least two chars.
        }
        Type *related = get_type(game, &(relation[1])); 
        if (related == NULL) {
            return -1; // invalid type found
        }
        switch (relation[0]) {
            case '+':
                type->higher = realloc(type->higher, sizeof(Type *) * 
                        ++type->numHigher);
                type->higher[type->numHigher - 1] = related;
                break;
            case '-':
                type->lower = realloc(type->lower, sizeof(Type *) * 
                        ++type->numLower);
                type->lower[type->numLower - 1] = related;
                break;
            case '=':
                break; // don't care.
            default:
                return -1; // bad character
        }
        free(relation);
    }
    if (line[strlen(line) - 1] == ' ') {
        return -1; // trailing space
    }
    return 0;
}

/**
 * Adds the effectiveness data from the given sinister file line to game data.
 * Returns non-zero if an error occurred.
 */
int read_effectiveness_strings(Game *game, char *line) {
    // read type name
    int pos = 0;
    char *typeName = get_token_update_pos(line, ' ', &pos);
    Type *type = get_type(game, typeName);
    if (type == NULL) {
        return -1; // invalid type
    }
    free(typeName);
    if (type->effectiveness[0] != NULL) {
        return -1; // duplicate types
    }

    // read three effectiveness strings
    for (int i = 2; i >= 0; i--) {
        if (pos >= strlen(line)) {
            return -1; // not enough effectiveness strings
        }
        type->effectiveness[i] = get_token_update_pos(line, ' ', &pos);
        if (strlen(type->effectiveness[i]) == 0) {
            return -1; // consecutive spaces
        }
    }

    if (pos < strlen(line) || line[strlen(line) - 1] == ' ') {
        return -1; // too much data on line
    }
    return 0; // all is well
}

/**
 * Creates a new attack with the given name
 */
Attack *new_attack(char *name) {
    Attack *attack = malloc(sizeof(Attack));
    attack->name = name;
    return attack;
}

/**
 * Adds the attack in the given line to the game data.
 * Returns non-zero if error.
 */
int read_attack(Game *game, char *line) {
    char *attackName = get_token(line, ' ');
    char *typeName = get_token(&line[strlen(attackName) + 1], '\0');
    if (get_attack(game, attackName) != NULL || 
            strlen(attackName) == 0 || strlen(typeName) == 0) {
        return -1; // consecutive spaces or duplicate attack
    }

    Attack *attack = new_attack(attackName);
    if ((attack->type = get_type(game, typeName)) == NULL) {
        return -1; // invalid type
    }
    free(typeName);

    game->attacks = realloc(game->attacks, sizeof(Attack *) * 
            ++game->numAttacks);
    game->attacks[game->numAttacks - 1] = attack;
    return 0;
}

/**
 * Adds the agent given in the line to game data.
 * Returns non-zero if error.
 */
int read_agent(Game *game, char *line) {
    // agent name 
    int pos = 0;
    char *name = get_token_update_pos(line, ' ', &pos);
    Agent *agent = get_agent(game, name);
    if (pos >= strlen(line) || strlen(name) == 0 || agent != NULL) {
        return -1; // not enough data, consecutive spaces, or duplicate agent
    }
    // agent type
    char *typeName = get_token_update_pos(line, ' ', &pos);
    Type *type = get_type(game, typeName);
    free(typeName);
    if (type == NULL) {
        return -1; // invalid type
    }
    agent = new_agent(name);
    agent->type = type;

    // get legal attacks
    for (int i = 0; i < LEGAL_ATTACKS; i++) {
        if (pos >= strlen(line)) {
            return -1; // not enough attacks
        }
        char *attackName = get_token_update_pos(line, ' ', &pos);
        Attack *attack = get_attack(game, attackName);
        free(attackName);
        if (attack == NULL) {
            return -1; // invalid attack
        }
        agent->legalAttacks[i] = attack;
    }

    if (line[strlen(line) - 1] == ' ' || pos < strlen(line)) {
        return -1;
    }
    // add agent to game data
    game->agents = realloc(game->agents, sizeof(Agent *) * ++game->numAgents);
    game->agents[game->numAgents - 1] = agent;
    return 0;
}

/**
 * Returns a new game
 */
Game *new_game(void) {
    Game *game = malloc(sizeof(Game));
    game->types = NULL;
    game->agents = NULL;
    game->attacks = NULL;
    game->numTypes = 0;
    game->numAgents = 0;
    game->numAttacks = 0;
    game->numNarratives = 0;
    sem_init(&game->narrativeLock, 0, 1);
    return game;
}

/**
 * Reads sinister file and populates the given game struct.
 * Returns non-zero if an error occurred.
 */
int read_sinister_file(Game *game, FILE *file) {
    // do most of the parsing
    if (read_section(game, file, read_type_name) ||
            read_section(game, file, read_effectiveness_strings) ||
            read_section(game, file, read_relation_strings) ||
            read_section(game, file, read_attack) || 
            read_section(game, file, read_agent)) {
        return -1;
    }
    // check we have some data for each category
    if (game->numTypes <= 0 || game->numAgents <= 0 || game->numAttacks <= 0) {
        return -1;
    }
    // check each type was in the effectiveness section
    for (int i = 0; i < game->numTypes; i++) {
        Type *type = game->types[i];
        if (type->effectiveness[0] == NULL) {
            return -1;
        }
    }
    return 0;
}

/**
 * Returns the type with the given typename, null if it doesn't exist.
 */
Type *get_type(Game *game, char *typeName) {
    for (int i = 0; i < game->numTypes; i++) {
        if (strcmp(game->types[i]->name, typeName) == 0) {
            return game->types[i];
        }
    }
    return NULL;
}

/** 
 * Returns the number given in the string, or -1 if not a valid number.
 */
int number(char *string) {
    for (int i = 0; i < strlen(string); i++) {
        if (!isdigit(string[i])) {
            return -1;
        }
    }
    return atoi(string);
}

/*
 * Returns true if the given port is in the valid range of ports (1 - 65535)
 */
bool valid_port(int port) {
    if (port <= 0 || port > MAX_PORT_NUMBER) {
        return false;
    }
    return true;
}

/**
 * Returns the attack with the given name, NULL if not found
 */
Attack *get_attack(Game *game, char *name) {
    for (int i = 0; i < game->numAttacks; i++) {
        if (strcmp(game->attacks[i]->name, name) == 0) {
            return game->attacks[i];
        }
    }
    return NULL;
}

/**
 * Returns the agent with the given name, NULL if not found
 */
Agent *get_agent(Game *game, char *name) {
    for (int i = 0; i < game->numAgents; i++) {
        if (strcmp(game->agents[i]->name, name) == 0) {
            return game->agents[i];
        }
    }
    return NULL;
}

/**
 * True if agent can legally use attack, false otherwise
 */
bool legal_attack(Agent *agent, Attack *attack) {
    for (int i = 0; i < LEGAL_ATTACKS; i++) {
        if (strcmp(agent->legalAttacks[i]->name, attack->name) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Returns a new agent
 */
Agent *new_agent(char *name) {
    Agent *agent = malloc(sizeof(Agent));
    agent->name = name;
    return agent;
}

/**
 * Accepts a connection on the given fdServer and returns accept's result.
 * read and write will be set to communicate over the accepted connection.
 */
int accept_connection(int fdServer, FILE **read, FILE **write) {
    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize = sizeof(struct sockaddr_in);
    int fd = accept(fdServer, (struct sockaddr *)&fromAddr, &fromAddrSize);
    *read = fdopen(fd, "r");
    *write = fdopen(fd, "w");
    return fd;
}

/**
 * Populates result with a line from the file, reallocing if necessary.
 * Leaves off newline character.
 * result must be malloc'd to buffer size prior to using this function.
 */
void read_line(char *result, int buffer, FILE *file) {
    int c;
    int position = 0;
    while ((c = fgetc(file)) != '\n' && c != EOF) {
        result[position++] = c;
        if (position == buffer - 1) {
            buffer *= 2;
            result = realloc(result, buffer);
        }
    }
    if (result[position - 1] == '\n') {
        result[position - 1] = '\0'; // remove newline
    }
    result[position] = '\0';
}

/**
 * Opens the given port (ephemeral if 0), and returns the associated
 * file descriptor. The given port updates to the value of the assigned port.
 * Returns <0 if an error occurs.
 * Largely taken from lecture slides.
 */
int open_listen(int *port) {
    // Create socket (TCP IPv4)
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    // Allow address (port number) to be reused immediately
    int optVal = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(int)) < 0) {
        return -1;
    }

    // Populate server address structure
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;    // IP v4
    serverAddr.sin_port = htons(*port);  // port num
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); // any IP addr of this host

    // Bind our socket to address we just created
    if (bind(fd, (struct sockaddr *)&serverAddr, sizeof(struct sockaddr_in)) < 
            0) {
        return -1;
    }
    
    struct sockaddr_in ephemeralAddr; // ephemeral goodness
    socklen_t nSize = sizeof(struct sockaddr);
    getsockname(fd, (struct sockaddr *)&ephemeralAddr, &nSize);
    *port = ntohs(ephemeralAddr.sin_port);

    // Indicate our willingness to accept connections
    if (listen(fd, SOMAXCONN) < 0) {
        return -1;
    }

    return fd;
}

/**
 * Returns a new team with the given name.
 */
Team *new_team(char *name) {
    Team *team = malloc(sizeof(Team));
    team->name = name;
    team->port = 0;
    team->nextMove = NULL;
    return team;
}

/* Ignores sigpipes */
void ignore_sigpipe(void) {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGPIPE, &sa, 0);
}
