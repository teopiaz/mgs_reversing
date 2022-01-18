#ifndef ACTOR_H_
#define ACTOR_H_

#include <libsn.h>
#include "libgv.h"

struct Actor;

typedef void (*TActorFunction)(struct Actor *);
typedef void (*TActorFreeFunction)(void *);

typedef struct Actor
{
    struct Actor *pPrevious;
    struct Actor *pNext;
    TActorFunction mFnUpdate;
    TActorFunction mFnShutdown;
    TActorFreeFunction mFreeFunc;
    const char *mName;
    int field_18;
    int field_1C;
} Actor;

struct ActorList
{
    struct Actor first;
    struct Actor last;
    short mPause;
    short mKill;
};

#define ACTOR_LIST_COUNT 9
struct PauseKill
{
    short pause;
    short kill;
};

extern int GM_CurrentMap_800AB9B0;

void GV_ExecActorSystem_80014F88(void);
struct Actor *GV_ActorAlloc_800150e4(int level, int memSize);
void GV_ActorList_Init_80014d98(void);
void GV_ActorPushBack_800150a8(int level, struct Actor *pActor, TActorFreeFunction fnFree);
void GV_ActorInit_8001514c(struct Actor *pActor, TActorFunction pFnUpdate, TActorFunction pFnShutdown, const char *pActorName);
void GV_ActorDelayedKill_800151c8(struct Actor *pActor);

#endif // ACTOR_H_
