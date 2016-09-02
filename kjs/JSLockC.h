/*
 * Copyright (C) 2006, 2007, Apple Inc. All rights reserved.
 */

#include <pthread.h>

#ifdef __cplusplus
extern "C" { 
#endif

void JSLockDropAllLocks(void);
void JSLockRecoverAllLocks(void);
void JSSetJavaScriptCollectionThread (pthread_t thread);
pthread_t JSJavaScriptCollectionThread (void);

#ifdef __cplusplus
}
#endif

