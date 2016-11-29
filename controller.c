#include "shared.h"
#include <stdlib.h>
#include <pthread.h>

#define MIN_DIMENSION 1

// All error exit codes
enum ExitCodes {
    EXIT_ARGS = 1,
    EXIT_INVALID_HEIGHT = 2,
    EXIT_INVALID_WIDTH = 3,
    EXIT_OPEN_FILE = 4,
    EXIT_FILE_CONTENTS = 5,
    EXIT_INVALID_ROUNDS = 6,
    EXIT_INVALID_PORT = 7,
    EXIT_PORT_USED = 8,
    EXIT_INVALID_TEAMS = 9,
    EXIT_BAD_MESSAGE = 19,
    EXIT_SYSTEM = 20
};

// Message types that we can receive
enum Messages {
    IWANNAPLAY,
    DONEFIGHTING,
    DISCO,
    TRAVEL,
    END // used for EOF
};

// List of teams and number of teams. Used for grouping teams in same zone.
typedef struct {
    Team **teams;
    int numTeams;
} GroupedTeams;

/**
 * Exits the program with the given status and corresponding error message
 */
void exit_game(int status) {
    char *message;
    switch (status) {
        case EXIT_ARGS:
            message = "Usage: 2310controller height width sinisterfile rounds "
                    "port teams [[rounds port teams] ...]";
            break;
        case EXIT_INVALID_HEIGHT:
            message = "Invalid height";
            break;
        case EXIT_INVALID_WIDTH:
            message = "Invalid width";
            break;
        case EXIT_OPEN_FILE:
            message = "Unable to access sinister file";
            break;
        case EXIT_FILE_CONTENTS:
            message = "Error reading sinister file";
            break;
        case EXIT_INVALID_ROUNDS:
            message = "Invalid number of rounds";
            break;
        case EXIT_INVALID_PORT:
            message = "Invalid port number";
            break;
        case EXIT_PORT_USED:
            message = "Unable to listen on port";
            break;
        case EXIT_INVALID_TEAMS:
            message = "Invalid number of teams";
            break;
        case EXIT_BAD_MESSAGE:
            message = "Protocol error";
            break;
        case EXIT_SYSTEM:
            message = "System error";
            break;
        default:
            message = "Well, this is awkward";
    }
    fprintf(stderr, "%s\n", message);
    exit(status);
}

/**
 * Ensures the given rounds, port, and teams are correct; and prints the port
 * it is listening on
 */
void setup_simulation(Simulation *sim, char *rounds, char *port, char *teams) {
    // check number of rounds
    sim->rounds = number(rounds);
    if (sim->rounds <= 0) {
        exit_game(EXIT_INVALID_ROUNDS);
    }
    // check port number
    int portNo;
    if (strcmp(port, "-") == 0) {
        portNo = 0;
    } else {
        portNo = number(port);
        if (!valid_port(portNo)) {
            exit_game(EXIT_INVALID_PORT);
        }
    }
    // check number of teams
    sim->numTeams = number(teams);
    if (sim->numTeams <= 1) {
        exit_game(EXIT_INVALID_TEAMS);
    }

    // start listening and print out port
    sim->fdServer = open_listen(&portNo);
    if (sim->fdServer == -1) {
        exit_game(EXIT_PORT_USED);
    }
    printf("%d\n", portNo);
    fflush(stdout);
}

/**
 * Reads a message from the given file stream and returns its type.
 * Exits with protocol error if the message doesn't conform to any type.
 */
enum Messages read_msg(char *result, int length, FILE *file) {
    int c = fgetc(file);
    if (c == EOF) {
        return END;
    }
    ungetc(c, file);
    read_line(result, length, file);
    enum Messages messageType = -1;
    char *type = get_token(result, ' ');
    if (strcmp(type, "iwannaplay") == 0) {
        messageType = IWANNAPLAY;
    } else if (strcmp(type, "donefighting") == 0) {
        messageType = DONEFIGHTING;
    } else if (strcmp(type, "disco") == 0) {
        messageType = DISCO;
    } else if (strcmp(type, "travel") == 0) {
        messageType = TRAVEL;
    } else {
        exit_game(EXIT_BAD_MESSAGE); 
    }
    free(type);
    return messageType;
}

/**
 * For qsorting teams alphabetically by name
 */
int sort_teams(const void *a, const void *b) {
    return strcmp((*((Team **)a))->name, (*((Team **)b))->name);
}

/**
 * Sends gameoverman message to all teams in the simulation
 */
void send_gameoverman(Simulation *sim) {
    for (int i = 0; i < sim->numTeams; i++) {
        Team *team = sim->teams[i];
        fprintf(team->write, "gameoverman\n");
        fflush(team->write);
    }
}

/** 
 * Reads all end of battle messages.
 * Exits with a protocol error if an invalid message is received.
 * Exits with status 0 and sends "gameoverman" to all players if disco received
 */
void read_donefighting_messages(Simulation *sim) {
    char *message = malloc(sizeof(char) * BUFFER);
    bool endEarly = false;

    // find teams who would have battled
    for (int i = 0; i < sim->numTeams; i++) {
        Team *a = sim->teams[i];
        for (int j = i + 1; j < sim->numTeams; j++) {
            Team *b = sim->teams[j];
            if (a->pos->x == b->pos->x && a->pos->y == b->pos->y) {
                // two teams in same grid square - get their messages
                enum Messages typeA = read_msg(message, BUFFER, a->read);
                enum Messages typeB = read_msg(message, BUFFER, b->read);
                if (typeA == DONEFIGHTING && typeB == DONEFIGHTING) {
                    continue; // both teams are all good
                } else if ((typeA == DISCO && typeB == END) || 
                        (typeA == END && typeB == DISCO)) {
                    endEarly = true; // one team disconnected
                } else {
                    exit_game(EXIT_BAD_MESSAGE); // unexpected message
                }
            }
        }
    }
    if (endEarly) {
        // A battle ended early due to disconnection; send gameover and exit
        send_gameoverman(sim);
        exit(0);
    }
    free(message);
}

/**
 * Returns a list of grouped teams by zone. Populates numZones with the number
 *      of zones containing a team.
 */
GroupedTeams **get_grouped_teams(Simulation *sim, int *numZones) {
    GroupedTeams **teamGroups = NULL;
    for (int i = 0; i < sim->numTeams; i++) {
        // add each team to a group
        bool placed = false;
        Team *a = sim->teams[i];
        for (int j = 0; j < *numZones; j++) {
            Team *b = teamGroups[j]->teams[0];
            if (b->pos->x == a->pos->x && b->pos->y == a->pos->y) {
                teamGroups[j]->teams = realloc(teamGroups[j]->teams, 
                        sizeof(Team *) * (++(teamGroups[j]->numTeams)));
                teamGroups[j]->teams[teamGroups[j]->numTeams - 1] = a;
                placed = true;
                break; // we've found an existing group to add them to
            }
        }
        if (!placed) {
            // add to new group 
            GroupedTeams *group = malloc(sizeof(GroupedTeams));
            group->numTeams = 1;
            group->teams = malloc(sizeof(Team *));
            group->teams[0] = a;
            (*numZones)++;
            teamGroups = realloc(teamGroups, sizeof(GroupedTeams *) * 
                    *numZones);
            teamGroups[(*numZones) - 1] = group;
        }
    }
    return teamGroups;
}

/**
 * Sends battle messages to all team members in the given simulation.
 */
void send_battle_messages(Simulation *sim) {
    int numZones = 0;
    GroupedTeams **teams = get_grouped_teams(sim, &numZones);

    // Send battle coords to all teams in each zone
    for (int i = 0; i < numZones; i++) {
        GroupedTeams *group = teams[i];
        // message all but last team in zone
        for (int j = 0; j < group->numTeams - 1; j++) {
            Team *a = group->teams[j];
            fprintf(a->write, "battle %d %d", a->pos->x, a->pos->y);
            for (int k = j + 1; k < group->numTeams - 1; k++) {
                Team *b = group->teams[k];
                fprintf(a->write, " %d", b->port);
            }
            fprintf(a->write, "\n");
            fflush(a->write);
        }
        // message last team in zone
        Team *last = group->teams[group->numTeams - 1];
        fprintf(last->write, "battle %d %d", last->pos->x, last->pos->y);
        for (int j = 0; j < group->numTeams - 1; j++) {
            Team *b = group->teams[j];
            if (last->pos->x == b->pos->x && last->pos->y == b->pos->y) {
                fprintf(last->write, " %d", b->port);
            }
        }
        fprintf(last->write, "\n");
        fflush(last->write);
    }
}

/**
 * Asks participants which direction they are going, and updates their location
 *      based on their response.
 * Exits with protocol error if a communication error occurs.
 */
void process_wherenow_messages(Simulation *sim) {
    char *message = malloc(sizeof(char) * BUFFER);
    for (int j = 0; j < sim->numTeams; j++) {
        // send "wherenow?"
        Team *team = sim->teams[j];
        fprintf(team->write, "wherenow?\n");
        fflush(team->write);
        // get their response
        if (read_msg(message, BUFFER, team->read) != TRAVEL ||
                strlen(message) != strlen("travel d")) {
            exit_game(EXIT_BAD_MESSAGE);
        }
        switch(message[strlen("travel ")]) {
            case 'N':
                team->pos->y += 1;
                break;
            case 'E':
                team->pos->x += 1;
                break;
            case 'S':
                team->pos->y -= 1;
                break;
            case 'W':
                team->pos->x -= 1;
                break;
            default:
                exit_game(EXIT_BAD_MESSAGE);
        }
        if (team->pos->x < 0) {
            team->pos->x = sim->width - 1;
        } else if (team->pos->y < 0) {
            team->pos->y = sim->height - 1;
        }
        team->pos->x = team->pos->x % sim->width;
        team->pos->y = team->pos->y % sim->height;
    }
    free(message);
}

/**
 * Accepts a connection from a team, runs through setup messages, 
 *   and populates team with data received.
 * Exits with protocol error if invalid data received.
 */
void connect_team(Simulation *sim, Team *team) {
    // connect and send sinister file
    accept_connection(sim->fdServer, &team->read, &team->write);
    char *message = malloc(sizeof(char) * BUFFER);
    FILE *sinister = fopen(sim->sinFilename, "r");
    fprintf(team->write, "sinister\n");
    while (fgets(message, BUFFER, sinister) != NULL) {
        fprintf(team->write, "%s", message);
    }
    fflush(team->write);

    // get coords from "iwannaplay" message
    if (read_msg(message, BUFFER, team->read) != IWANNAPLAY) {
        exit_game(EXIT_BAD_MESSAGE);
    }
    int pos = strlen("iwannaplay ");
    team->pos = get_coords(message, ' ', &pos);
    if (team->pos->x < 0 || team->pos->y < 0 || pos >= strlen(message)) {
        exit_game(EXIT_BAD_MESSAGE); // bad coords
    }
    team->pos->x = team->pos->x % sim->width;
    team->pos->y = team->pos->y % sim->height;

    // get team name and port
    team->name = get_token_update_pos(message, ' ', &pos);
    if (pos >= strlen(message)) {
        exit_game(EXIT_BAD_MESSAGE); // not enough info
    }
    char *portVal = get_token_update_pos(message, '\0', &pos);
    team->port = number(portVal);
    free(portVal);
    if (!valid_port(team->port)) {
        exit_game(EXIT_BAD_MESSAGE);
    }
    free(message);
}

/**
 * Runs a simulation
 */
void *run_simulation(void *args) {
    // accept a connection from each team, and send setup info 
    Simulation *sim = (Simulation *) args;
    sim->teams = malloc(sizeof(Team *) * sim->numTeams);
    for (int i = 0; i < sim->numTeams; i++) {
        sim->teams[i] = malloc(sizeof(Team));
        connect_team(sim, sim->teams[i]);
    }
    // sort teams alphabetically
    qsort(sim->teams, sim->numTeams, sizeof(Team *), sort_teams);

    // run each round in the simulation
    for (int round = 0; round < sim->rounds; round++) {
        send_battle_messages(sim);
        read_donefighting_messages(sim);
        if (round == sim->rounds - 1) {
            // last round - send all gameover messages
            send_gameoverman(sim);
            pthread_exit(0);
        }
        process_wherenow_messages(sim);
    } 
    return NULL;
}

int main(int argc, char **argv) {
    // check usage 
    if (argc < 7 || ((argc - 4) % 3) != 0) {
        exit_game(EXIT_ARGS);
    }
    ignore_sigpipe();

    // check height, width
    int height = number(argv[1]);
    int width = number(argv[2]);
    if (height < MIN_DIMENSION) {
        exit_game(EXIT_INVALID_HEIGHT);
    } else if (width < MIN_DIMENSION) {
        exit_game(EXIT_INVALID_WIDTH);
    }

    // check sinister file
    char *sinisterFilename = argv[3];
    FILE *sinister = fopen(argv[3], "r");
    Game *game = new_game();
    if (sinister == NULL) {
        exit_game(EXIT_OPEN_FILE);
    }
    if (read_sinister_file(game, sinister) != 0) {
        exit_game(EXIT_FILE_CONTENTS);
    }

    // run each simulation in its own thread
    for (int i = 4; i < argc; i += 3) {
        Simulation *simulation = malloc(sizeof(Simulation));
        simulation->height = height;
        simulation->width = width;
        simulation->sinFilename = sinisterFilename;
        setup_simulation(simulation, argv[i], argv[i + 1], argv[i + 2]);
        pthread_t simRunner;
        pthread_create(&simRunner, NULL, run_simulation, (void *)simulation);
        pthread_detach(simRunner);
    } 
    pthread_exit(0);
}
