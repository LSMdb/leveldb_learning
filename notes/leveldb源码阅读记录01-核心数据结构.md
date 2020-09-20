## 1. 核心的数据结构

### 1. Env

#### env构造

env的构造从Option构造开始，具体可参见第二小结。

比较器暂时不看，options中主要包含了一个Env环境。

```c++
leveldb/util/env_posix.cc

using PosixDefaultEnv = SingletonEnv<PosixEnv>;
Env* Env::Default() {
  static PosixDefaultEnv env_container;
  return env_container.env();
}

Env* env() { return reinterpret_cast<Env*>(&env_storage_); }
```

Evn.Default调用PosixDefaultEnv（是SingletonEnv<PosixEnv>的别名).env, env返回env_storage.

<img src="https://pic.downk.cc/item/5f633ecd160a154a67e86190.png" alt="image-20200916163444419" style="zoom:50%;" />

关于env_storage：

```c++
用于字节对齐
typename std::aligned_storage<sizeof(EnvType), alignof(EnvType)>::type
      env_storage_;
env_storage_在PosixDefaultEnv的构造函数中被初始化：
new (&env_storage_) EnvType();		// 这句话的意思是，按照EvnType申请一个对象，然后将这个对象填充到env_storage_的地址空间中去。
```

最后的默认环境为 **PosixEnv类**, 看一下它的类图：

<img src="https://pic.downk.cc/item/5f633eec160a154a67e8687e.png" alt="image-20200916170716873" style="zoom: 50%;" />

可以看到都是一些辅助函数，包括文件创建，监测，线程管理（同步）等。这里面其实维护了一个线程池和工作队列，

#### 线程池、队列

工作队列如下：

```c++
// 队列
std::queue<BackgroundWorkItem> background_work_queue_
      GUARDED_BY(background_work_mutex_);
      
// work item
// Stores the work item data in a Schedule() call.
//
// Instances are constructed on the thread calling Schedule() and used on the
// background thread.
//
// This structure is thread-safe beacuse it is immutable.
struct BackgroundWorkItem {
    explicit BackgroundWorkItem(void (*function)(void* arg), void* arg)
        : function(function), arg(arg) {}

    void (*const function)(void*);
    void* const arg;
};
```

如何调度一个线程，看Schedule函数：

目前看来线程池中只有一个线程：

```c++
void PosixEnv::Schedule(
    void (*background_work_function)(void* background_work_arg),
    void* background_work_arg) {
  background_work_mutex_.Lock();

  // Start the background thread, if we haven't done so already.
  if (!started_background_thread_) {
    started_background_thread_ = true;
    std::thread background_thread(PosixEnv::BackgroundThreadEntryPoint, this);
    background_thread.detach();
  }

  // If the queue is empty, the background thread may be waiting for work.
  if (background_work_queue_.empty()) {
    background_work_cv_.Signal();
  }

  background_work_queue_.emplace(background_work_function, background_work_arg);
  background_work_mutex_.Unlock();
}
```

这里重点看下唤醒消费者的代码，注意这里Signal后，消费者的线程虽然被唤醒，但是依然处于阻塞状态，只有当前线程，调用 

```c++
background_work_mutex_.Unlock();
```

后，消费者线程才能正常执行。

```c++
// If the queue is empty, the background thread may be waiting for work.
if (background_work_queue_.empty()) {
	background_work_cv_.Signal();
}
background_work_queue_.emplace(background_work_function, background_work_arg);
background_work_mutex_.Unlock();
```

如果第一次调度，则首先启动线程, 线程执行的函数为 BackgroundThreadEntryPoint:

```c++
 static void BackgroundThreadEntryPoint(PosixEnv* env) {
    env->BackgroundThreadMain();
  }
  
  void PosixEnv::BackgroundThreadMain() {
  while (true) {
    background_work_mutex_.Lock();

    // Wait until there is work to be done.
    while (background_work_queue_.empty()) {
      background_work_cv_.Wait();
    }

    assert(!background_work_queue_.empty());
    auto background_work_function = background_work_queue_.front().function;
    void* background_work_arg = background_work_queue_.front().arg;
    background_work_queue_.pop();

    background_work_mutex_.Unlock();
    background_work_function(background_work_arg);
  }
}
```

<img src="https://pic.downk.cc/item/5f633efd160a154a67e86b3b.png" alt="image-20200916171519015" style="zoom:50%;" />



#### Mutex和CondVar

```c++
class LOCKABLE Mutex {
 public:
  Mutex() = default;
  ~Mutex() = default;

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;

  void Lock() EXCLUSIVE_LOCK_FUNCTION() { mu_.lock(); }
  void Unlock() UNLOCK_FUNCTION() { mu_.unlock(); }
  void AssertHeld() ASSERT_EXCLUSIVE_LOCK() {}

 private:
  friend class CondVar;
  std::mutex mu_;
};
```

Mutex就是对标准mutex的封装，同时用了些宏来修饰，这些宏是clang用于 语法分析（猜想） 的宏，如这里的 ASSERT_EXCLUSIVE_LOCK， 表示调用AssertHeld函数时，必须实在获取独占lock的情况下。

```c++
// Thinly wraps std::condition_variable.
class CondVar {
 public:
  explicit CondVar(Mutex* mu) : mu_(mu) { assert(mu != nullptr); }
  ~CondVar() = default;

  CondVar(const CondVar&) = delete;
  CondVar& operator=(const CondVar&) = delete;

  void Wait() {
    std::unique_lock<std::mutex> lock(mu_->mu_, std::adopt_lock);
    cv_.wait(lock);
    lock.release();		// 断开lock和mutex的关联，不释放mutex
  }
  void Signal() { cv_.notify_one(); }
  void SignalAll() { cv_.notify_all(); }

 private:
  std::condition_variable cv_;
  Mutex* const mu_;
};
```

### 2. Options

首先看下Options数据结构：

<img src="https://cdn.jsdelivr.net/gh/ravenxrz/PicBed/img/image-20200917190238495.png" alt="image-20200917190238495" style="zoom:50%;" />

其中Env为环境管理，Comparator为排序比较器，FilterPolicy包含Bloom过滤器等，Logger则为日志记录类。

### 3. DB & DBImpl

<img src="https://cdn.jsdelivr.net/gh/ravenxrz/PicBed/img/image-20200917191447072.png" alt="image-20200917191447072" style="zoom: 50%;" />

默认实现中DB只是一个抽象类，具体的实现由DBImpl实：这里不关注DB提供了哪些接口（因为接口和DB抽象类差不多），我们看看DBImpl内部维护了哪些东西。
<img src="https://cdn.jsdelivr.net/gh/ravenxrz/PicBed/img/image-20200917200526294.png" alt="image-20200917191610062" style="zoom:50%;" />

DBImpl是整个leveldb的核心了。之后回回来慢慢分析。

### 4. VersionEdit

VersionEdit是LevelDB两个Version之间的差量，即：

```
Versoin0 + VersoinEdit = Version1
```

差量包括本次操作，新增的文件和删除的文件。

看看VersionEdit的成员：

```c++
 private:
  friend class VersionSet;

  typedef std::set<std::pair<int, uint64_t>> DeletedFileSet;

  std::string comparator_;
  uint64_t log_number_;
  uint64_t prev_log_number_;
  uint64_t next_file_number_;
  SequenceNumber last_sequence_;
  bool has_comparator_;
  bool has_log_number_;
  bool has_prev_log_number_;
  bool has_next_file_number_;
  bool has_last_sequence_;

  std::vector<std::pair<int, InternalKey>> compact_pointers_;		// 存放这个version的压缩指针，pair.first对应哪一个level， pair.second 对应哪一个key开始compaction
  DeletedFileSet deleted_files_;
  std::vector<std::pair<int, FileMetaData>> new_files_;
```

关注最后3个， compacton_pointers暂时不管，**delted_files_, new_files_是这次版本修改的差量。**

**关注new_files_中的FileMetaData，因为一次版本修改新增的文件是这个类的集合，**

### 5. FileMetaData

FileMetaData是每个Version内部维持的文件，每层中都有多个FileMetaData（个人猜想，这和SST相关，但是不是SST）

```c++
struct FileMetaData {
  FileMetaData() : refs(0), allowed_seeks(1 << 30), file_size(0) {}

  int refs;
  int allowed_seeks;  // Seeks allowed until compaction
  uint64_t number;
  uint64_t file_size;    // File size in bytes
  InternalKey smallest;  // Smallest internal key served by table
  InternalKey largest;   // Largest internal key served by table
};
```

很容易猜到这个类应该就是用来记录SST的元数据，即Manifest中的数据。

### 6. Version

<img src="https://cdn.jsdelivr.net/gh/ravenxrz/PicBed/img/image-20200917191610062.png" alt="image-20200917200526294" style="zoom:50%;" />

来看看Version的成员：

```c++
 private:
 ...

 // 这里看出leveldb在系统中维护的version组成一个链表，且系统中可能存在多个VersionSet。每个Set维护一（多）组Version
  VersionSet* vset_;  // VersionSet to which this Version belongs
  Version* next_;     // Next version in linked list
  Version* prev_;     // Previous version in linked list
  int refs_;          // Number of live refs to this version  // 引用计数，估计和回收Version相关

  // 每层的files, 每个file都是FileMetadata
  // List of files per level
  std::vector<FileMetaData*> files_[config::kNumLevels];

  // Next file to compact based on seek stats.
  FileMetaData* file_to_compact_;
  int file_to_compact_level_;

  // compaction相关，根据compactoin_score_决定是否需要compaction
  // Level that should be compacted next and its compaction score.
  // Score < 1 means compaction is not strictly needed.  These fields
  // are initialized by Finalize().
  double compaction_score_;
  int compaction_level_;
```

next_和prev_指针，表明version之间组成一个双链表。

files_是这个version的files。

下面是和compaction相关的结构。

### 7. VersionSet

```c++
private:
class Builder;

friend class Compaction;
friend class Version;

bool ReuseManifest(const std::string& dscname, const std::string& dscbase);

void Finalize(Version* v);

void GetRange(const std::vector<FileMetaData*>& inputs, InternalKey* smallest,
InternalKey* largest);

void GetRange2(const std::vector<FileMetaData*>& inputs1,
const std::vector<FileMetaData*>& inputs2,
InternalKey* smallest, InternalKey* largest);

void SetupOtherInputs(Compaction* c);

// Save current contents to *log
Status WriteSnapshot(log::Writer* log);

void AppendVersion(Version* v);

Env* const env_;
const std::string dbname_;
const Options* const options_;
TableCache* const table_cache_;
const InternalKeyComparator icmp_;
uint64_t next_file_number_;
uint64_t manifest_file_number_;
uint64_t last_sequence_;
uint64_t log_number_;
uint64_t prev_log_number_;  // 0 or backing store for memtable being compacted

// Opened lazily
WritableFile* descriptor_file_;
log::Writer* descriptor_log_;
Version dummy_versions_;  // Head of circular doubly-linked list of versions.	链表head
Version* current_;        // == dummy_versions_.prev_							 当前version

// 下一次compaction时，每层compaction的开始key
// Per-level key at which the next compaction at that level should start.
// Either an empty string, or a valid InternalKey.
std::string compact_pointer_[config::kNumLevels];
```

![img](https://bean-li.github.io/assets/LevelDB/version_versionset.png)

LevelDB会触发Compaction，会对一些文件进行清理操作，让数据更加有序，清理后的数据放到新的版本里面，而老的数据作为原始的素材，最终是要清理掉的，但是如果有读事务位于旧的文件，那么暂时就不能删除。因此利用引用计数，只要一个Verison还活着，就不允许删除该Verison管理的所有文件。当一个Version生命周期结束，它管理的所有文件的引用计数减1.

当一个version被销毁时，每个和它想关联的file的引用计数都会-1，当引用计数小于=0时，file被删除：

```c++
Version::~Version() {
  assert(refs_ == 0);

  // Remove from linked list
  prev_->next_ = next_;
  next_->prev_ = prev_;

  // Drop references to files
  for (int level = 0; level < config::kNumLevels; level++) {
    for (size_t i = 0; i < files_[level].size(); i++) {
      FileMetaData* f = files_[level][i];
      assert(f->refs > 0);
      f->refs--;
      if (f->refs <= 0) {
        delete f;
      }
    }
  }
}
```

### 8.  VersionEdit & Version

前面说到， Version + VersionEdit = new Version，如何应用这个增量呢？

具体的操作是在VersionSet中的Builder中的。

首先可以看到，Builder是在 LogAndApply，Recover中被调用的：

<img src="https://cdn.jsdelivr.net/gh/ravenxrz/PicBed/img/image-20200917210900575.png" alt="image-20200917210900575" style="zoom:50%;" />

重点看一下LogAndApply

<img src="https://cdn.jsdelivr.net/gh/ravenxrz/PicBed/img/image-20200917210931871.png" alt="image-20200917210931871" style="zoom:50%;" />

可以看到，一共有4个函数调用了LogAndApply，DB打开时，其余3个都是和Compaction相关。

#### 1. LogAndApply

看一下LogAndApply的工作：

```c++
Status VersionSet::LogAndApply(VersionEdit* edit, port::Mutex* mu) {
    // 1. 为edit生成log_number
  if (edit->has_log_number_) {
    assert(edit->log_number_ >= log_number_);
    assert(edit->log_number_ < next_file_number_);
  } else {
    edit->SetLogNumber(log_number_);
  }

  if (!edit->has_prev_log_number_) {
    edit->SetPrevLogNumber(prev_log_number_);
  }

  edit->SetNextFile(next_file_number_);
  edit->SetLastSequence(last_sequence_);

    // 引用edit到当前版本
  Version* v = new Version(this);
  {
    Builder builder(this, current_);
    builder.Apply(edit);
    builder.SaveTo(v);
  }
    
   // 计算v的compaction相关变量，（compaction level和compaction score)
  Finalize(v);

  // Initialize new descriptor log file if necessary by creating
  // a temporary file that contains a snapshot of the current version.
  std::string new_manifest_file;
  Status s;
  if (descriptor_log_ == nullptr) {
    // No reason to unlock *mu here since we only hit this path in the
    // first call to LogAndApply (when opening the database).
    assert(descriptor_file_ == nullptr);
     // new_manifest_file为当前manifest文件路径
    new_manifest_file = DescriptorFileName(dbname_, manifest_file_number_);
    edit->SetNextFile(next_file_number_);
     // 创建文件
    s = env_->NewWritableFile(new_manifest_file, &descriptor_file_);
    if (s.ok()) {
        // 创建manifest写着
      descriptor_log_ = new log::Writer(descriptor_file_);
      s = WriteSnapshot(descriptor_log_);
    }
  }

  // Unlock during expensive MANIFEST log write
  {
    mu->Unlock();

    // Write new record to MANIFEST log
    if (s.ok()) {
      std::string record;
        // edit内容编码到record !!, 可以看一下是怎么编码的
      edit->EncodeTo(&record);
       // 写入manifest文件
      s = descriptor_log_->AddRecord(record);
      if (s.ok()) {
          // 同步
        s = descriptor_file_->Sync();
      }
      if (!s.ok()) {
        Log(options_->info_log, "MANIFEST write: %s\n", s.ToString().c_str());
      }
    }

     // 更新Current指针
    // If we just created a new descriptor file, install it by writing a
    // new CURRENT file that points to it.
    if (s.ok() && !new_manifest_file.empty()) {
      s = SetCurrentFile(env_, dbname_, manifest_file_number_);
    }

    mu->Lock();
  }

   // 插入当前version到VersionSet中
  // Install the new version
  if (s.ok()) {
     // 插入version，更新current
    AppendVersion(v);
    log_number_ = edit->log_number_;
    prev_log_number_ = edit->prev_log_number_;
  } else {
    delete v;
    if (!new_manifest_file.empty()) {
      delete descriptor_log_;
      delete descriptor_file_;
      descriptor_log_ = nullptr;
      descriptor_file_ = nullptr;
      env_->RemoveFile(new_manifest_file);
    }
  }

  return s;
}
```

current_版本的更替时机一定要注意到，LogAndApply生成新版本之后，同时将VersionEdit记录到MANIFEST文件之后。

#### 2. Builder

builder的内部数据域：

```c++
  typedef std::set<FileMetaData*, BySmallestKey> FileSet;
  struct LevelState {
    std::set<uint64_t> deleted_files;
    FileSet* added_files;
  };

  VersionSet* vset_;
  Version* base_;
  LevelState levels_[config::kNumLevels];

 // 构造函数
  // Initialize a builder with the files from *base and other info from *vset
  Builder(VersionSet* vset, Version* base) : vset_(vset), base_(base) {
    base_->Ref();
    BySmallestKey cmp;
    cmp.internal_comparator = &vset_->icmp_;
    for (int level = 0; level < config::kNumLevels; level++) {
      levels_[level].added_files = new FileSet(cmp);
    }
  }
```

核心代码：

```c++
    // 引用edit到当前版本
  Version* v = new Version(this);
  {
    Builder builder(this, current_);		// builder的base就是current_版本
    builder.Apply(edit);
    builder.SaveTo(v);
  }
```

==Apply:==

**将edit中的更改保存在builder中。**

```c++
  // Apply all of the edits in *edit to the current state.
  void Apply(VersionEdit* edit) {
    // Update compaction pointers
    for (size_t i = 0; i < edit->compact_pointers_.size(); i++) {
      const int level = edit->compact_pointers_[i].first;	// first为 level
      vset_->compact_pointer_[level] =
          edit->compact_pointers_[i].second.Encode().ToString(); // second 为 这一level开始compaction的key
    }

    // Delete files 删除文件保存在builder中
    for (const auto& deleted_file_set_kvp : edit->deleted_files_) {
      const int level = deleted_file_set_kvp.first;
      const uint64_t number = deleted_file_set_kvp.second;
      levels_[level].deleted_files.insert(number);			// delete 的 file用number表示
    }

    // Add new files
    for (size_t i = 0; i < edit->new_files_.size(); i++) {
      const int level = edit->new_files_[i].first;
      FileMetaData* f = new FileMetaData(edit->new_files_[i].second);
      f->refs = 1;

       // 计算seek_allow
      // We arrange to automatically compact this file after
      // a certain number of seeks.  Let's assume:
      //   (1) One seek costs 10ms
      //   (2) Writing or reading 1MB costs 10ms (100MB/s)
      //   (3) A compaction of 1MB does 25MB of IO:
      //         1MB read from this level
      //         10-12MB read from next level (boundaries may be misaligned)
      //         10-12MB written to next level
      // This implies that 25 seeks cost the same as the compaction
      // of 1MB of data.  I.e., one seek costs approximately the
      // same as the compaction of 40KB of data.  We are a little
      // conservative and allow approximately one seek for every 16KB
      // of data before triggering a compaction.
      f->allowed_seeks = static_cast<int>((f->file_size / 16384U));	
      if (f->allowed_seeks < 100) f->allowed_seeks = 100;

       // 如果文件之前删除了，现在又新添加了，则覆盖原来删除的文件（删除也是一个插入）
      levels_[level].deleted_files.erase(f->number);
      levels_[level].added_files->insert(f);
    }
  }
```

首先提取edit中删除的文件和新增的文件，放在Buider中。中间穿插了一个 file的seek_allow计算：**seek_allow用于触发compaction。**
1次seek的cost相当于compact 40kb的数据。保守估计，所以1次seek的cost相当于compact 16kb的数据。**目前还不懂为什么。**

==SaveTo:==

```c++
// Save the current state in *v.
  void SaveTo(Version* v) {
    BySmallestKey cmp;		// 按照smallestkey比较，如果key相同，按照file number比较，越新越好
    cmp.internal_comparator = &vset_->icmp_;
    for (int level = 0; level < config::kNumLevels; level++) {		// 一层层的合并
      // Merge the set of added files with the set of pre-existing files.
      // Drop any deleted files.  Store the result in *v.
      const std::vector<FileMetaData*>& base_files = base_->files_[level];
      std::vector<FileMetaData*>::const_iterator base_iter = base_files.begin();
      std::vector<FileMetaData*>::const_iterator base_end = base_files.end();
      const FileSet* added_files = levels_[level].added_files;
      v->files_[level].reserve(base_files.size() + added_files->size());
      // 小于added_files的key 的 当前版本中的文件，全部加入新版本中
      for (const auto& added_file : *added_files) {
        // Add all smaller files listed in base_

        for (std::vector<FileMetaData*>::const_iterator bpos =
                 std::upper_bound(base_iter, base_end, added_file, cmp);	// 查一下upper_bound函数
             base_iter != bpos; ++base_iter) {
		
          MaybeAddFile(v, level, *base_iter);
        }
		 // 加入 added_file
        MaybeAddFile(v, level, added_file);
      }
	
      // 剩余文件整合
      // Add remaining base files
      for (; base_iter != base_end; ++base_iter) {
        MaybeAddFile(v, level, *base_iter);
      }

       // 下面是检查level>0是否有overlap
#ifndef NDEBUG
      // Make sure there is no overlap in levels > 0
      if (level > 0) {
        for (uint32_t i = 1; i < v->files_[level].size(); i++) {
          const InternalKey& prev_end = v->files_[level][i - 1]->largest;
          const InternalKey& this_begin = v->files_[level][i]->smallest;
          if (vset_->icmp_.Compare(prev_end, this_begin) >= 0) {
            std::fprintf(stderr, "overlapping ranges in same level %s vs. %s\n",
                         prev_end.DebugString().c_str(),
                         this_begin.DebugString().c_str());
            std::abort();
          }
        }
      }
#endif
    }
  }

 void MaybeAddFile(Version* v, int level, FileMetaData* f) {
    if (levels_[level].deleted_files.count(f->number) > 0) {
      // File is deleted: do nothing  // 在删除列表，不用添加
    } else {
      std::vector<FileMetaData*>* files = &v->files_[level];
      if (level > 0 && !files->empty()) {	// 考虑level>0, 要求key不能overlap
        // Must not overlap
        assert(vset_->icmp_.Compare((*files)[files->size() - 1]->largest,
                                    f->smallest) < 0);
      }
      f->refs++;	//  当前新版本v 对file有引用，所以refs++
      files->push_back(f);	 // 实际压入到新版本
    }	
  }

```

#### 3. Finalize

这里决定下一次compaction的best level。

level0: 根据文件数量决定。

其余：根据该层的文件大小决定。

```c++
void VersionSet::Finalize(Version* v) {
  // Precomputed best level for next compaction
  int best_level = -1;
  double best_score = -1;

  for (int level = 0; level < config::kNumLevels - 1; level++) {
    double score;
    if (level == 0) {
      // level0 单独处理，文件数量 超过kL0_CompactionTrigger时，就trigger compaction
        
      // We treat level-0 specially by bounding the number of files
      // instead of number of bytes for two reasons:
      //
      // (1) With larger write-buffer sizes, it is nice not to do too
      // many level-0 compactions.
      //
      // (2) The files in level-0 are merged on every read and
      // therefore we wish to avoid too many files when the individual
      // file size is small (perhaps because of a small write-buffer
      // setting, or very high compression ratios, or lots of
      // overwrites/deletions).
      score = v->files_[level].size() /
              static_cast<double>(config::kL0_CompactionTrigger);	// static const int kL0_CompactionTrigger = 4;
    } else {
        // 其余level 用文件size来比较
      // Compute the ratio of current size to size limit.
      const uint64_t level_bytes = TotalFileSize(v->files_[level]);
      score =
          static_cast<double>(level_bytes) / MaxBytesForLevel(options_, level);
    }

    if (score > best_score) {
      best_level = level;
      best_score = score;
    }
  }

  v->compaction_level_ = best_level;
  v->compaction_score_ = best_score;
}
```

#### 4.  Manifest相关

==创建manifest操作writer==

```c++
// Initialize new descriptor log file if necessary by creating
  // a temporary file that contains a snapshot of the current version.
  std::string new_manifest_file;
  Status s;
  if (descriptor_log_ == nullptr) {
    // No reason to unlock *mu here since we only hit this path in the
    // first call to LogAndApply (when opening the database).
    assert(descriptor_file_ == nullptr);
    new_manifest_file = DescriptorFileName(dbname_, manifest_file_number_);
    edit->SetNextFile(next_file_number_);
    s = env_->NewWritableFile(new_manifest_file, &descriptor_file_);
    if (s.ok()) {
        // 这里创建最终的writer，从本质上来看，manifest也是一个log
      descriptor_log_ = new log::Writer(descriptor_file_);
       // 保存当前合并后的version的snapshot
      s = WriteSnapshot(descriptor_log_);
    }
  }


// 根据dbname和 manifest_file_number生成manifest文件全路径
std::string DescriptorFileName(const std::string& dbname, uint64_t number) {
  assert(number > 0);
  char buf[100];
  std::snprintf(buf, sizeof(buf), "/MANIFEST-%06llu",
                static_cast<unsigned long long>(number));
  return dbname + buf;
}

```



==写入manifest，manifest保存着这次修改的差量，即VersionEdit。==

```c++
// Unlock during expensive MANIFEST log write
  {
    mu->Unlock();

    // Write new record to MANIFEST log
    if (s.ok()) {
      std::string record;
      edit->EncodeTo(&record);	// 序列化到record中
      s = descriptor_log_->AddRecord(record);	// 写入
      if (s.ok()) {
        s = descriptor_file_->Sync();
      }
      if (!s.ok()) {
        Log(options_->info_log, "MANIFEST write: %s\n", s.ToString().c_str());
      }
    }

    // If we just created a new descriptor file, install it by writing a
    // new CURRENT file that points to it.
    if (s.ok() && !new_manifest_file.empty()) {
      s = SetCurrentFile(env_, dbname_, manifest_file_number_);
    }

    mu->Lock();
  }
```

现在来看看edit如何encode以及encode了哪些字段：

```c++
// Tag numbers for serialized VersionEdit.  These numbers are written to
// disk and should not be changed.
enum Tag {
  kComparator = 1,
  kLogNumber = 2,
  kNextFileNumber = 3,
  kLastSequence = 4,
  kCompactPointer = 5,
  kDeletedFile = 6,
  kNewFile = 7,
  // 8 was used for large value refs
  kPrevLogNumber = 9
};

// 对应VersionEdit中的成员
std::string comparator_;
uint64_t log_number_;
uint64_t prev_log_number_;
uint64_t next_file_number_;
SequenceNumber last_sequence_;
bool has_comparator_;
bool has_log_number_;
bool has_prev_log_number_;
bool has_next_file_number_;
bool has_last_sequence_;

std::vector<std::pair<int, InternalKey>> compact_pointers_;
DeletedFileSet deleted_files_;
std::vector<std::pair<int, FileMetaData>> new_files_;
```

![img](https://bean-li.github.io/assets/LevelDB/write_a_manifest.png)



==EncodeTo==

编码

```c++
void VersionEdit::EncodeTo(std::string* dst) const {
  if (has_comparator_) {
    PutVarint32(dst, kComparator);
    PutLengthPrefixedSlice(dst, comparator_);
  }
  if (has_log_number_) {
    PutVarint32(dst, kLogNumber);
    PutVarint64(dst, log_number_);
  }
  if (has_prev_log_number_) {
    PutVarint32(dst, kPrevLogNumber);
    PutVarint64(dst, prev_log_number_);
  }
  if (has_next_file_number_) {
    PutVarint32(dst, kNextFileNumber);
    PutVarint64(dst, next_file_number_);
  }
  if (has_last_sequence_) {
    PutVarint32(dst, kLastSequence);
    PutVarint64(dst, last_sequence_);
  }

  for (size_t i = 0; i < compact_pointers_.size(); i++) {
    PutVarint32(dst, kCompactPointer);
    PutVarint32(dst, compact_pointers_[i].first);  // level
    PutLengthPrefixedSlice(dst, compact_pointers_[i].second.Encode());
  }

  for (const auto& deleted_file_kvp : deleted_files_) {
    PutVarint32(dst, kDeletedFile);
    PutVarint32(dst, deleted_file_kvp.first);   // level
    PutVarint64(dst, deleted_file_kvp.second);  // file number
  }

  for (size_t i = 0; i < new_files_.size(); i++) {
    const FileMetaData& f = new_files_[i].second;
    PutVarint32(dst, kNewFile);
    PutVarint32(dst, new_files_[i].first);  // level
    PutVarint64(dst, f.number);
    PutVarint64(dst, f.file_size);
    PutLengthPrefixedSlice(dst, f.smallest.Encode());
    PutLengthPrefixedSlice(dst, f.largest.Encode());
  }
}
```

直接编码到dst中，这里的string相当于字节数组。

#### 5. 从Manifest看恢复（Open中的Recover）

VersionEdit可以序列化，存进MANIFEST文件，同样道理，MANIFEST中可以将VersionEdit一个一个的重放出来。这个重放的目的，是为了得到当前的Version 以及VersionSet。

一般来讲，当打开的DB的时候，需要获得这种信息，而这种信息的获得，靠的就是所有VersionEdit 按照次序一一回放，生成当前的Version。

==Open中的Recover==

```c++

Status DBImpl::Recover(VersionEdit* edit, bool* save_manifest) {
  mutex_.AssertHeld();		// 线程宏保护，保证该函数在独占模式下执行

   // 1. 创建db目录即相关文件
  // Ignore error from CreateDir since the creation of the DB is
  // committed only when the descriptor is created, and this directory
  // may already exist from a previous failed creation attempt.
  env_->CreateDir(dbname_);
  assert(db_lock_ == nullptr);
  Status s = env_->LockFile(LockFileName(dbname_), &db_lock_);
  if (!s.ok()) {
    return s;
  }

  if (!env_->FileExists(CurrentFileName(dbname_))) {
    if (options_.create_if_missing) {
      s = NewDB();
      if (!s.ok()) {
        return s;
      }
    } else {
      return Status::InvalidArgument(
          dbname_, "does not exist (create_if_missing is false)");
    }
  } else {
    if (options_.error_if_exists) {
      return Status::InvalidArgument(dbname_,
                                     "exists (error_if_exists is true)");
    }
  }

  // 2.执行恢复（重点看看这里）
  s = versions_->Recover(save_manifest);
  if (!s.ok()) {
    return s;
  }
  SequenceNumber max_sequence(0);

   // 3. 恢复newer 的log , 
  // Recover from all newer log files than the ones named in the
  // descriptor (new log files may have been added by the previous
  // incarnation without registering them in the descriptor).
  //
  // Note that PrevLogNumber() is no longer used, but we pay
  // attention to it in case we are recovering a database
  // produced by an older version of leveldb.
  const uint64_t min_log = versions_->LogNumber();
  const uint64_t prev_log = versions_->PrevLogNumber();
  std::vector<std::string> filenames;
  s = env_->GetChildren(dbname_, &filenames);
  if (!s.ok()) {
    return s;
  }
  std::set<uint64_t> expected;
  versions_->AddLiveFiles(&expected);
  uint64_t number;
  FileType type;
  std::vector<uint64_t> logs;
  for (size_t i = 0; i < filenames.size(); i++) {
    if (ParseFileName(filenames[i], &number, &type)) {
      expected.erase(number);
      if (type == kLogFile && ((number >= min_log) || (number == prev_log)))
        logs.push_back(number);
    }
  }
  if (!expected.empty()) {
    char buf[50];
    std::snprintf(buf, sizeof(buf), "%d missing files; e.g.",
                  static_cast<int>(expected.size()));
    return Status::Corruption(buf, TableFileName(dbname_, *(expected.begin())));
  }

  // Recover in the order in which the logs were generated
  std::sort(logs.begin(), logs.end());
  for (size_t i = 0; i < logs.size(); i++) {
    s = RecoverLogFile(logs[i], (i == logs.size() - 1), save_manifest, edit,
                       &max_sequence);
    if (!s.ok()) {
      return s;
    }

    // The previous incarnation may not have written any MANIFEST
    // records after allocating this log number.  So we manually
    // update the file number allocation counter in VersionSet.
    versions_->MarkFileNumberUsed(logs[i]);
  }

  if (versions_->LastSequence() < max_sequence) {
    versions_->SetLastSequence(max_sequence);
  }

  return Status::OK();
}
```



==核心代码:s = versions_->Recover(save_manifest)==

第一步：读取manifest，生成文件句柄。

```c++
  // Read "CURRENT" file, which contains a pointer to the current manifest file
  std::string current;
  Status s = ReadFileToString(env_, CurrentFileName(dbname_), &current);
  if (!s.ok()) {
    return s;
  }
  if (current.empty() || current[current.size() - 1] != '\n') {
    return Status::Corruption("CURRENT file does not end with newline");
  }
  current.resize(current.size() - 1);

  std::string dscname = dbname_ + "/" + current;
  SequentialFile* file;
  s = env_->NewSequentialFile(dscname, &file);
  if (!s.ok()) {
    if (s.IsNotFound()) {
      return Status::Corruption("CURRENT points to a non-existent file",
                                s.ToString());
    }
    return s;
  }
```

第二步：按照VersionEdit的序列化顺序逐步恢复，**恢复数据保存在Builder中。**

```c++
 bool have_log_number = false;
  bool have_prev_log_number = false;
  bool have_next_file = false;
  bool have_last_sequence = false;
  uint64_t next_file = 0;
  uint64_t last_sequence = 0;
  uint64_t log_number = 0;
  uint64_t prev_log_number = 0;
  Builder builder(this, current_);

  {
    LogReporter reporter;
    reporter.status = &s;
    log::Reader reader(file, &reporter, true /*checksum*/,
                       0 /*initial_offset*/);
    Slice record;
    std::string scratch;
    while (reader.ReadRecord(&record, &scratch) && s.ok()) {
      VersionEdit edit;
      s = edit.DecodeFrom(record);
      if (s.ok()) {
        if (edit.has_comparator_ &&
            edit.comparator_ != icmp_.user_comparator()->Name()) {
          s = Status::InvalidArgument(
              edit.comparator_ + " does not match existing comparator ",
              icmp_.user_comparator()->Name());
        }
      }

      if (s.ok()) {
        builder.Apply(&edit);
      }

      if (edit.has_log_number_) {
        log_number = edit.log_number_;
        have_log_number = true;
      }

      if (edit.has_prev_log_number_) {
        prev_log_number = edit.prev_log_number_;
        have_prev_log_number = true;
      }

      if (edit.has_next_file_number_) {
        next_file = edit.next_file_number_;
        have_next_file = true;
      }

      if (edit.has_last_sequence_) {
        last_sequence = edit.last_sequence_;
        have_last_sequence = true;
      }
    }
  }
  delete file;
  file = nullptr;
```

第三步：将所有差量聚合到一个新版本。

```c++
  if (s.ok()) {
    Version* v = new Version(this);
    builder.SaveTo(v);
    // Install recovered version
    Finalize(v);
    AppendVersion(v);    // 将当前版本插入到VersionSet中，并更新current_指针到当前版本
    manifest_file_number_ = next_file;
    next_file_number_ = next_file + 1;
    last_sequence_ = last_sequence;
    log_number_ = log_number;
    prev_log_number_ = prev_log_number;

    // See if we can reuse the existing MANIFEST file. 
    if (ReuseManifest(dscname, current)) {	// 是否重用主要看当前manifest的size是否超过阈值（options中可配置）
      // No need to save new manifest
    } else {
      *save_manifest = true;
    }
  }

  return s;


void VersionSet::AppendVersion(Version* v) {
  // Make "v" current
  assert(v->refs_ == 0);
  assert(v != current_);
  if (current_ != nullptr) {
    current_->Unref();
  }
  current_ = v;
  v->Ref();

  // Append to linked list
  v->prev_ = dummy_versions_.prev_;
  v->next_ = &dummy_versions_;
  v->prev_->next_ = v;
  v->next_->prev_ = v;
}

```



## 2. 操作解析

### 1.options构造

```c++
leveldb::Options opts;
opts.create_if_missing = true;
leveldb::Status status = leveldb::DB::Open(opts,"/home/raven/Projects/leveldb_learning/mytest/testdb",&db);
```

看看opts是如何构造的.

```c++
// leveldb/db/db_impl.h
namespace leveldb {

Options::Options() : comparator(BytewiseComparator()), env(Env::Default()) {}

}  // namespace leveldb

```

默认的Options初始化了：

1. BytewiseComparator
2. 默认的Evn

### 2. DB::Open

DB::Open为DB类的静态函数。

```c++
Status DB::Open(const Options& options, const std::string& dbname, DB** dbptr) {
  *dbptr = nullptr;

  // 1. 创建DBImpl
  DBImpl* impl = new DBImpl(options, dbname);
  impl->mutex_.Lock();
  VersionEdit edit;
  // Recover handles create_if_missing, error_if_exists
  bool save_manifest = false;
  // 2. 恢复
  Status s = impl->Recover(&edit, &save_manifest);
  if (s.ok() && impl->mem_ == nullptr) {
    // Create new log and a corresponding memtable.
    uint64_t new_log_number = impl->versions_->NewFileNumber();
    WritableFile* lfile;
    s = options.env->NewWritableFile(LogFileName(dbname, new_log_number),
                                     &lfile);
    if (s.ok()) {
      edit.SetLogNumber(new_log_number);
      impl->logfile_ = lfile;
      impl->logfile_number_ = new_log_number;
      impl->log_ = new log::Writer(lfile);
      impl->mem_ = new MemTable(impl->internal_comparator_);
      impl->mem_->Ref();
    }
  }
  if (s.ok() && save_manifest) {
    edit.SetPrevLogNumber(0);  // No older logs needed after recovery.
    edit.SetLogNumber(impl->logfile_number_);
    s = impl->versions_->LogAndApply(&edit, &impl->mutex_);
  }
  if (s.ok()) {
    impl->RemoveObsoleteFiles();
    impl->MaybeScheduleCompaction();
  }
  impl->mutex_.Unlock();
  if (s.ok()) {
    assert(impl->mem_ != nullptr);
    *dbptr = impl;
  } else {
    delete impl;
  }
  return s;
}
```



