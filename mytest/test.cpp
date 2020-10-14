//
// Created by raven on 9/15/20.
//
#include <iostream>
#include <ctype.h>
#include "../leveldb/include/leveldb/filter_policy.h"
#include "../leveldb/include/leveldb/db.h"
#include "../leveldb/include/leveldb/write_batch.h"

using namespace std;
using namespace leveldb;

void test1()
{
    leveldb::DB *db;
    leveldb::Options opts;
    opts.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(opts,"/home/raven/Projects/leveldb_learning/mytest/testdb",&db);

    /* write data test */
    status = db->Put(leveldb::WriteOptions(),"name","raven");
    /* Read data test*/
    string value;
    status = db->Get(leveldb::ReadOptions(),"name",&value);

    cout << value << endl;

    /* batch write test */
    leveldb::WriteBatch batch;
    batch.Delete("name");
    batch.Put("name0","raven0");
    batch.Put("name1","raven1");
    batch.Put("name2","raven2");
    batch.Put("name3","raven3");
    batch.Put("name4","raven4");
    batch.Put("name5","raven5");
    batch.Put("name6","raven6");
    batch.Put("name7","raven7");
    status = db->Write(leveldb::WriteOptions(), &batch);


    /* scan database */
    leveldb::Iterator *it = db->NewIterator(leveldb::ReadOptions());
    for(it->SeekToFirst(); it->Valid(); it->Next()){
        cout << it->key().ToString() <<":" << it->value().ToString() << "\n";
    }

    /* scan range [name3,name7) */
    for(it->Seek("name3"); it->Valid() && it->key().ToString() < "name8";it->Next()){
        cout << it->key().ToString() <<":" << it->value().ToString() << "\n";
    }

    /* write a large key value pair */
    string l_key = "large file";
    char buf[1024*100];
    string l_value(buf);
    leveldb::Status  s = db->Put(leveldb::WriteOptions(), l_key, l_value);

    delete db;
}

void test2()
{
    leveldb::DB *db;
    leveldb::Options opts;
    opts.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(opts,"/home/raven/Projects/leveldb_learning/mytest/testdb",&db);

    srand(time(0));
    string key;
    string value;
    char buf[100];
    for(unsigned int i = 0;i<=INT32_MAX;i++){
        fill(begin(buf),end(buf),'\0');
        sprintf(buf,"key%d",rand());
        key = buf;

        fill(begin(buf),end(buf),'\0');
        sprintf(buf,"value%d",rand());
        value = buf;
        db->Put(WriteOptions(),key,value);
    }

    delete db;
}


void test3()
{
    leveldb::DB *db;
    leveldb::Options opts;
    opts.create_if_missing = true;
    const FilterPolicy *pPolicy = NewBloomFilterPolicy(10);
    opts.filter_policy= pPolicy;
    opts.compression = kNoCompression;
    leveldb::Status status = leveldb::DB::Open(opts,"/home/raven/Projects/leveldb_learning/mytest/testdb",&db);


    /* key 2bytes value 200bytes */
    const int len = 3500;
    char key[6];
    char value[len];
    for(int i = 0;i<30*1000;i++){
        sprintf(key,"%d",i);
        db->Put(leveldb::WriteOptions(),key,Slice(value,len));
    }

    string get_value;
    for(int i = 0;i<30*1000;i++){
        sprintf(key,"%d",i);
        const Status &s = db->Get(leveldb::ReadOptions(), key, &get_value);
        if(s.ok()){
            cout << i << " search success\n";
        }else{
            cout << i << " search failed\n";
        }
    }

    delete db;
}
int main()
{
    test2();
    return 0;
}

