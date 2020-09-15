//
// Created by raven on 9/15/20.
//
#include <iostream>
#include "../leveldb/include/leveldb/db.h"
#include "../leveldb/include/leveldb/write_batch.h"

using namespace std;

int main(void)
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

    delete db;
    return 0;
}

