#include <iostream>
#include <fstream>
#include <dirent.h>
#include <algorithm>
#include "MapReduceClient.h"
#include "MapReduceFramework.h"

#define THREADS_NUM 8

/**
 * represents k1 class.
 */
class k1: public k1Base {
public:

    k1(const std::string& dirName, const std::string& toSearch) {
        this->dirName = dirName;
        this->toSearch = toSearch;
    }
    virtual ~k1() {
        return;
    }
    std::string getKey() const {
        return this->dirName;
    }

    std::string getSearchStr() const {
        return this->toSearch;
    }

    virtual bool operator<(const k1Base &other) const {
        const k1& key = (k1&)other;
        return getKey() < key.getKey();
    }
private:
    std::string dirName;
    std::string toSearch;
};

/**
 * represents class k2
 */
class k2: public k2Base {
public:

    k2(const std::string& name) : fileName(name) {}

    virtual ~k2() {
        return;
    }
    std::string getKey() const {
        return fileName;
    }
    virtual bool operator<(const k2Base &other) const {
        const k2& key = (k2&)other;
        return fileName < key.getKey();
    }

private:
    std::string fileName;
};

/**
 * represents class k3.
 */
class k3: public k3Base {
public:

    k3(const std::string& name) {
        this->fileName = name;
    }

    virtual ~k3() {
        return;
    }

    std::string getKey() const {
        return this->fileName;
    }

    virtual bool operator<(const k3Base &other) const {
        const k3& key = (k3&)other;
        return fileName < key.getKey();
    }

private:
    std::string fileName;
};

/**
 * represents class v1.
 */
class directoryNum: public v1Base {
public:

    int getCount() const {
        return this->count;
    }

    directoryNum(int count) {
        this->count = count;
    }

    ~directoryNum() {
        return;
    }

private:
    int count;
};

/**
 * represents class v2.
 */
class v2: public v2Base
{
public:

    int getCount() const {
        return this->count;
    }

    v2(int count) {
        this->count = count;
    }

    ~v2() {
        return;
    }

private:
    int count;
};

/**
 * represents class v3.
 */
class v3: public v3Base
{
public:

    int getCount() const {
        return this->count;
    }

    v3(int count) {
        this->count = count;
    }

    ~v3() {}

private:
    int count;
};

/**
 * this class has the map and reduce function
 */
class MapReduce : public MapReduceBase {

public:
    virtual void Map(const k1Base *const key, const v1Base *const val) const {
        k1 * dirName = (k1*)key;
        DIR *dir;
        struct dirent *ent;
        if ((dir = opendir (dirName->getKey().c_str())) != NULL) {
            /* print all the files and directories within directory */
            while ((ent = readdir (dir)) != NULL) {
                if (((std::string)ent->d_name).find(dirName->getSearchStr())
                                                        != std::string::npos) {
                    k2* newKey = new(std::nothrow) k2((std::string)ent->d_name);
                    v2* newVal = new(std::nothrow) v2(1);
                    if (!newKey || !newVal) { exit(1); }
                    Emit2(newKey, newVal);
                }
            }
            closedir(dir);
        }
    }

    virtual void Reduce(const k2Base *const key, const V2_VEC &vals) const {
        k2* temp = (k2*)key;
        const std::string name = temp->getKey();
        k3* out_k = new(std::nothrow) k3(name);
        v3* out = new(std::nothrow) v3((int)vals.size());
        if (!out_k || !out) { exit(1); }
        Emit3(out_k, out);
    }
};


/**
 * @param argc number of arguments
 * @param argv the arguments
 * @return 0 iff the program is successful
 */
int main(int argc, char * argv[]) {
    if (argc == 1) {
        printf("Usage: <substring to search> <folders, separated by space>");
        return 0;
    }
    const std::string toSearch = argv[1];
    IN_ITEMS_VEC input;
    for (int i = 2; i < argc; ++i) {
        k1* in_k = new(std::nothrow) k1(argv[i], toSearch);
        if (!in_k) { exit(1); }
        directoryNum* in_v = nullptr;
        IN_ITEM current(in_k, in_v);
        input.push_back(current);
    }
    MapReduce map_r;
    OUT_ITEMS_VEC out = RunMapReduceFramework(map_r, input, THREADS_NUM, true);
    for (unsigned int i = 0; i < out.size(); ++i) {
        k3* out_k = (k3*)out[i].first;
        std::cout << out_k->getKey();
        if (i != out.size() - 1) {
            std::cout << " ";
        }
    }

    // ************ delete ************
    for (unsigned int i = 0; i < input.size(); ++i) {
        delete (input[i].first);
        // second is null anyway
    }

    for (unsigned int i = 0; i < out.size(); i++) {
        delete (out[i].first);
        delete (out[i].second);
    }
    return 0;
}
