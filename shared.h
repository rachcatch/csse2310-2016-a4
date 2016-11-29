#ifndef SHARED_H
#define SHARED_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <semaphore.h>

#define MAX_TEAM_PLAYERS 4
#define LEGAL_ATTACKS 3 // number of possible attacks per agent
#define MAX_HEALTH 10
#define MAX_PORT_NUMBER 65535
#define BUFFER 80 // pretty arbitrarily chosen buffer size

enum Effectiveness {
    HIGH = 3,
    NORMAL = 2,
    LOW = 1
};

typedef struct Type {
    char *name;
    char *effectiveness[3]; // {low, med, high}
    struct Type **lower; // types that this type is lower than
    struct Type **higher; // the types that this type is higher than
    int numLower;
    int numHigher;
} Type;

typedef struct {
    char *name;
    Type *type;
} Attack;

// circularly linked attack list node
typedef struct AttackNode {
    Attack *attack;
    struct AttackNode *next;
} AttackNode;

// circularly linked direction list node
typedef struct Direction {
    char direction; // N, E, S, W
    struct Direction *next;
} Direction;

typedef struct {
    char *name;
    Type *type;  
    Attack *legalAttacks[LEGAL_ATTACKS]; // list of legal attacks 
} Agent;

// A Team Member
typedef struct {
    Agent *agent;
    AttackNode *nextAttack; // circularly linked
    AttackNode *firstAttack;
    int health;
} Member;

typedef struct {
    int x;
    int y;
} Coords;

typedef struct {
    char *name;
    Member *members[MAX_TEAM_PLAYERS];
    int port; // port the team is waiting on
    Coords *pos; // team's position on the grid
    Direction *nextMove;
    FILE *read; // read from this team
    FILE *write; // write to this team
} Team;

// Holds all the sinsiter file data and game information
typedef struct Game {
    Team *team;
    Type **types;
    int numTypes;
    Agent **agents;
    int numAgents;
    Attack **attacks;
    int numAttacks;
    char **narratives;
    int numNarratives;
    sem_t narrativeLock; // for adding to narratives array
    bool simulation; // true if in simulation mode
    FILE *read; // read from controller
    FILE *write; // write to controller
} Game; 

// used for the purpose of passing game-related arguments to a thread
typedef struct {
    Game *game;
    Team *opposing;
    bool simulation;
    int port;
} ThreadGame;

// Used in controller to hold all of one simulation's info
typedef struct {
    Team **teams;
    int numTeams;
    int rounds;
    int width;
    int height;
    int fdServer;
    char *sinFilename;
} Simulation; 

// setup
void ignore_sigpipe(void);
int read_sinister_file(Game *game, FILE *file);
Agent *new_agent(char *name);
Team *new_team(char *name);
Game *new_game(void);

// map stuff
Agent *get_agent(Game *game, char *name);
Type *get_type(Game *game, char *name);
Attack *get_attack(Game *game, char *name);
bool legal_attack(Agent *agent, Attack *attack);

// networking shizzle
int open_listen(int *port);
int accept_connection(int fdServer, FILE **read, FILE **write);
bool valid_port(int port);

// general parsing
int number(char *string);
char *get_token(char *message, char delimiter);
char *get_token_update_pos(char *line, char delimiter, int *pos);
void read_line(char *result, int buffer, FILE *file);
Coords *get_coords(char *line, char end, int *pos);

#endif
