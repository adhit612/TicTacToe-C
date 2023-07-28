//Tic Tac Toe Server Code!
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#ifndef DEBUG
#define DEBUG 0
#endif

#define BUFSIZE 256
#define HOSTSIZE 100
#define PORTSIZE 10

#define QUEUE_SIZE 8

volatile int active = 1;

//this function will be run for each client
void *connection_handler(void*);
void *connection_handler_player_One(void*);
void *connection_handler_player_Two(void*);

int enterPosAndCheckVictory(char [3][3], char, int, int);
int checkForVictory(char[3][3],char);
void printGameGrid(char [3][3],int,int);
int tieChecker(char [3][3]);

enum Turn{
    NONE,
    PONE,
    PTWO
};

struct Client{
    char domain[500];
    char port[500]; 
    char clientToken; //assigns an X or O to the player
};

struct Game{
    char gameGrid[3][3];
    struct Client * clientOne;
    struct Client * clientTwo;
    int gameState; //0 means no command has been entered, 1 means game is in wait state, 2 means in play state
    pthread_mutex_t lock;
    int sockOne;
    int sockTwo;
    pthread_t threadIDOne;
    pthread_t threadIDTwo;
    enum Turn playerTurn;
    int exitCondition;
    char *clientOneOrTwo;
    int drawState;
    char drawRequestToken; //who is requesting for draw
    char playerOneName [100];
    char playerTwoName [100];
};

void resetGameStruct(struct Game*);

void freeGameStruct(struct Game*);

void completionOfGame(struct Game*);

struct Game* gameList[256];

char globalNameList[512][512]; //512 strings with each having 512 character max size

struct Game* initializeGame();

void handler(int signum)
{
    active = 0;
}

void install_handlers(void)
{
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
}

int open_listener(char *service, int queue_size)
{
    struct addrinfo hint, *info_list, *info;
    int error, sock;

    // initialize hints
    memset(&hint, 0, sizeof(struct addrinfo));
    hint.ai_family   = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags    = AI_PASSIVE;

    // obtain information for listening socket
    error = getaddrinfo(NULL, service, &hint, &info_list);
    if (error) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return -1;
    }

    // attempt to create socket
    for (info = info_list; info != NULL; info = info->ai_next) {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

        // if we could not create the socket, try the next method
        if (sock == -1) continue;

        // bind socket to requested port
        error = bind(sock, info->ai_addr, info->ai_addrlen);
        if (error) {
            close(sock);
            continue;
        }

        // enable listening for incoming connection requests
        error = listen(sock, queue_size);
        if (error) {
            close(sock);
            continue;
        }

        // if we got this far, we have opened the socket
        break;
    }

    freeaddrinfo(info_list);

    // info will be NULL if no method succeeded
    if (info == NULL) {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }

    return sock;
}

void printGameContents(struct Game* game){
    printf("===GAME CONTENTS===\n");
    printf("client One token is %c\n",game->clientOne->clientToken);
    printf("client One domain %s\n",game->clientOne->domain);
    printf("client One port is %s\n",game->clientOne->port);
    printf("client Two token is%c\n",game->clientTwo->clientToken);
    printf("client Two domain is %s\n",game->clientTwo->domain);
    printf("client Two port iw %s\n",game->clientTwo->port);
    printf("gameState: %d\n",game->gameState);
    printf("sockOne: %d\n",game->sockOne);
    printf("sockTwo: %d\n",game->sockTwo);
    printf("playerTurn: %d\n",game->playerTurn);
    printf("threadIdOne: %ld\n",game->threadIDOne);
    printf("threadIdTwo: %ld\n",game->threadIDTwo);
    printf("exit condition: %d\n",game->exitCondition);

    for(int r = 0; r < 3; r ++){
        for(int c = 0; c < 3; c ++){
            printf("%c",game->gameGrid[r][c]);
        }
        printf("\n");
    }
    printf("==END OF CONTENTS==\n");
}

int main(int argc, char **argv)
{
        pthread_t thread_id;

        for(int i = 0; i < 256; i ++){
            struct Game *gameAtPos = initializeGame();
            gameList[i] = gameAtPos;
        }

        for(int i = 0; i < 512; i ++){
            strcpy(globalNameList[i], "-");
        }

        struct sockaddr_storage remote_host;
        socklen_t remote_host_len;

        char *service = argc == 2 ? argv[1] : argv[1];

        install_handlers();
        
        int listener = open_listener(service, QUEUE_SIZE);
        if (listener < 0) exit(EXIT_FAILURE);
        
        puts("Waiting for Players");

        while (active){
            remote_host_len = sizeof(remote_host);
            int sock = accept(listener, 
                (struct sockaddr *)&remote_host,
                &remote_host_len);
            if (sock < 0) {
                perror("accept");
                continue;
            }
            else{}

            sleep(2);
            char buf[BUFSIZE + 1], host[HOSTSIZE], port[PORTSIZE];
            int error = getnameinfo((struct sockaddr *)&remote_host, remote_host_len, host, HOSTSIZE, port, PORTSIZE, NI_NUMERICSERV);
            if (error) {
            fprintf(stderr, "getnameinfo: %s\n", gai_strerror(error));
            strcpy(host, "??");
            strcpy(port, "??");
            }

            int indexForFirstFullyEmptyGame = 0;
            int fullyEmptyGameCounter = 0;

            int indexForFirstHalfGame = 0;
            int halfGameCounter = 0;

            int finalIndex = 0;

            for(int i = 0; i < 256; i ++){
                if(gameList[i]->clientOne->clientToken == '\0' && gameList[i]->clientTwo->clientToken == '\0' && fullyEmptyGameCounter == 0){ //check for fully empty game
                    indexForFirstFullyEmptyGame = i;
                    fullyEmptyGameCounter ++;
                }
                else if(gameList[i]->clientOne->clientToken == 'X' && gameList[i]->clientTwo->clientToken == '\0' && halfGameCounter == 0){ //check for half empty game
                    indexForFirstHalfGame = i;
                    halfGameCounter ++;
                }
                else{}
            }

            if(halfGameCounter == 1){ //add the connection as p2
                if(DEBUG)printf("At client two creation state with game state %d\n",gameList[indexForFirstHalfGame]->gameState);   
                gameList[indexForFirstHalfGame]->clientTwo->clientToken = 'O';
                memcpy(gameList[indexForFirstHalfGame]->clientTwo->domain,host,sizeof(host));
                memcpy(gameList[indexForFirstHalfGame]->clientTwo->port,port,sizeof(port));
                memcpy(gameList[indexForFirstHalfGame]->clientOneOrTwo,"two",4);
                gameList[indexForFirstHalfGame]->sockTwo = sock;
                finalIndex = indexForFirstHalfGame;
            }
            else{ //new game has to be created
                if(DEBUG)printf("At client one creation state with game state %d\n",gameList[indexForFirstFullyEmptyGame]->gameState);   
                gameList[indexForFirstFullyEmptyGame]->clientOne->clientToken = 'X';
                memcpy(gameList[indexForFirstFullyEmptyGame]->clientOne->domain,host,sizeof(host));
                memcpy(gameList[indexForFirstFullyEmptyGame]->clientOne->port,port,sizeof(port));
                memcpy(gameList[indexForFirstFullyEmptyGame]->clientOneOrTwo,"one",4);
                gameList[indexForFirstFullyEmptyGame]->sockOne = sock;
                finalIndex = indexForFirstFullyEmptyGame;
            }

            if(DEBUG){
                printf("at thread creation\n");
            }

            if(strcmp(gameList[finalIndex]->clientOneOrTwo,"one") == 0){
                if(pthread_create(&thread_id,NULL,connection_handler_player_One,(void*)gameList[finalIndex]) == -1){
                    printf("NOT WORKING\n");
                }
                else{
                    if(DEBUG)printf("THREAD CREATED\n");
                }
            }
            else{
                if(pthread_create(&thread_id,NULL,connection_handler_player_Two,(void*)gameList[finalIndex]) == -1){
                    printf("NOT WORKING\n");
                }
                else{
                    if(DEBUG)printf("THREAD CREATED\n");
                }
            }
            pthread_detach(thread_id);
        } //end of while(active) loop

        for(int i = 0; i < 256; i ++){
            freeGameStruct(gameList[i]);
        }

        puts("Shutting down");
        close(listener);

    return EXIT_SUCCESS;
}

void *connection_handler_player_One(void *arg){
    struct Game* game = (struct Game *) arg;
    int sock = game->sockOne;
    if(DEBUG){   
        printf("IN THREAD FOR CLIENT one\n");
    }

    pthread_mutex_lock(&game->lock);
    game->threadIDOne = pthread_self();
    pthread_mutex_unlock(&game->lock);

    char message [] = "Welcome to the TicTacToe game!";
    int messageSize = strlen(message);

    char messageTwo[] = "Here are the rules:";
    int messageTwoSize = strlen(messageTwo);

    char messageThree[] = "PLAY ____ => enter your name";
    int messageThreeSize = strlen(messageThree);

    char messageFour[] = "MOVE X,Y => move your token to position (starting at indices 0,0)";
    int messageFourSize = strlen(messageFour);

    char messageFive[] = "RESIGN => leave the game!";
    int messageFiveSize = strlen(messageFive);

    char messageSix[] = "DRAW S => suggest a draw";
    int messageSixSize = strlen(messageSix);

    char messageSev[] = "When game is done, hit enter multiple times to exit";
    int messageSevSize = strlen(messageSev);

    if(write(sock,message,messageSize) < 0){printf("write FAILED with sock: %d\n",sock);}
    sleep(1);
    if(write(sock,messageTwo,messageTwoSize) < 0){printf("write FAILED with sock: %d\n",sock);}
    sleep(1);
    if(write(sock,messageThree,messageThreeSize) < 0){printf("write FAILED with sock: %d\n",sock);}
    sleep(1);
    if(write(sock,messageFour,messageFourSize) < 0){printf("write FAILED with sock: %d\n",sock);}
    sleep(1);
    if(write(sock,messageFive,messageFiveSize) < 0){printf("write FAILED with sock: %d\n",sock);}
    sleep(1);
    if(write(sock,messageSix,messageSixSize) < 0){printf("write FAILED with sock: %d\n",sock);}
    sleep(1);
    if(write(sock,messageSev,messageSevSize) < 0){printf("write FAILED with sock: %d\n",sock);}

    char buf[BUFSIZE + 1], host[HOSTSIZE], port[PORTSIZE];
    int bytes, error;
    int playCount = 0;

    memset(buf, '\0', sizeof(buf));
    while (active && (((bytes = (read(sock, buf, BUFSIZE))) > 0))){
        //first split up the given line from client into words
        buf[bytes-1] = '\0';
        char *words[256];
        for(int i = 0; i < 256; i ++){
            words[i] = NULL;
        }
        words[0] = '\0';
        int i = 0;
        int wordsSize = 0;
        char *token;
        token = strtok(buf," ");
        while(token != NULL){
            words[i] = token;
            i ++;
            wordsSize ++;
            token = strtok(NULL, " ");
        }

        //done splitting up, now time to interpret
        int stateOfGame = 0;
        pthread_mutex_lock( &game->lock);
        stateOfGame =  game->gameState;
        pthread_mutex_unlock( &game->lock);

        pthread_mutex_lock(&game->lock);
        if(game->drawState == 1 && game->drawRequestToken == 'O'){
            if(strcmp(words[0],"A") == 0 && wordsSize == 1){
                char finT [] = "OVER|17|D|Draw accepted.|";
                int finST = strlen(finT);
                if(write(game->sockOne,finT,finST) < 0){printf("FAILED\n");}
                if(write(game->sockTwo,finT,finST) < 0){printf("FAILED\n");}
                sleep(0.2);
                char fin [] = "Draw accepted, game over!";
                int finS = strlen(fin);
                if(write(game->sockOne,fin,finS) < 0){printf("FAILED\n");}
                if(write(game->sockTwo,fin,finS) < 0){printf("FAILED\n");}
                printf("OVER|17|D|Draw accepted.|\n");
                completionOfGame(game);
            }
            else if(strcmp(words[0],"R") == 0 && wordsSize == 1){
                char finT [] = "DRAW|2|R|";
                int finST = strlen(finT);
                if(write(game->sockOne,finT,finST) < 0){printf("FAILED\n");}
                sleep(0.2);
                char fin [] = "Player one rejects! (wait for / enter move)";
                int finS = strlen(fin);
                if(write(game->sockOne,fin,finS) < 0){printf("FAILED\n");}
                if(write(game->sockTwo,fin,finS) < 0){printf("FAILED\n");}
                game->drawState = 0;
            }
            else{
                char uno [] = "INVL|25|Please accept or reject.|";
                int unoS = strlen(uno);
                if(write(game->sockOne,uno,unoS) < 0){printf("FAILED\n");}
            }
        }
        pthread_mutex_unlock(&game->lock);
        if(wordsSize < 1){
            char enterM [] = "INVL|24|Please enter something!|";
            int enterSize = strlen(enterM);
            if(write(sock,enterM,enterSize) < 0){printf("FAILED\n");}
        }
        else if((strcmp(words[0],"PLAY") != 0 && stateOfGame == 0) || (strcmp(words[0],"PLAY") == 0 && strcmp(words[0],"") == 0)){ 
            char initMessage [] = "INVL|33|Please enter an initial command.|";
            int initMessageSize = strlen(initMessage);
            if(write(sock,initMessage,initMessageSize) < 0){printf("FAILED\n");}
        }
        else if(strcmp(words[0],"PLAY") == 0 && wordsSize == 1){
            char vM [] = "INVL|30|Please enter a valid command.|";
            int vmS = strlen(vM);
            if(write(sock,vM,vmS) < 0){printf("FAILED\n");}
        }
        else if(strcmp(words[0],"PLAY") == 0 && playCount == 0 && stateOfGame == 0){ //P1 has entered PLAY and P2 does not exist
            if(DEBUG)printf("---------------------------\n");
            if(DEBUG)printf("Name has been entered by p1\n");
            char nameString[100];

            nameString[0] = '\0';
            strcpy(nameString, words[1]);

            for(int printer = 1; printer < wordsSize; printer++) {
                if(printer > 1) {
                    strcat(nameString, words[printer]);
                }
                if(printer == (wordsSize - 1)) {
                }
                else {
                    strcat(nameString, " ");
                }
            }

            int checkIfNameExistsCode = 0; //1 if there is already a name
            for(int i = 0; i < 512; i ++){
                if(strcmp(nameString,globalNameList[i]) == 0){
                    checkIfNameExistsCode = 1;
                }
            }

            if(checkIfNameExistsCode == 1){
                char watchM [] = "INVL|41|Name is taken! Please enter another one.|";
                int watchMS = strlen(watchM);
                if(write(sock,watchM,watchMS) < 0){printf("write FAILED\n");}
            }
            else{
                for(int i = 0; i < 512; i ++){
                    if(strcmp(globalNameList[i],"-") == 0){
                        strcpy(globalNameList[i],nameString);
                        break;
                    }
                }
                if(DEBUG)printf("Namestring is %s\n", nameString);
                strcpy(game->playerOneName,nameString);
                printf("NAME|%ld|%s|\n", strlen(game->playerOneName) + 1, game->playerOneName);


                pthread_mutex_lock(&game->lock);
                game->gameState = 1;
                stateOfGame =  game->gameState;
                pthread_mutex_unlock( &game->lock);

                pthread_mutex_lock( &game->lock);
                if(DEBUG)printf("game state is %d\n", game->gameState);
                pthread_mutex_unlock( &game->lock);

                printf("WAIT|0|\n");
                char waitMessage [] = "WAIT";
                int waitMessageSize = strlen(waitMessage);
                if(write(sock,waitMessage,waitMessageSize) < 0){printf("write FAILED\n");}
                if(DEBUG)printf("---------------------------\n");
                playCount ++;
            }
        }
        else if(strcmp(words[0],"PLAY") == 0 && playCount != 0 && stateOfGame != 0){
            char playMessage [] = "INVL|32|Name has already been entered.|";
            int playMessagesize = strlen(playMessage);
            if(write(sock,playMessage,playMessagesize) < 0){printf("write FAILED\n");}
        }
        else if(stateOfGame == 1 && strcmp( game->clientTwo->domain,"") == 0){ //P1 has already entered PLAY and P2 does not exist and P1 tries to do something else
            if(DEBUG)printf("---------------------------\n");
            if(DEBUG)printf("In P1 has already typed PLAY\n");
            pthread_mutex_lock( &game->lock);
            if(DEBUG)printf("game state is %d\n", game->gameState);
            pthread_mutex_unlock( &game->lock);
            char playerCheckMessage [] = "INVL|34|Please wait for opponent to join.|";
            int playerCheckMessageSize = strlen(playerCheckMessage);
            if(write(sock,playerCheckMessage,playerCheckMessageSize) < 0){printf("FAILED\n");}
            if(DEBUG)printf("---------------------------\n");
        }
        else{ //IN PLAY STATE
            if(DEBUG)printf("----------------\n");
            if(DEBUG)printf("In p1 PLAY STAGE\n");

            if(strcmp(words[0],"PLAY") == 0){
                char playMessage [] = "INVL|30|Name has already been entered|";
                int playMessagesize = strlen(playMessage);
                if(write(sock,playMessage,playMessagesize) < 0){printf("write FAILED\n");}
            }
            else if(game->drawState == 0 && (game->drawRequestToken == 'X' || game->drawRequestToken == 'Y')){
                game->drawRequestToken = '\0';
            }
            else if(strcmp(words[0],"MOVE") == 0 && wordsSize == 2 && strlen(words[1]) == 3 && isdigit(words[1][0]) && isdigit(words[1][2]) && words[1][1] == ','){ //move command has been entered, process it
                if(DEBUG)printf("--------In P1 MOVE-----------\n");
                pthread_mutex_lock( &game->lock);
                int x = words[1][0] - '0';
                int y = words[1][2] - '0';

                if(DEBUG)printf("player turn is %d\n", game->playerTurn);

                if( game->playerTurn != NONE &&  game->playerTurn != PONE){
                    char waitForTurnMessage [] = "INVL|27|Please wait for your turn.|";
                    int waitForTurnSize = strlen(waitForTurnMessage);
                    if(write(sock,waitForTurnMessage,waitForTurnSize) < 0){printf("FAILED\n");}
                }
                else{
                    if( game->gameGrid[x][y] == 'X' ||  game->gameGrid[x][y] == 'O'){ //position already occupied
                        if(DEBUG)printf("at this position token is %c\n", game->gameGrid[x][y]);
                        char ooBPosMessage [] = "INVL|27|Position is already taken!|";
                        int ooBPosMessageSize = strlen(ooBPosMessage);
                        if(write(sock,ooBPosMessage,ooBPosMessageSize) < 0){printf("FAILED\n");}
                    }
                    else if(x > 2 || y > 2){ //out of bounds position
                        char ooBPosMessage [] = "INVL|27|Position is out of bounds.|";
                        int ooBPosMessageSize = strlen(ooBPosMessage);
                        if(write(sock,ooBPosMessage,ooBPosMessageSize) < 0){printf("FAILED\n");}
                    }
                    else{ //valid position
                        if(DEBUG)printf("IN P1 VALID POS\n");

                        printf("MOVE|6|X|%d,%d|", x, y);   

                        //making movd log
                        char movdGrid[25];
                        movdGrid[0] = 'M';
                        movdGrid[1] = 'O';
                        movdGrid[2] = 'V';
                        movdGrid[3] = 'D';
                        movdGrid[4] = '|';
                        movdGrid[5] = '1';
                        movdGrid[6] = '6';
                        movdGrid[7] = '|';
                        movdGrid[8] = 'X';
                        movdGrid[9] = '|';
                        movdGrid[10] = x+'0';
                        movdGrid[11] = ',';
                        movdGrid[12] = y+'0';
                        movdGrid[13] = '|';
                        int startIndexMovd = 14;
                        for(int r = 0; r < 3; r ++){
                            for(int c = 0; c < 3; c ++){
                                if(r == x && c == y){
                                    movdGrid[startIndexMovd] = 'X';
                                }
                                else if(game->gameGrid[r][c] == '-'){
                                    movdGrid[startIndexMovd] = '.';
                                }
                                else{
                                    movdGrid[startIndexMovd] = game->gameGrid[r][c];
                                }
                                startIndexMovd ++;
                            }
                        }
                        movdGrid[23] = '|';
                        movdGrid[24] = '\0';

                        int movdGridSize = strlen(movdGrid);
                        if(write( game->sockOne,movdGrid,movdGridSize) < 0){printf("FAILED\n");}
                        if(write( game->sockTwo,movdGrid,movdGridSize) < 0){printf("FAILED\n");}
                        printf("\n");
                        //end of movd log
                        
                        char ch = 'o';
                        int victoryCode = enterPosAndCheckVictory( game->gameGrid,ch,x,y);
                        if(victoryCode == 1){
                            //do game shut down since p1 has won
                            game->exitCondition = 1;
                            char printM [] = "===Current GameGrid===";
                            int printMSize = strlen(printM);
                            if(write( game->sockOne,printM,printMSize) < 0){printf("FAILED\n");}
                            if(write( game->sockTwo,printM,printMSize) < 0){printf("FAILED\n");}
                            printGameGrid( game->gameGrid, game->sockOne, game->sockTwo);
                            char printMTwo [] = "======================";
                            int printMSizeTwo = strlen(printMTwo);
                            if(write( game->sockOne,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                            if(write( game->sockTwo,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                            sleep(0.2);
                            char oneWinnerMessage [] = "OVER|22|W|Player one has won!|";
                            int oneWinMessageSize = strlen(oneWinnerMessage);
                            char oneWinnerMessageT [] = "OVER|22|L|Player one has won!|";
                            if(write( game->sockOne,oneWinnerMessage,oneWinMessageSize) < 0){printf("FAILED\n");}
                            if(write( game->sockTwo,oneWinnerMessageT,oneWinMessageSize) < 0){printf("FAILED\n");}
                            sleep(0.2);
                            //cleanup
                            completionOfGame(game);
                        }
                        else{
                            int tieCode = tieChecker( game->gameGrid);
                            if(tieCode == 1){ //there has not been a tie
                                char printM [] = "===Current GameGrid===";
                                int printMSize = strlen(printM);
                                if(write( game->sockOne,printM,printMSize) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,printM,printMSize) < 0){printf("FAILED\n");}
                                sleep(0.2);
                                //print gameGrid
                                printGameGrid( game->gameGrid, game->sockOne, game->sockTwo);
                                sleep(0.2);
                                char printMTwo [] = "======================";
                                int printMSizeTwo = strlen(printMTwo);
                                if(write( game->sockOne,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                            }
                            else{ //there has been a tie
                                char printM [] = "===Current GameGrid===";
                                int printMSize = strlen(printM);
                                if(write( game->sockOne,printM,printMSize) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,printM,printMSize) < 0){printf("FAILED\n");}
                                sleep(0.2);
                                //print gameGrid
                                printGameGrid( game->gameGrid, game->sockOne, game->sockTwo);
                                sleep(0.2);
                                char printMTwo [] = "======================";
                                int printMSizeTwo = strlen(printMTwo);
                                if(write( game->sockOne,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                                sleep(0.2);
                                char tieMessage [] = "OVER|24|D|There has been a tie!|";
                                int tieMessageSize = strlen(tieMessage);
                                if(write( game->sockOne,tieMessage,tieMessageSize) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,tieMessage,tieMessageSize) < 0){printf("FAILED\n");}
                                printf("DRAW through game play\n");
                                //do game shut down since tie
                                completionOfGame(game);
                            }
                             game->playerTurn = PTWO;
                        }
                    }
                }
                pthread_mutex_unlock( &game->lock);
                if(DEBUG)printf("-----------------------------\n");
            }
            else if(strcmp(words[0],"RESIGN") == 0 && wordsSize < 2){ //user wants to resign
                if(DEBUG)printf("-------------------\n");
                if(DEBUG)printf("in the p1 resign command\n");
                if(DEBUG)printf("-------------------\n");
                printf("RSGN\n");
                char endM [] = "OVER|27|W|Player one has resigned!|";
                int endMSize = strlen(endM);
                if(write( game->sockTwo,endM,endMSize) < 0){printf("FAILED\n");}
                completionOfGame(game);
            }
            else if(wordsSize == 2 && strcmp(words[0],"DRAW") == 0 && strcmp(words[1],"S") == 0){
                pthread_mutex_lock(&game->lock);
                char endMU [] = "DRAW|2|S|";
                int endMSizeU = strlen(endMU);
                if(write( game->sockTwo,endMU,endMSizeU) < 0){printf("FAILED\n");}
                char endM [] = "Player one has requested draw, hit A to accept or R to reject";
                int endMSize = strlen(endM);
                if(write( game->sockTwo,endM,endMSize) < 0){printf("FAILED\n");}
                game->drawState = 1;
                game->drawRequestToken = 'X';
                pthread_mutex_unlock( &game->lock);
            }
            else{
                printf("INVL|27|Incorrect command entered.|\n");
                char validMessage [] = "INVL|30|Please enter a valid command.|";
                int validMessageSize = strlen(validMessage);
                if(write(sock,validMessage,validMessageSize) < 0){printf("write FAILED\n");}
            }
            if(DEBUG)printf("----------------\n");
        } 
        memset(buf, '\0', sizeof(buf));
    } //end of read while loop

    if (bytes == 0){
        printf("[%s:%s] got EOF\n", host, port);
    } 
    else if (bytes == -1){
        printf("[%s:%s] terminating:\n", host, port);
    } 
    else{
        printf("[%s:%s] terminating\n", host, port);
    }
    close(sock);
}

void *connection_handler_player_Two(void *arg){
    struct Game* game = (struct Game *) arg;
    int sock =  game->sockTwo;
    if(DEBUG){
        printf("IN THREAD FOR CLIENT 2\n");
    }

    pthread_mutex_lock( &game->lock);
    game->threadIDTwo = pthread_self();
    pthread_mutex_unlock( &game->lock);

    char message [] = "Welcome to the TicTacToe game!";
    int messageSize = strlen(message);

    char messageTwo[] = "Here are the rules:";
    int messageTwoSize = strlen(messageTwo);

    char messageThree[] = "PLAY ____ => enter your name";
    int messageThreeSize = strlen(messageThree);

    char messageFour[] = "MOVE X,Y => move your token to position (starting at indices 0,0)";
    int messageFourSize = strlen(messageFour);

    char messageFive[] = "RESIGN => leave the game!";
    int messageFiveSize = strlen(messageFive);

    char messageSix[] = "DRAW S => suggest a draw";
    int messageSixSize = strlen(messageSix);

    char messageSev[] = "When game is done, hit enter multiple times to exit";
    int messageSevSize = strlen(messageSev);

    if(write(sock,message,messageSize) < 0){printf("write FAILED\n");}
    sleep(1);
    if(write(sock,messageTwo,messageTwoSize) < 0){printf("write FAILED\n");}
    sleep(1);
    if(write(sock,messageThree,messageThreeSize) < 0){printf("write FAILED\n");}
    sleep(1);
    if(write(sock,messageFour,messageFourSize) < 0){printf("write FAILED\n");}
    sleep(1);
    if(write(sock,messageFive,messageFiveSize) < 0){printf("write FAILED\n");}
    sleep(1);
    if(write(sock,messageSix,messageSixSize) < 0){printf("write FAILED\n");}
    sleep(1);
    if(write(sock,messageSev,messageSevSize) < 0){printf("write FAILED\n");}

    char buf[BUFSIZE + 1], host[HOSTSIZE], port[PORTSIZE];
    int bytes, error;

    int playCount = 0;

    if(DEBUG)printf("IN P2 thread with game status %d\n", game->gameState);

    memset(buf, '\0', sizeof(buf));
    while (active && (((bytes = (read(sock, buf, BUFSIZE))) > 0))){
        buf[bytes - 1] = '\0';
        if(DEBUG)printf("READING string is %s\n",buf);
        char *words[256];
        for(int i = 0; i < 256; i ++){
            words[i] = NULL;
        }
        words[0] = '\0';
        int i = 0;
        int wordsSize = 0;
        char *token;
        token = strtok(buf," ");
        while(token != NULL){
            words[i] = token;
            i ++;
            wordsSize ++;
            token = strtok(NULL, " ");
        }

        int stateOfGame = 0;
        pthread_mutex_lock( &game->lock);
        stateOfGame =  game->gameState;
        pthread_mutex_unlock( &game->lock);

        pthread_mutex_lock(&game->lock);
        if(game->drawState == 1 && game->drawRequestToken == 'X'){
            if(strcmp(words[0],"A") == 0 && wordsSize == 1){
                char finT [] = "OVER|17|D|Draw accepted.|";
                int finST = strlen(finT);
                if(write(game->sockOne,finT,finST) < 0){printf("FAILED\n");}
                if(write(game->sockTwo,finT,finST) < 0){printf("FAILED\n");}
                sleep(0.2);
                char fin [] = "Draw accepted, game over!";
                int finS = strlen(fin);
                if(write(game->sockOne,fin,finS) < 0){printf("FAILED\n");}
                if(write(game->sockTwo,fin,finS) < 0){printf("FAILED\n");}
                completionOfGame(game);
            }
            else if(strcmp(words[0],"R") == 0 && wordsSize == 1){
                char finW [] = "DRAW|2|R";
                int finSW = strlen(finW);
                if(write(game->sockOne,finW,finSW) < 0){printf("FAILED\n");}
                sleep(0.2);
                char fin [] = "Player two rejects! (wait for / enter move)";
                int finS = strlen(fin);
                if(write(game->sockOne,fin,finS) < 0){printf("FAILED\n");}
                if(write(game->sockTwo,fin,finS) < 0){printf("FAILED\n");}
                game->drawState = 0;
            }
            else{
                char uno [] = "Please accept or reject";
                int unoS = strlen(uno);
                if(write(game->sockOne,uno,unoS) < 0){printf("FAILED\n");}
            }
        }
        pthread_mutex_unlock(&game->lock);
    
        //P2 has joined, is in the wait state, but has not entered play
        if(wordsSize < 1){
            char enterM [] = "INVL|23|Please enter something!|";
            int enterSize = strlen(enterM);
            if(write(sock,enterM,enterSize) < 0){printf("FAILED\n");}
        }
        else if(stateOfGame == 0){
            char pleaseWaitMessage [] = "INVL|34|Please wait for opponent to join.|";
            int pleaseWaitSize = strlen(pleaseWaitMessage);
            if(write(sock,pleaseWaitMessage,pleaseWaitSize) < 0){printf("FAILED\n");}
        }
        else if(stateOfGame == 1 && strcmp(words[0],"PLAY") != 0){
            char enterInitCommandMessage [] = "INVL|33|Please enter an initial command.|";
            int initCommandMessagesize = strlen(enterInitCommandMessage);
            if(write(sock,enterInitCommandMessage,initCommandMessagesize) < 0){printf("FAILED\n");}
        }
        else if(strcmp(words[0],"PLAY") == 0 && wordsSize == 1){
            char vM [] = "INVL|30|Please enter a valid command.|";
            int vmS = strlen(vM);
            if(write(sock,vM,vmS) < 0){printf("FAILED\n");}
        }
        else if(stateOfGame == 1 && strcmp(words[0],"PLAY") == 0 && playCount == 0){
            if(DEBUG)printf("---------------------------\n");
            if(DEBUG)printf("Name has been entered by p2\n");

            char nameString[100];

            nameString[0] = '\0';
            strcpy(nameString, words[1]);

            for(int printer = 1; printer < wordsSize; printer++) {
                if(printer > 1) {
                    strcat(nameString, words[printer]);
                }
                if(printer == (wordsSize - 1)) {
                }
                else {
                    strcat(nameString, " ");
                }
            }

            int checkIfNameExistsCode = 0; //1 if there is already a name
            for(int i = 0; i < 512; i ++){
                if(strcmp(nameString,globalNameList[i]) == 0){
                    checkIfNameExistsCode = 1;
                }
            }

            if(checkIfNameExistsCode == 1){
                char watchM [] = "INVL|15|Name is taken!|";
                int watchMS = strlen(watchM);
                if(write(sock,watchM,watchMS) < 0){printf("write FAILED\n");}
            }
            else{
                for(int i = 0; i < 512; i ++){
                    if(strcmp(globalNameList[i],"-") == 0){
                        strcpy(globalNameList[i],nameString);
                        break;
                    }
                }

                if(DEBUG)printf("Namestring is %s\n", nameString);
                strcpy(game->playerTwoName,nameString);
                printf("NAME|%ld|%s|\n", strlen(game->playerTwoName) + 1, game->playerTwoName);


                pthread_mutex_lock( &game->lock);
                game->gameState = 2;
                stateOfGame =  game->gameState;
                pthread_mutex_unlock( &game->lock);

                pthread_mutex_lock( &game->lock);
                if(DEBUG)printf("game state is %d\n", game->gameState);
                pthread_mutex_unlock( &game->lock);

                printf("WAIT|0|\n");
                char waitMessage [] = "WAIT";
                int waitMessageSize = strlen(waitMessage);
                if(write(sock,waitMessage,waitMessageSize) < 0){printf("write FAILED\n");}
                sleep(1);

                //creating first message
                int beginMSize = 7 + strlen(game->playerOneName)+1;
                char beginMessageOne [500];
                beginMessageOne[0] = '\0';
                beginMessageOne[0] = 'B';
                beginMessageOne[1] = 'E';
                beginMessageOne[2] = 'G';
                beginMessageOne[3] = 'N';
                beginMessageOne[4] = ' ';
                beginMessageOne[5] = 'O';
                beginMessageOne[6] = ' ';
                int mesIndex = 0;
                for(int i = 7; i < 7 + strlen(game->playerOneName); i ++){
                    beginMessageOne[i] = game->playerOneName[mesIndex];
                    mesIndex ++;
                }
                beginMessageOne[beginMSize-1] = '\0';
                //end of making first message

                char beginMessageTwo [8 + strlen(game->playerTwoName)];
                snprintf(beginMessageTwo, sizeof(beginMessageTwo), "BEGN X %s", game->playerTwoName);
                int beginTwoSize = strlen(beginMessageTwo);

                printf("BEGN|2|X|\n");
                printf("BEGN|2|O|\n");

                if(write( game->sockOne,beginMessageTwo,beginTwoSize) < 0){printf("write FAILED\n");}
                if(write( game->sockTwo,beginMessageOne,beginMSize) < 0){printf("write FAILED\n");}
                
                if(DEBUG)printf("---------------------------\n");
                playCount ++;
            }
        } 
        else if(stateOfGame == 2 && strcmp(words[0],"PLAY") == 0 && playCount != 0){
            char playMessage [] = "INVL|31|Name has already been entered.|";
            int playMessagesize = strlen(playMessage);
            if(write(sock,playMessage,playMessagesize) < 0){printf("write FAILED\n");}
        }
        else{ //IN PLAY STATE
            if(DEBUG)printf("----------------\n");
            if(DEBUG)printf("In p2 PLAY STAGE with words[0] : %s\n",words[0]);
            if(DEBUG)printf("----------------\n");

            if(strcmp(words[0],"PLAY") == 0){
                char playMessage [] = "INVL|31|Name has already been entered.|";
                int playMessagesize = strlen(playMessage);
                if(write(sock,playMessage,playMessagesize) < 0){printf("write FAILED\n");}
            }
            else if(game->drawState == 0 && (game->drawRequestToken == 'X' || game->drawRequestToken == 'Y')){
                game->drawRequestToken = '\0';
            }
            else if(strcmp(words[0],"MOVE") == 0 && wordsSize == 2 && strlen(words[1]) == 3 && isdigit(words[1][0]) && isdigit(words[1][2]) && words[1][1] == ','){ //move command has been entered, process it
                if(DEBUG)printf("--------In P2 MOVE-----------\n");
                pthread_mutex_lock( &game->lock);
                int x = words[1][0] - '0';
                int y = words[1][2] - '0';

                if(DEBUG)printf("player turn is %d\n", game->playerTurn);

                if( game->playerTurn != NONE &&  game->playerTurn != PTWO){
                    char waitForTurnMessage [] = "INVL|27|Please wait for your turn.|";
                    int waitForTurnSize = strlen(waitForTurnMessage);
                    if(write(sock,waitForTurnMessage,waitForTurnSize) < 0){printf("FAILED\n");}
                }
                else{
                    if( game->gameGrid[x][y] == 'X' ||  game->gameGrid[x][y] == 'O'){ //position already occupied
                        if(DEBUG)printf("at this position token is %c\n", game->gameGrid[x][y]);
                        char ooBPosMessage [] = "INVL|24|Position already taken!|";
                        int ooBPosMessageSize = strlen(ooBPosMessage);
                        if(write(sock,ooBPosMessage,ooBPosMessageSize) < 0){printf("FAILED\n");}
                    }
                    else if(x > 2 || y > 2){ //out of bounds position
                        char ooBPosMessage [] = "INVL|33|Given position is out of bounds.|";
                        int ooBPosMessageSize = strlen(ooBPosMessage);
                        if(write(sock,ooBPosMessage,ooBPosMessageSize) < 0){printf("FAILED\n");}
                    }
                    else{ //valid position
                        if(DEBUG)printf("IN P2 VALID POS\n");

                        printf("MOVE|6|O|%d,%d|", x, y);
                        //making movd log
                        char movdGrid[25];
                        movdGrid[0] = 'M';
                        movdGrid[1] = 'O';
                        movdGrid[2] = 'V';
                        movdGrid[3] = 'D';
                        movdGrid[4] = '|';
                        movdGrid[5] = '1';
                        movdGrid[6] = '6';
                        movdGrid[7] = '|';
                        movdGrid[8] = 'O';
                        movdGrid[9] = '|';
                        movdGrid[10] = x+'0';
                        movdGrid[11] = ',';
                        movdGrid[12] = y+'0';
                        movdGrid[13] = '|';
                        int startIndexMovd = 14;
                        for(int r = 0; r < 3; r ++){
                            for(int c = 0; c < 3; c ++){
                                if(r == x && c == y){
                                    movdGrid[startIndexMovd] = 'O';
                                }
                                else if(game->gameGrid[r][c] == '-'){
                                    movdGrid[startIndexMovd] = '.';
                                }
                                else{
                                    movdGrid[startIndexMovd] = game->gameGrid[r][c];
                                }
                                startIndexMovd ++;
                            }
                        }
                        movdGrid[23] = '|';
                        movdGrid[24] = '\0';

                        int movdGridSize = strlen(movdGrid);
                        if(write( game->sockOne,movdGrid,movdGridSize) < 0){printf("FAILED\n");}
                        if(write( game->sockTwo,movdGrid,movdGridSize) < 0){printf("FAILED\n");}
                        printf("\n");
                        //end of movd log

                        char ch = 't';
                        int victoryCode = enterPosAndCheckVictory( game->gameGrid,ch,x,y);
                        if(victoryCode == 1){
                            //do game shut down since p2 has won
                            char printM [] = "===Current GameGrid===";
                            int printMSize = strlen(printM);
                            if(write( game->sockOne,printM,printMSize) < 0){printf("FAILED\n");}
                            if(write( game->sockTwo,printM,printMSize) < 0){printf("FAILED\n");}
                            sleep(0.2);
                            printGameGrid( game->gameGrid, game->sockOne, game->sockTwo);
                            sleep(0.2);
                            char printMTwo [] = "======================";
                            int printMSizeTwo = strlen(printMTwo);
                            if(write( game->sockOne,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                            if(write( game->sockTwo,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}\
                            sleep(0.2);
                            char twoWinnerMessage [] = "OVER|22|W|Player two has won!|";
                            int twoWinMessageSize = strlen(twoWinnerMessage);
                            char twoWinnerMessageT [] = "OVER|22|L|Player two has won!|";
                            if(write( game->sockOne,twoWinnerMessageT,twoWinMessageSize) < 0){printf("FAILED\n");}
                            if(write( game->sockTwo,twoWinnerMessage,twoWinMessageSize) < 0){printf("FAILED\n");}
                            sleep(0.2);
                            completionOfGame(game);
                        }
                        else{
                            int tieCode = tieChecker( game->gameGrid);
                            if(tieCode == 1){ //there has not been a tie
                                char printM [] = "===Current GameGrid===";
                                int printMSize = strlen(printM);
                                if(write( game->sockOne,printM,printMSize) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,printM,printMSize) < 0){printf("FAILED\n");}
                                sleep(0.2);
                                printGameGrid( game->gameGrid, game->sockOne, game->sockTwo);
                                sleep(0.2);
                                char printMTwo [] = "======================";
                                int printMSizeTwo = strlen(printMTwo);
                                if(write( game->sockOne,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                            }
                            else{ //there has been a tie
                                char printM [] = "===Current GameGrid===";
                                int printMSize = strlen(printM);
                                if(write( game->sockOne,printM,printMSize) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,printM,printMSize) < 0){printf("FAILED\n");}
                                sleep(0.2);
                                printGameGrid( game->gameGrid, game->sockOne, game->sockTwo);
                                sleep(0.2);
                                char printMTwo [] = "======================";
                                int printMSizeTwo = strlen(printMTwo);
                                if(write( game->sockOne,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,printMTwo,printMSizeTwo) < 0){printf("FAILED\n");}
                                sleep(0.2);
                                char tieMessage [] = "OVER|24|D|There has been a tie!|";
                                int tieMessageSize = strlen(tieMessage);
                                if(write( game->sockOne,tieMessage,tieMessageSize) < 0){printf("FAILED\n");}
                                if(write( game->sockTwo,tieMessage,tieMessageSize) < 0){printf("FAILED\n");}
                                //do game shut down since tie
                                completionOfGame(game);
                            }
                             game->playerTurn = PONE;
                        }
                    }
                }
                pthread_mutex_unlock( &game->lock);
                if(DEBUG)printf("-----------------------------\n");
            }
            else if(strcmp(words[0],"RESIGN") == 0 && wordsSize < 2){ //user wants to resign
                printf("RSGN\n");
                char endM [] = "OVER|27|W|Player two has resigned!|";
                int endMSize = strlen(endM);
                if(write( game->sockOne,endM,endMSize) < 0){printf("FAILED\n");}
                completionOfGame(game);
                if(DEBUG)printf("-------------------\n");
                if(DEBUG)printf("in the p2 resign command\n");
                if(DEBUG)printf("-------------------\n");
            }
            else if(wordsSize == 2 && strcmp(words[0],"DRAW") == 0 && strcmp(words[1],"S") == 0){
                pthread_mutex_lock(&game->lock);
                char endMF [] = "DRAW|2|S|";
                int endMSizeF = strlen(endMF);
                if(write(game->sockOne,endMF,endMSizeF) < 0){printf("FAILED\n");}
                sleep(0.2);
                char endM [] = "Player two has requested draw, hit A to accept or R to reject";
                int endMSize = strlen(endM);
                if(write(game->sockOne,endM,endMSize) < 0){printf("FAILED\n");}
                game->drawState = 1;
                game->drawRequestToken = 'O';
                pthread_mutex_unlock(&game->lock);
            }
            else{
                char validMessage [] = "INVL|30|Please enter a valid command.|";
                int validMessageSize = strlen(validMessage);
                if(write(sock,validMessage,validMessageSize) < 0){printf("write FAILED\n");}
            }
        }
        memset(buf, '\0', sizeof(buf));
    } //end of read while loop

    if (bytes == 0){
        printf("[%s:%s] got EOF\n", host, port);
    } 
    else if (bytes == -1){
        printf("[%s:%s] terminating:\n", host, port);
    } 
    else{
        printf("[%s:%s] terminating\n", host, port);
    }
    close(sock);
}

int enterPosAndCheckVictory(char gameGrid [3][3], char oneOrTwo,int x,int y){
    //1 means victory
    if(oneOrTwo == 'o'){
        //check for X
        gameGrid[x][y] = 'X';
        char token = 'X';
        int victoryCode = checkForVictory(gameGrid,token);
        if(victoryCode == 1){
            return 1;
        }
        else{
            return 0;
        }
    }
    else{
        //check for O
        gameGrid[x][y] = 'O';
        char token = 'O';
        int victoryCode = checkForVictory(gameGrid,token);
        if(victoryCode == 1){
            return 1;
        }
        else{
            return 0;
        }
    }
}

int checkForVictory(char gameGrid[3][3], char xOrO){
    //1 means victory has been found
    if(gameGrid[0][0] == xOrO && gameGrid[0][1] == xOrO && gameGrid[0][2] == xOrO){ //1st row
        return 1;
    }
    else if(gameGrid[1][0] == xOrO && gameGrid[1][1] == xOrO && gameGrid[1][2] == xOrO){ //second row
        return 1;
    }
    else if(gameGrid[2][0] == xOrO && gameGrid[2][1] == xOrO && gameGrid[2][2] == xOrO){ //third row
        return 1;
    }
    else if(gameGrid[0][0] == xOrO && gameGrid[1][0] == xOrO && gameGrid[2][0] == xOrO){ //1st col
        return 1;
    }
    else if(gameGrid[0][1] == xOrO && gameGrid[1][1] == xOrO && gameGrid[2][1] == xOrO){ //2nd col
        return 1;
    }
    else if(gameGrid[0][2] == xOrO && gameGrid[1][2] == xOrO && gameGrid[2][2] == xOrO){ //3rd col
        return 1;
    }
    else if(gameGrid[0][0] == xOrO && gameGrid[1][1] == xOrO && gameGrid[2][2] == xOrO){ //diagonal starting at 0,0
        return 1;
    }
    else if(gameGrid[0][2] == xOrO && gameGrid[1][1] == xOrO && gameGrid[2][0] == xOrO){ //diagonal starting at 0,2
        return 1;
    }
    else{ //no victory
        return 0;
    }
}

void printGameGrid(char gameGrid[3][3],int sockOne, int sockTwo){
    char newLine[] = "\n";
    int newLineSize = strlen(newLine);

    for(int r = 0; r < 3; r ++){
        if(write(sockOne,gameGrid[r],3) < 0){printf("write FAILED\n");}
        sleep(0.2);
        //if(write(sockOne,newLine,newLineSize) < 0){printf("write FAILED\n");}
        if(write(sockTwo,gameGrid[r],3) < 0){printf("write FAILED\n");}
        sleep(0.2);
        //if(write(sockTwo,newLine,newLineSize) < 0){printf("write FAILED\n");}
    }
}

int tieChecker(char gameGrid[3][3]){
    //1 returned => there is a - token in the grid, otherwise there is a tie
    for(int r = 0; r < 3; r ++){
        for(int c = 0; c < 3; c ++){
            if(gameGrid[r][c] == '-'){
                return 1;
            }
        }
    }
    return 0;
}

struct Game* initializeGame(){
    char gameGrid[3][3];
    for(int r = 0; r < 3; r ++){
        for(int c = 0; c < 3; c ++){
            gameGrid[r][c] = '-';
        }
    }

    struct Game *game = (struct Game*) malloc(sizeof(struct Game));

    struct Client *clientOne = (struct Client*) malloc(sizeof(struct Client));
    struct Client *clientTwo = (struct Client*) malloc(sizeof(struct Client));


    game->clientOne = clientOne;
    game->clientTwo = clientTwo;

    if(pthread_mutex_init(&game->lock,NULL)!=0){
        perror("Mutex failed\n");
    }

    memcpy(game->gameGrid,gameGrid,sizeof(gameGrid));
    game->clientOne->clientToken = '\0';
    game->clientOne->domain[0] = '\0';
    game->clientOne->port[0] = '\0';
    game->clientTwo->clientToken = '\0';
    game->clientTwo->domain[0] = '\0';
    game->clientTwo->port[0] = '\0';
    game->drawRequestToken = '\0';
    game->playerOneName[0] = '\0';
    game->playerTwoName[0] = '\0';
    game->gameState = 0;
    game->sockOne = 0;
    game->sockTwo = 0;
    game->playerTurn = NONE;
    game->threadIDOne = 0;
    game->threadIDTwo = 0;
    game->exitCondition = 0;
    game->clientOneOrTwo = malloc(4*sizeof(char));
    game->drawState = 0;

    return game;
}

void resetGameStruct(struct Game* game){
    for(int r = 0; r < 3; r ++){
        for(int c = 0; c < 3; c ++){
            game->gameGrid[r][c] = '-';
        }
    }
    game->clientOne->clientToken = '\0';
    game->clientOne->domain[0] = '\0';
    game->clientOne->port[0] = '\0';
    game->clientOne->clientToken = '\0';
    game->clientTwo->domain[0] = '\0';
    game->clientTwo->port[0] = '\0';
    game->playerOneName[0] = '\0';
    game->playerTwoName[0] = '\0';
    game->gameState = 0;
    game->sockOne = 0;
    game->sockTwo = 0;
    game->drawRequestToken = '\0';
    game->playerTurn = NONE;
    game->threadIDOne = 0;
    game->threadIDTwo = 0;
    game->exitCondition = 0;
    game->drawState = 0;
    strcpy(game->clientOneOrTwo,"\0");
}

void freeGameStruct(struct Game* game){
    free(game->clientOneOrTwo);
    free(game->clientOne);
    free(game->clientTwo);
    free(game);
    pthread_mutex_destroy(&game->lock);
}

void completionOfGame(struct Game* game){
    //clear data structure
    //close both sockets
    close( game->sockOne);
    close( game->sockTwo);
    //exit the current thread, kill the other
    pthread_cancel( game->threadIDTwo);
    resetGameStruct(game);
    pthread_exit(NULL);
}