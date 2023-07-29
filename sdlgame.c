#include <stdio.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <memory.h>
#include <stdlib.h>

#include "SDL2/SDL.h"
#include "SDL2/SDL_image.h"
// #include "SDL.h"
// #include "SDL_image.h"
#include "Lua/lua.h"
#include "Lua/lauxlib.h"

#pragma region 

const char* spriteSheetFile = "cavetiles.png";
const char* levelFile = "CavesAutomapTest.lua";
const int gridSize = 32;
const float scaling = 2.0f;
const int viewRows = 12;
const int viewColumns = 16;
#define MAX_COLUMNS 96
#define MAX_ROWS 48
#define MAX_PATH 100
#define MAX_MOBS 100
#define MAX_ITEMS 100
#define MAX_ENEMIES 10
#define MAX_ACTIONS 100

const float moveSpeed = 100.0f / 32;
const int targetAC = 12;
const int enemyMaxMove = 3.0f;
const float playerMaxMove = 3.0f;
const int aggroRadius = 5.0f;

enum LayerIndex
{
    LAYER_GROUND,
    LAYER_WALLS,
    LAYER_PROPS,
    LAYER_MOBS,
    LAYER_ITEMS,
    LAYER_TOP,
    LAYER_COLLISION
};

enum SpriteIndex
{
    SPRITE_INACCESSIBLE = 7,
    SPRITE_PLAYERIDLE = 13,
    SPRITE_ORC = 21,
    SPRITE_MOVETO = 22,
    SPRITE_PATHDOT = 30,
    SPRITE_PLAYERMOVE1 = 37,
    SPRITE_ATTACK = 38,
    SPRITE_PLAYERMOVE2 = 45,
};

typedef enum ActionType
{
    ACTION_NONE,
    ACTION_MOVE,
    ACTION_ATTACK,
} ActionType;

typedef struct Action
{
    ActionType tp;
    union 
    {
        SDL_Point to;
        struct Sprite* target;
    } obj;
} Action;

enum GameState
{
    GAME_EXPLORE,
    GAME_COMBAT_PLAYERINPUT,
    GAME_COMBAT_PLAYERRESOLVE,
    GAME_COMBAT_ENEMYAI,
    GAME_COMBAT_ENEMYRESOLVE,
} gameState = GAME_EXPLORE;

const int playerSprites[] = {SPRITE_PLAYERIDLE, SPRITE_PLAYERMOVE1, SPRITE_PLAYERMOVE2};

typedef struct Sprite
{
    SDL_Point pos;
    SDL_Point offset;
    int spriteIndex;
    float animIndex;
    int hp;
    int AC;
} Sprite;

Sprite player;
Sprite mobs[MAX_MOBS];
int nbMobs = 0;
Sprite items[MAX_ITEMS];
int nbItems = 0;
bool isColliding[MAX_COLUMNS][MAX_ROWS] = {false};
SDL_Renderer* renderer = NULL;
SDL_Texture* spriteSheetTexture = NULL;
SDL_Texture* backgroundTexture = NULL;
SDL_Texture* foregroundTexture = NULL;
Sprite* combatEnemies[MAX_ENEMIES];
int nbCombatEnemies = 0;

Action currentAction;
Action actionQueue[MAX_ACTIONS];
int actionHead = 0;
int actionTail = 0;
float actionProgress = 0.0f;

void SpriteInit(Sprite* sprite, int x, int y, int spriteIndex, bool collides)
{
    sprite->pos.x = x;
    sprite->pos.y = y;
    sprite->offset.x = 0;
    sprite->offset.y = -16;
    sprite->spriteIndex = spriteIndex;
    sprite->animIndex = 0;
    // sprite->currentAction.tp = ACTION_NONE;
    // sprite->actionHead = 0;
    // sprite->actionTail = 0;
    sprite->hp = 8;
    sprite->AC = 13;
    if(collides) isColliding[x][y] = true;
}

int get_int_at_key(lua_State* L, const char* key)
{
    lua_pushstring(L, key);
    int r = lua_gettable(L, -2);
    assert(r == LUA_TNUMBER);
    r = lua_tointeger(L, -1);
    lua_pop(L, 1); // pop the result
    return r;
}

// int comp_str_at_key(lua_State* L, const char* key, const char* str)
// {
//     lua_pushstring(L, key);
//     int r = lua_gettable(L, -2);
//     assert(r == LUA_TSTRING);
//     int res = (strcmp(lua_tostring(L, -1), str) == 0);
//     lua_pop(L, 1); // pop the string
//     return res;
// }

void RenderSpriteIndex(SDL_Renderer* renderer, SDL_Texture* texture, int spriteIndex, const SDL_Rect* dstrect)
{
    const int spriteSheetRows = 8;
    const int spriteSheetColumns = 8;
    if(spriteIndex > 0)
    {
        SDL_Rect srcrect = {
            ((spriteIndex - 1) % spriteSheetColumns) * gridSize, 
            ((spriteIndex - 1) / spriteSheetColumns) * gridSize, 
            gridSize, 
            gridSize
        };
        SDL_RenderCopy(renderer, texture, &srcrect, dstrect);
    }
}

bool LoadLualevel()
{
    lua_State* L = luaL_newstate();
    if(luaL_dofile(L, levelFile) == LUA_OK)
    {
        int r = lua_gettop(L);

        int width = get_int_at_key(L, "width");
        assert(width == MAX_COLUMNS);
        int height = get_int_at_key(L, "height");
        assert(height == MAX_ROWS);

        lua_pushstring(L, "layers");
        r = lua_gettable(L, -2);
        assert(r == LUA_TTABLE);

        int nb_layers = luaL_len(L, -1);

        for(int i = 0; i < nb_layers; i++) 
        {
            r = lua_geti(L, -1, i + 1); // 1-based
            assert(r == LUA_TTABLE);

            lua_pushstring(L, "data");
            int data = lua_gettable(L, -2);
            assert(r == LUA_TTABLE);

            switch (i)
            {
            case LAYER_GROUND:
                backgroundTexture = SDL_CreateTexture(
                    renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 
                    MAX_COLUMNS * gridSize, MAX_ROWS * gridSize);
            case LAYER_WALLS:
            case LAYER_PROPS:
                SDL_SetRenderTarget(renderer, backgroundTexture);
                break;
            case LAYER_TOP:
                foregroundTexture = SDL_CreateTexture(
                    renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 
                    MAX_COLUMNS * gridSize, MAX_ROWS * gridSize);
                SDL_SetTextureBlendMode(foregroundTexture, SDL_BLENDMODE_BLEND);
                SDL_SetRenderTarget(renderer, foregroundTexture);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);
                break;
            default:
                break;
            }

            int data_len = luaL_len(L, -1);
            for(int j = 1; j <= data_len; j++) // 1-based
            {
                r = lua_geti(L, -1, j); 
                assert(r == LUA_TNUMBER);
                r = lua_tointeger(L, -1);

                int x = (j - 1) % MAX_COLUMNS;
                int y = (j - 1) / MAX_COLUMNS;
                SDL_Rect dstrect = {x * gridSize, y * gridSize, gridSize, gridSize};

                switch (i)
                {
                case LAYER_MOBS:
                    if(r == SPRITE_PLAYERIDLE)
                    {
                        SpriteInit(&player, x, y, r, true);
                    }
                    else if(r == SPRITE_ORC)
                    {
                        assert(nbMobs < MAX_MOBS);
                        SpriteInit(mobs + nbMobs, x, y, r, true);
                        nbMobs++;
                    }
                    break;
                case LAYER_ITEMS:
                    if(r > 0)
                    {
                        assert(nbItems < MAX_ITEMS);
                        SpriteInit(items + nbItems, x, y, r, false);
                        nbItems++;
                    }
                    break;
                case LAYER_COLLISION:
                    isColliding[x][y] = (r > 0);
                    break;
                case LAYER_GROUND:
                case LAYER_WALLS:
                case LAYER_PROPS:
                case LAYER_TOP:
                    RenderSpriteIndex(renderer, spriteSheetTexture, r, &dstrect);
                    break;
                }

                lua_pop(L, 1); // pop the element
            }

            lua_pop(L, 1); // pop the data

            lua_pop(L, 1); // pop the layer
        }

        lua_pop(L, 1); // pop layers
        
        lua_close(L);
        SDL_SetRenderTarget(renderer, NULL);
        return true;
    }
    else
    {
        return false;
    }
}

void RenderSprite(SDL_Renderer* renderer, SDL_Texture* texture, Sprite* sprite, const SDL_Rect* camera)
{
    SDL_Rect dstrect = {
        sprite->pos.x * gridSize + sprite->offset.x - camera->x, 
        sprite->pos.y * gridSize + sprite->offset.y - camera->y, 
        gridSize, 
        gridSize
    };
    RenderSpriteIndex(renderer, texture, sprite->spriteIndex, &dstrect);        
}

int GetNeighbors(SDL_Point p, SDL_Point neighbors[8])
{
    int nb_neighbors = 0;
    if((p.x > 0) && (p.y > 0))
        if(isColliding[p.x - 1][p.y] + isColliding[p.x - 1][p.y - 1] + isColliding[p.x][p.y - 1] == 0)
        {
            neighbors[nb_neighbors].x = p.x - 1;
            neighbors[nb_neighbors++].y = p.y - 1;
        }
    if(p.y > 0)
        if(isColliding[p.x][p.y - 1] == 0)
        {
            neighbors[nb_neighbors].x = p.x;
            neighbors[nb_neighbors++].y = p.y - 1;
        }
    if((p.x < MAX_COLUMNS - 1) && (p.y > 0))
        if(isColliding[p.x][p.y - 1] + isColliding[p.x + 1][p.y - 1] + isColliding[p.x + 1][p.y] == 0)
        {
            neighbors[nb_neighbors].x = p.x + 1;
            neighbors[nb_neighbors++].y = p.y - 1;
        }
    if(p.x < MAX_COLUMNS - 1)
        if(isColliding[p.x + 1][p.y] == 0)
        {
            neighbors[nb_neighbors].x = p.x + 1;
            neighbors[nb_neighbors++].y = p.y;
        }
    if((p.x < MAX_COLUMNS - 1) && (p.y < MAX_ROWS - 1))
        if(isColliding[p.x + 1][p.y] + isColliding[p.x + 1][p.y + 1] + isColliding[p.x][p.y + 1] == 0)
        {
            neighbors[nb_neighbors].x = p.x + 1;
            neighbors[nb_neighbors++].y = p.y + 1;
        }
    if(p.y < MAX_ROWS - 1)
        if(isColliding[p.x][p.y + 1] == 0)
        {
            neighbors[nb_neighbors].x = p.x;
            neighbors[nb_neighbors++].y = p.y + 1;
        }
    if((p.x > 0) && (p.y < MAX_ROWS - 1))
        if(isColliding[p.x][p.y + 1] + isColliding[p.x - 1][p.y + 1] + isColliding[p.x - 1][p.y] == 0)
        {
            neighbors[nb_neighbors].x = p.x - 1;
            neighbors[nb_neighbors++].y = p.y + 1;
        }
    if(p.x > 0)
        if(isColliding[p.x - 1][p.y] == 0)
        {
            neighbors[nb_neighbors].x = p.x - 1;
            neighbors[nb_neighbors++].y = p.y;
        }
    return nb_neighbors;
}

// sift up element at position idx, based on priority
int SiftUp(SDL_Point heap[], int idx, float priority[][MAX_ROWS])
{
    if(idx != 0) // otherwise, no need to sift up
    {
        int parent_idx = (idx - 1) / 2;
        if(priority[heap[parent_idx].x][heap[parent_idx].y] > priority[heap[idx].x][heap[idx].y]) // parent has larger f, so swap them
        {
            SDL_Point tempNode = heap[parent_idx];
            heap[parent_idx] = heap[idx];
            heap[idx] = tempNode;
            // try to sift up again from parent position
            return SiftUp(heap, parent_idx, priority);
        }
    }
    return 0;
}

int HeapInsert(SDL_Point heap[], int* heapSize, SDL_Point item, float priority[][MAX_ROWS])
{
    (*heapSize)++; // increment heap size
    heap[*heapSize - 1] = item; // append item at the end of the heap
    return SiftUp(heap, *heapSize - 1, priority); // sift the new element up
}

int SiftDown(SDL_Point heap[], int idx, int* heapSize, float priority[][MAX_ROWS])
{
    int min_idx;
    int left_child_idx = 2 * idx + 1;
    int right_child_idx = 2 * idx + 2;
    if (right_child_idx >= *heapSize)
    {
        if (left_child_idx >= *heapSize)
            return idx;
        else
            min_idx = left_child_idx;
    }
    else
    {
        if (priority[heap[left_child_idx].x][heap[left_child_idx].y] <= priority[heap[right_child_idx].x][heap[right_child_idx].y])
            min_idx = left_child_idx;
        else
            min_idx = right_child_idx;
    }
    if (priority[heap[idx].x][heap[idx].y] > priority[heap[min_idx].x][heap[min_idx].y])
    {
        SDL_Point tmpNode = heap[min_idx];
        heap[min_idx] = heap[idx];
        heap[idx] = tmpNode;
        return SiftDown(heap, min_idx, heapSize, priority);
    }
    else
        return idx;
}

SDL_Point HeapPop(SDL_Point heap[], int* heapSize, float priority[][MAX_ROWS])
{
    SDL_Point result = heap[0];
    // move last element to front
    heap[0] = heap[*heapSize - 1];
    // decrement heap size
    (*heapSize)--;
    if(*heapSize > 0)
    {
        SiftDown(heap, 0, heapSize, priority);
    }
    return result;
}

float MeleeDistEstimate(SDL_Point a, SDL_Point b)
{
    int dx = abs(a.x - b.x);
    if (dx > 0) dx--;
    int dy = abs(a.y - b.y);
    if (dy > 0) dy--;
    return dx + dy;    
}

float MoveCost(SDL_Point a, SDL_Point b)
{
    int dx = abs(a.x - b.x);
    int dy = abs(a.y - b.y);
    return (dx > dy) ? (dx + 0.5f * dy) : (dy + 0.5f * dx);
}

void Reverse(SDL_Point x[], int length)
{
    for(int i = 0; i < length / 2; i++)
    {
        SDL_Point tmp = x[length - 1 -i];
        x[length - 1 -i] = x[i];
        x[i] = tmp;
    }
}

int FindPath(SDL_Point start, SDL_Point end, SDL_Point path[], float h(SDL_Point, SDL_Point))
{
    float dToGoal[MAX_COLUMNS][MAX_ROWS];
    float dFromStart[MAX_COLUMNS][MAX_ROWS];
    bool isVisited[MAX_COLUMNS][MAX_ROWS];
    SDL_Point cameFrom[MAX_COLUMNS][MAX_ROWS];

    // initialize
    for(int x = 0; x < MAX_COLUMNS; x++)
        for(int y = 0; y < MAX_ROWS; y++)
        {
            dFromStart[x][y] = FLT_MAX;
            isVisited[x][y] = false;
        }

    // start with empty heap for the open set
    SDL_Point heap[MAX_COLUMNS * MAX_ROWS];
    int heapSize = 0;
    // add start point
    HeapInsert(heap, &heapSize, start, dToGoal);
    dToGoal[start.x][start.y] = h(start, end);
    dFromStart[start.x][start.y] = 0;

    while(heapSize > 0)
    {
        SDL_Point current = HeapPop(heap, &heapSize, dToGoal);
        if(h(current, end) == 0.0f) // found path
        {
            // backtrack to construct path
            int length = 0;
            while((start.x != current.x) || (start.y != current.y))
            {
                path[length] = current;
                length++;
                current = cameFrom[current.x][current.y];
            }
            assert(length < MAX_PATH);
            Reverse(path, length);
            return length;
        }
        else if(!isVisited[current.x][current.y])
        {
            SDL_Point neighbors[8];
            int nb_neighbors = GetNeighbors(current, neighbors);
            for(int i = 0; i < nb_neighbors; i++)
            {
                SDL_Point neighbor = neighbors[i];
                float tentativeDFromStart = dFromStart[current.x][current.y] + MoveCost(current, neighbor);
                if(tentativeDFromStart < dFromStart[neighbor.x][neighbor.y]) // found a shorter path from start to neighbor
                {
                    cameFrom[neighbor.x][neighbor.y] = current;
                    dFromStart[neighbor.x][neighbor.y] = tentativeDFromStart;
                    dToGoal[neighbor.x][neighbor.y] = tentativeDFromStart + h(neighbor, end);
                    HeapInsert(heap, &heapSize, neighbor, dToGoal);
                }
            }
            isVisited[current.x][current.y] = true;
        }
    }
    return -1;
}

bool InMeleeRange(const Sprite* a, const Sprite* b)
{
    return (abs(a->pos.x - b->pos.x) <= 1) && (abs(a->pos.y - b->pos.y) <= 1);
}

void EnqueueMoves(SDL_Point path[], int len)
{
    for(int i = 0; i < len; i++)
    {
        actionQueue[actionTail].tp = ACTION_MOVE;
        actionQueue[actionTail].obj.to = path[i];
        actionTail++;
        if(actionTail >= MAX_ACTIONS)
        {
            actionTail = 0;
        }    
    }
}

void EnqueueAttack(Sprite* target)
{
    actionQueue[actionTail].tp = ACTION_ATTACK;
    actionQueue[actionTail].obj.target = target;
    actionTail++;
    if(actionTail >= MAX_ACTIONS)
    {
        actionTail = 0;
    }    
}

Action DequeueAction()
{
    Action action = actionQueue[actionHead];
    actionHead++;
    if(actionHead >= MAX_ACTIONS)
    {
        actionHead = 0;
    }
    return action;
}

bool IsQueueEmpty()
{
    return actionTail == actionHead;
}

void ClearQueue()
{
    actionHead = actionTail;
    actionProgress = 0.0f;
}

void AddEnemy(Sprite* sprite)
{
    assert(nbCombatEnemies < MAX_ENEMIES);
    combatEnemies[nbCombatEnemies] = sprite;
    nbCombatEnemies++;
}

void RemoveEnemy(Sprite* sprite)
{
    int i;
    for(i = 0; i < nbCombatEnemies; i++)
    {
        if(combatEnemies[i] == sprite) break;
    }
    memmove(&combatEnemies[i], &combatEnemies[i + 1], (nbCombatEnemies - i - 1) * sizeof(combatEnemies[0]));
    nbCombatEnemies--;
}

void RemoveMob(Sprite* sprite)
{
    isColliding[sprite->pos.x][sprite->pos.y] = false;
    int mobIdx = (sprite - &mobs[0]) / sizeof(mobs[0]);
    memmove(sprite, sprite + 1, (nbMobs - mobIdx - 1) * sizeof(mobs[0]));
    nbMobs--;
}

void UpdateSprite(Sprite* sprite, float deltaTime)
{
    switch (currentAction.tp)
    {
    case ACTION_NONE:
        // start next action in the queue if there is one
        if(!IsQueueEmpty())
        {
            currentAction = DequeueAction();
            actionProgress = 0.0f;
        }
        break;
    case ACTION_MOVE:
        // progress the move
        actionProgress += moveSpeed * deltaTime;
        // if finished, update grid position, and move to next action if any
        if(actionProgress >= 1.0f)
        {
            isColliding[sprite->pos.x][sprite->pos.y] = false; // update collision grid
            sprite->pos = currentAction.obj.to;
            isColliding[sprite->pos.x][sprite->pos.y] = true;
            if(IsQueueEmpty())
            {
                currentAction.tp = ACTION_NONE;
                actionProgress = 0.0f;
            }
            else
            {
                currentAction = DequeueAction();
                if(currentAction.tp == ACTION_MOVE)
                {
                    actionProgress -= 1.0f; // roll extra progress into next move
                }
                else
                {
                    actionProgress = 0.0f;
                }
            }
        }
        // update display position
        sprite->offset.x = (currentAction.obj.to.x - sprite->pos.x) * actionProgress * gridSize;
        sprite->offset.y = (currentAction.obj.to.y - sprite->pos.y) * actionProgress * gridSize - 16;
        break;
    case ACTION_ATTACK:
    {
        Sprite* target = currentAction.obj.target;
        // attack
        int attackRoll = (rand() % 20) + 1;
        printf("attack roll=%d, ", attackRoll);
        if(attackRoll >= target->AC)
        {
            printf("hit, ");
            int dmgRoll = (rand() % 6) + 1;
            printf("dmg=%d, ", dmgRoll);
            target->hp -= dmgRoll;
            printf("hp=%d, \n", target->hp);
            if(target->hp <= 0)
            {
                if(target != &player)
                {
                    RemoveEnemy(target);
                    RemoveMob(target);
                }
            }
        }
        else
        {
            printf("miss\n");
        }
        currentAction.tp = ACTION_NONE;
        actionProgress = 0.0f;
        break;
    }
    }
}

float SpriteDistance(const Sprite* a, const Sprite* b)
{
    float dx = a->pos.x - b->pos.x;
    float dy = a->pos.y - b->pos.y;
    return sqrtf(dx * dx + dy * dy);
}

float PathMoveCost(SDL_Point path[], int pathLength)
{
    float cost = 0.0f;
    for(int i = 1; i < pathLength - 1; i++)
    {
        cost += MoveCost(path[i - 1], path[i]);
    }
    return cost;
}

Sprite* EnemyAtPosition(SDL_Point p)
{
    for(int i = 0; i < nbMobs; i++)
    {
        if((p.x == mobs[i].pos.x) && (p.y == mobs[i].pos.y))
        {
            return mobs + i;
        }
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    // initialize SDL and graphic resources

    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) 
    {
        SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "title", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        viewColumns * gridSize * scaling, viewRows * gridSize * scaling, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // load sprite sheet in graphic memory

    IMG_Init(IMG_INIT_PNG);
    SDL_Surface* image = IMG_Load(spriteSheetFile);
    if(!image) {
        printf("IMG_Load: %s\n", IMG_GetError());
        // handle error
    }
    spriteSheetTexture = SDL_CreateTextureFromSurface(renderer, image);
    SDL_FreeSurface(image);
    IMG_Quit();

    // load level

    if(!LoadLualevel())
    {
        printf("Can't load level\n");
        return 1;
    }

    // other initialization
    SDL_Rect camera = {
        gridSize * (player.pos.x - viewColumns / 2), 
        gridSize * (player.pos.y - viewRows / 2), 
        gridSize * viewColumns, 
        gridSize * viewRows};
    SDL_Point cursor = {0, 0};
    int cursorSpriteIndex = SPRITE_MOVETO;
    Uint32 prevButtons = 0;
    SDL_Point path[MAX_PATH];
    int pathLength = 0;
    int currentTime = 0;
    int prevTime = 0;
    float deltaTime;
    float frameTime = 0.0f;
    int currentEnemy = 0;

    SDL_RenderSetScale(renderer, scaling, scaling);
    SDL_RenderPresent(renderer);

    // main game loop

    SDL_bool loopShouldStop = SDL_FALSE;
    while (!loopShouldStop)
    {
        prevTime = currentTime;
        currentTime = SDL_GetTicks();
        deltaTime = (currentTime - prevTime) / 1000.0f;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_QUIT:
                    loopShouldStop = SDL_TRUE;
                    break;
            }
        }

        frameTime += deltaTime;
        if(frameTime >= 0.04f)
        {
            Sprite* enemy = combatEnemies[currentEnemy];
            Sprite* target = NULL;
            int x, y;
            Uint32 buttons;
            switch (gameState)
            {
            case GAME_EXPLORE:
                // update cursor
                SDL_PumpEvents();
                buttons = SDL_GetMouseState(&x, &y);
                cursor.x = (x / (int)scaling + camera.x) / gridSize;
                cursor.y = (y / (int)scaling + camera.y) / gridSize;
                target = EnemyAtPosition(cursor);
                if(target)
                {
                    cursorSpriteIndex = SPRITE_ATTACK;
                    pathLength = FindPath(player.pos, cursor, path, MeleeDistEstimate);
                }
                else
                {
                    cursorSpriteIndex = SPRITE_MOVETO;
                    pathLength = FindPath(player.pos, cursor, path, MoveCost);
                }
                if(((buttons & SDL_BUTTON_LMASK) != 0) && ((prevButtons & SDL_BUTTON_LMASK) == 0))
                {
                    ClearQueue();
                    EnqueueMoves(path, pathLength);
                }
                prevButtons = buttons;
                // update player position
                UpdateSprite(&player, deltaTime);
                // check aggro
                for(int i = 0; i < nbMobs; i++)
                {
                    if(SpriteDistance(&player, mobs + i) <= aggroRadius) // aggro
                    {
                        AddEnemy(mobs + i);
                    }
                }
                // if aggro'd start combat
                if(nbCombatEnemies > 0) 
                {
                    ClearQueue();
                    printf("combat start, roll initiative\n");
                    currentEnemy = 0;
                    // roll initiative
                    if(rand() % 2)
                    {
                        printf("player has initiative\n");
                        gameState = GAME_COMBAT_PLAYERINPUT;
                    }
                    else
                    {
                        printf("enemy has initiative\n");
                        gameState = GAME_COMBAT_ENEMYAI;
                    }
                }
                break;
            case GAME_COMBAT_PLAYERINPUT:
                // update cursor
                SDL_PumpEvents();
                buttons = SDL_GetMouseState(&x, &y);
                cursor.x = (x / (int)scaling + camera.x) / gridSize;
                cursor.y = (y / (int)scaling + camera.y) / gridSize;
                target = EnemyAtPosition(cursor);
                if(target)
                {
                    pathLength = FindPath(player.pos, cursor, path, MeleeDistEstimate);
                    if(PathMoveCost(path, pathLength) > playerMaxMove)
                    {
                        cursorSpriteIndex = SPRITE_INACCESSIBLE;
                    }
                    else
                    {
                        cursorSpriteIndex = SPRITE_ATTACK;
                    }
                }
                else
                {
                    pathLength = FindPath(player.pos, cursor, path, MoveCost);
                    if(PathMoveCost(path, pathLength) > playerMaxMove)
                    {
                        cursorSpriteIndex = SPRITE_INACCESSIBLE;
                    }
                    else
                    {
                        cursorSpriteIndex = SPRITE_MOVETO;
                    }
                }
                if(((buttons & SDL_BUTTON_LMASK) != 0) && ((prevButtons & SDL_BUTTON_LMASK) == 0))
                {
                    ClearQueue();
                    EnqueueMoves(path, pathLength);
                    if(cursorSpriteIndex == SPRITE_ATTACK)
                    {
                        EnqueueAttack(target);
                    }
                    gameState = GAME_COMBAT_PLAYERRESOLVE;
                }
                prevButtons = buttons;
                break;
            case GAME_COMBAT_PLAYERRESOLVE:
                // update player position
                UpdateSprite(&player, deltaTime);
                // check if move is finished
                if(currentAction.tp == ACTION_NONE)
                {
                    printf("player finished, ");
                    if(nbCombatEnemies > 0)
                    {
                        printf("enemy turn\n");
                        gameState = GAME_COMBAT_ENEMYAI;
                    }
                    else
                    {
                        printf("combat finished\n");
                        gameState = GAME_EXPLORE;
                    }
                }
                break;
            case GAME_COMBAT_ENEMYAI:
                // play enemy turn 
                if(InMeleeRange(&player, enemy))
                {
                    ClearQueue();
                    EnqueueAttack(&player);
                }
                else
                {
                    // move within melee range
                    pathLength = FindPath(enemy->pos, player.pos, path, MeleeDistEstimate);
                    if(pathLength > enemyMaxMove)
                    {
                        EnqueueMoves(path, enemyMaxMove);
                    }
                    else
                    {
                        EnqueueMoves(path, pathLength);
                        EnqueueAttack(&player);
                    }
                    pathLength = 0;
                    printf("enemy moving\n");
                }
                gameState = GAME_COMBAT_ENEMYRESOLVE;
                break;
            case GAME_COMBAT_ENEMYRESOLVE:
                // update enemy position
                UpdateSprite(enemy, deltaTime);
                // check if turn is finished
                if(currentAction.tp == ACTION_NONE)
                {
                    printf("enemy finished, ");
                    // next enemy
                    currentEnemy++;
                    if(currentEnemy >= nbCombatEnemies)
                    {
                        printf("player turn\n");
                        currentEnemy = 0;
                        gameState = GAME_COMBAT_PLAYERINPUT;
                    }
                    else
                    {
                        printf("next enemy\n");
                        gameState = GAME_COMBAT_ENEMYAI;
                    }    
                }
                break;
            }
        }

        // re-center camera on player
        camera.x = gridSize * (player.pos.x - viewColumns / 2) + player.offset.x;
        camera.y = gridSize * (player.pos.y - viewRows / 2) + player.offset.y;
        // first render the background
        SDL_RenderCopy(renderer, backgroundTexture, &camera, NULL);
        // render sprites
        RenderSprite(renderer, spriteSheetTexture, &player, &camera);
        for(int i = 0; i < nbMobs; i++)
        {
            RenderSprite(renderer, spriteSheetTexture, mobs + i, &camera);
        }
        for(int i = 0; i < nbItems; i++)
        {
            RenderSprite(renderer, spriteSheetTexture, items + i, &camera);
        }
        // render foreground
        SDL_RenderCopy(renderer, foregroundTexture, &camera, NULL);
        // draw cursor
        SDL_Rect dstrect = {cursor.x * gridSize - camera.x, cursor.y * gridSize - camera.y, gridSize, gridSize};
        RenderSpriteIndex(renderer, spriteSheetTexture, cursorSpriteIndex, &dstrect);
        // draw path
        for(int i = 1; i < pathLength - 1; i++)
        {
            dstrect.x = path[i].x * gridSize - camera.x;
            dstrect.y = path[i].y * gridSize - camera.y;
            RenderSpriteIndex(renderer, spriteSheetTexture, SPRITE_PATHDOT, &dstrect);
        }

        SDL_RenderPresent(renderer);
    }

    // clean-up
    SDL_DestroyTexture(spriteSheetTexture);
    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(foregroundTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}