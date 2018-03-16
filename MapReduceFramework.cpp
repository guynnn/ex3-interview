#include <cstdio>
#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>
#include "MapReduceFramework.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <fstream>
#include <chrono>

#define CHUNK 10

// ************** global variables **************

// mapInput from the runMapReduceFramework function
std::vector<IN_ITEM> mapInput;
// initializing that mutex
pthread_mutex_t pthreadToContainer_mutex;
// make sure shuffle and execMap wont access the same container.
std::vector<pthread_mutex_t> modifyPthreadToContainer_mutex;
// make sure execMaps wont read the same elements from the input vector
pthread_mutex_t index_mutex;

// the index in the input vector which need to be read next
unsigned int mapIndex = 0;
// the index in the reduce vector which need to be read next
unsigned int reduceIndex = 0;
// maps between the running thread to its container(int)
std::map<pthread_t ,int> toInt;
// each execMap thread has exactly one vector
std::vector<std::vector<std::pair<k2Base *, v2Base *>>> pthreadToContainer;
// each execReduce thread has exactly one vector
std::vector<std::vector<std::pair<k3Base *, v3Base *>>> reduceThreadContainers;

MapReduceBase* mapReduceCopy;

sem_t shuffle_sem;

bool isMapFinished = false;

std::vector<std::pair<k2Base*, std::vector<v2Base*>>> shuffleVector;

/**
 * each thread take the next available pairs and run map() on them
 * @return nullptr
 */
void* execMap(void*) {
    // each ExecMap thread in its first two lines lock and then unlock the mutex
    if (pthread_mutex_lock(&pthreadToContainer_mutex)) { exit(1); }
    if (pthread_mutex_unlock(&pthreadToContainer_mutex)) { exit(1); }
    if (pthread_mutex_lock(&index_mutex)) { exit(1); }
    unsigned int currentIndex = mapIndex;
    if (pthread_mutex_unlock(&index_mutex)) { exit(1); }
    while (currentIndex < mapInput.size()) {
        if (pthread_mutex_lock(&index_mutex)) { exit(1); }
        currentIndex = mapIndex;
        mapIndex += CHUNK;
        if (pthread_mutex_unlock(&index_mutex)) { exit(1); }
        for (int i = 0; i < CHUNK && i + currentIndex < mapInput.size(); i++) {
            k1Base* key = mapInput[currentIndex + i].first;
            v1Base* value = mapInput[currentIndex + i].second;
            mapReduceCopy->Map(key, value);
        }
    }
    return nullptr;
}

/**
 * check if new ,or we need to open new key.
 */
void addToShuffle(k2Base* k2, v2Base* v2) {
    for (unsigned int i = 0; i < shuffleVector.size(); i++) {
        if (!(*shuffleVector[i].first < *k2) &&
            !(*k2 < *shuffleVector[i].first)) {
            // k2 equals to an existing key
            shuffleVector[i].second.push_back(v2);
            return;
        }
    }
    // k2 is new to shuffleVector, need to open new pair
    std::vector<v2Base*> currVec;
    currVec.push_back(v2);
    std::pair<k2Base*, std::vector<v2Base*>> add = std::make_pair(k2, currVec);
    shuffleVector.push_back(add);
}

/**
 * @return true if pthreadToContainer is empty
 */
bool isEmpty() {
    for (unsigned int i = 0; i < pthreadToContainer.size(); i++) {
        if (!pthreadToContainer[i].empty()) {
            return false;
        }
    }
    return true;
}

/**
 * while pthreadToContainer is not empty, transfer elements from it to
 * shuffleVector.
 * @return nullptr
 */
void* shuffle(void*) {
    while (!isMapFinished || !isEmpty()) {
        bool enter = true;
        if (sem_wait(&shuffle_sem)) { exit(1); }
        while (enter) {
            enter = false;
            for (unsigned int i = 0; i < pthreadToContainer.size(); i++) {
                if (!pthreadToContainer[i].empty()) {
                    enter = true;
                    if (pthread_mutex_lock(&modifyPthreadToContainer_mutex[i])) { exit(1); }
                    k2Base* k2 = pthreadToContainer[i].at(pthreadToContainer[i].size() - 1).first;
                    v2Base* v2 = pthreadToContainer[i].at(pthreadToContainer[i].size() - 1).second;
                    pthreadToContainer[i].pop_back();
                    addToShuffle(k2, v2);
                    if (pthread_mutex_unlock(&modifyPthreadToContainer_mutex[i])) { exit(1); }
                }
            }
        }
    }
    return nullptr;
}

/**
 * run the reduce() function on elements from shuffleVector
 * @return nullptr
 */
void* execReduce(void*) {
    if (pthread_mutex_lock(&pthreadToContainer_mutex)) { exit(1); }
    if (pthread_mutex_unlock(&pthreadToContainer_mutex)) { exit(1); }
    if (pthread_mutex_lock(&index_mutex)) { exit(1); }
    unsigned int currentIndex = reduceIndex;
    pthread_mutex_unlock(&index_mutex);
    while (currentIndex < shuffleVector.size()) {
        if (pthread_mutex_lock(&index_mutex)) { exit(1); }
        currentIndex = reduceIndex;
        reduceIndex += CHUNK;
        if (pthread_mutex_unlock(&index_mutex)) { exit(1); }
        for (unsigned int i = 0; i < CHUNK && i + currentIndex <
                                                      shuffleVector.size(); ++i) {
            mapReduceCopy->Reduce(shuffleVector[i + currentIndex].first,
                                  shuffleVector[i + currentIndex].second);
        }
    }
    return nullptr;
}

/**
 * compare between two pairs of k3, v2. assuming k3 implements operator<
 * @return true iff a.k3Base < b.k3Base
 */
bool sortingFinal(std::pair<k3Base *, v3Base *> a,
                  std::pair<k3Base *, v3Base *> b) {
    return *a.first < *b.first;
}

/**
 * the main function.
 * @param mapReduce objets that has map() and reduce()
 * @param itemsVec the input vector
 * @param multiThreadLevel the number of threads to use
 * @param autoDeleteV2K2 indicates if k2, v2 pairs should be deleted
 * @return vector of k3, v3 pairs.
 */
OUT_ITEMS_VEC RunMapReduceFramework(MapReduceBase& mapReduce,
                                    IN_ITEMS_VEC& itemsVec,
                                    int multiThreadLevel, bool autoDeleteV2K2) {
    // *** initializing mutexes and semaphore
    pthreadToContainer_mutex = PTHREAD_MUTEX_INITIALIZER;
    index_mutex = PTHREAD_MUTEX_INITIALIZER;

    // copy the mapInput
    for (unsigned int i = 0; i < itemsVec.size(); ++i) {
        mapInput.push_back(itemsVec[i]);
    }

    // copy the object:
    mapReduceCopy = &mapReduce;
    if (pthread_mutex_lock(&pthreadToContainer_mutex)) { exit(1); }


    // ************ map *************
    std::vector<pthread_t> mapThreads(multiThreadLevel);
    for (int i = 0; i < multiThreadLevel; ++i) {
        std::vector<std::pair<k2Base *, v2Base *>> vector;
        pthreadToContainer.push_back(vector);
        pthread_mutex_t current_mutex = PTHREAD_MUTEX_INITIALIZER;
        modifyPthreadToContainer_mutex.push_back(current_mutex);
        int retCreateMap = pthread_create(&mapThreads[i], nullptr, execMap, nullptr);
        if (retCreateMap != 0) { exit(1); }
        toInt[mapThreads[i]] = i;
    }

    if (pthread_mutex_unlock(&pthreadToContainer_mutex)) { exit(1); }

    // *********** shuffle ************
    pthread_t shuffleThread;
    if (sem_init(&shuffle_sem, 0, 0)) { exit(1); }

    int retCreateShuffle = pthread_create(&shuffleThread, nullptr, shuffle, nullptr);
    if (retCreateShuffle != 0) { exit(1); }

    for (int i = 0; i < multiThreadLevel; i++) {
        if (pthread_join(mapThreads[i], nullptr)) { exit(1); }
    }

    isMapFinished = true;
    if (sem_post(&shuffle_sem)) { exit(1); }
    if (pthread_join(shuffleThread, nullptr)) { exit(1); }


    // ****  Reduce ****
    std::vector<pthread_t> reduceThreads((unsigned)multiThreadLevel);

    if (pthread_mutex_lock(&pthreadToContainer_mutex)) { exit(1); }
    toInt.clear();
    for (int i = 0; i < multiThreadLevel; i++) {
        OUT_ITEMS_VEC vector;
        reduceThreadContainers.push_back(vector);
        int retCreateReduce = pthread_create(&reduceThreads[i], nullptr, execReduce, nullptr);
        toInt[reduceThreads[i]] = i;
        if (retCreateReduce != 0) { exit(1); }
    }
    if (pthread_mutex_unlock(&pthreadToContainer_mutex)) { exit(1); }


    for (int j = 0; j < multiThreadLevel; ++j) {
        if (pthread_join(reduceThreads[j], nullptr)) { exit(1); }
    }

    //*** final output ***
    OUT_ITEMS_VEC finalOutput;
    for (unsigned int i = 0; i < reduceThreadContainers.size(); ++i) {
        while (!reduceThreadContainers[i].empty()) {
            unsigned long last = reduceThreadContainers[i].size();
            std::pair<k3Base*, v3Base*> pair = reduceThreadContainers[i][last - 1];
            finalOutput.push_back(pair);
            reduceThreadContainers[i].pop_back();
        }
    }
    std::sort(finalOutput.begin(), finalOutput.end(), sortingFinal);

    // ** delete **
    if (autoDeleteV2K2) {
        for (unsigned int i = 0; i < shuffleVector.size(); ++i) {
            delete (shuffleVector[i].first);
            for (unsigned int j = 0; j < shuffleVector[i].second.size(); ++j) {
                delete (shuffleVector[i].second[j]);
            }
        }
    }
//    clear_all();
    return finalOutput;
}

/**
 * add the given pair to pthreadToContainer
 * @param key the k2 object to add
 * @param value the v2 object to add
 */
void Emit2(k2Base* key, v2Base* value) {
    int i = toInt[pthread_self()];
    std::pair<k2Base*, v2Base*> p = std::make_pair(key, value);

    if (pthread_mutex_lock(&modifyPthreadToContainer_mutex[i])) { exit(1); }
    pthreadToContainer[i].push_back(p);
    if (pthread_mutex_unlock(&modifyPthreadToContainer_mutex[i])) { exit(1); }

    if (sem_post(&shuffle_sem)) { exit(1); }
}

/**
 * add the given pair to reduceThreadContainer
 * @param key k3 object to add
 * @param value v3 object to add
 */
void Emit3(k3Base* key, v3Base* value) {
    int i = toInt[pthread_self()];
    std::pair<k3Base *, v3Base *> p = std::make_pair(key, value);
    reduceThreadContainers[i].push_back(p);
}
