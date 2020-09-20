## 1. 整体架构

首先给出leveldb的整体架构图：

<img src="http://images0.cnblogs.com/blog2015/603001/201506/181017380918348.png" alt="http://images0.cnblogs.com/blog2015/603001/201506/181017380918348.png" style="zoom:67%;" />

各个组件的位置以及作用：

**内存**：MemTable 和 Immutable MemTable

 **磁盘**：Current 文件，Manifest 文件，log 文件以及 SSTable 文件

   其中，log 文件、MemTable、SSTable 文件都是用来存储 k-v 记录的

​    SSTable 中的某个文件属于特定层级，而且其存储的记录是 key 有序的，那么必然有文件中的最小 key 和最大 key，这是非常重要的信息。

   Manifest 就记载了 SSTable 各个文件的管理信息，比如属于哪个 Level，文件名称叫啥，最小 key 和最大 key 各自是多少。下图是 Manifest 所存储内容的示意：

![img](https://img-blog.csdnimg.cn/20190314203724514.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

另外，在 LevleDb 的运行过程中，随着 Compaction 的进行，SSTable 文件会发生变化，会有新的文件产生，老的文件被废弃，Manifest 也会跟着反映这种变化，此时往往会新生成 Manifest 文件来记载这种变化，而 Current 则用来指出哪个 Manifest 文件才是我们关心的那个 Manifest 文件。

## 2. 读写流程

**写操作流程**

  1、顺序写入磁盘 log 文件；

  2、写入内存 memtable（采用 skiplist 结构实现）；

  3、写入磁盘 SST 文件 (sorted string table files)，这步是数据归档的过程（永久化存储）；

1. log 文件的作用是是用于系统崩溃恢复而不丢失数据，假如没有 Log 文件，因为写入的记录刚开始是保存在内存中的，此时如果系统崩溃，内存中的数据还没有来得及 Dump 到磁盘，所以会丢失数据；
2. 在写 memtable 时，如果其达到 check point（满员）的话，会将其改成 immutable memtable（只读），然后等待 dump 到磁盘 SST 文件中，此时也会生成新的 memtable 供写入新数据；
3. memtable 和 sst 文件中的 key 都是有序的，log 文件的 key 是无序的；
4. LevelDB 删除操作也是插入，只是标记 Key 为删除状态，真正的删除要到 Compaction 的时候才去做真正的操作；
5. LevelDB 没有更新接口，如果需要更新某个 Key 的值，只需要插入一条新纪录即可；或者先删除旧记录，再插入也可。

​    插入一条新记录的过程也很简单，即先查找合适的插入位置，然后修改相应的链接指针将新记录插入即可。完成这一步，写入记录就算完成了，所以一个插入记录操作涉及一次磁盘文件追加写和内存 SkipList 插入操作，这是为何 levelDb 写入速度如此高效的根本原因。

**读操作流程**

  1、在内存中依次查找 memtable、immutable memtable；

  2、如果配置了 cache，查找 cache；

  3、根据 mainfest 索引文件，在磁盘中查找 SST 文件；

![img](http://images0.cnblogs.com/blog2015/603001/201506/181048158107682.png)

## 3. log文件

LevelDb 对于一个 log 文件，会把它切割成以 32K 为单位的物理 Block，每次读取的单位以一个 Block 作为基本读取单位，下图展示的 log 文件由 3 个 Block 构成，所以从物理布局来讲，一个 log 文件就是由连续的 32K 大小 Block 构成的。

![img](https://img-blog.csdnimg.cn/20190314203727797.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

物理上分块，逻辑上存储Record：

![img](https://img-blog.csdnimg.cn/20190314203728486.png)

  记录头包含三个字段，ChechSum 是对 “类型” 和“数据”字段的校验码，为了避免处理不完整或者是被破坏的数据，当 LevelDb 读取记录数据时候会对数据进行校验，如果发现和存储的 CheckSum 相同，说明数据完整无破坏，可以继续后续流程。“记录长度” 记载了数据的大小，“数据” 则是上面讲的 Key:Value 数值对，“类型” 字段则指出了每条记录的逻辑结构和 log 文件物理分块结构之间的关系，具体而言，主要有以下四种类型：FULL/FIRST/MIDDLE/LAST。

如果 一个Record的size在一个block内，则type=FULL

否则Record将会跨越多个block，则可能出现上图中的情况，分别对应FIRST/MIDDLE/LAST

## 4. MemTable

LevelDb的MemTable提供了将KV数据写入，删除以及读取KV记录的操作接口，但是事实上Memtable并不存在真正的删除操作,删除某个Key的Value在Memtable内是作为插入一条记录实施的，但是会打上一个Key的删除标记，真正的删除操作是Lazy的，会在以后的Compaction过程中去掉这个KV。

需要注意的是，**LevelDb的Memtable中KV对是根据Key大小有序存储的**，在系统插入新的KV时，LevelDb要把这个KV插到合适的位置上以保持这种Key有序性。其实，**LevelDb的Memtable类只是一个接口类**，真正的操作是通过背后的SkipList来做的，包括插入操作和读取操作等，**所以Memtable的核心数据结构是一个SkipList。**

SkipList是平衡树的一种替代数据结构，但是和红黑树不相同的是，SkipList对于树的平衡的实现是基于一种随机化的算法的，这样也就是说SkipList的插入和删除的工作是比较简单的。

​    [SkipList参考博客](http://www.cnblogs.com/xuqiang/archive/2011/05/22/2053516.html)，

SkipList不仅是维护有序数据的一个简单实现，而且相比较平衡树来说，在插入数据的时候可以避免频繁的树节点调整操作，所以写入效率是很高的，LevelDb整体而言是个高写入系统，SkipList在其中应该也起到了很重要的作用。Redis为了加快插入操作，也使用了SkipList来作为内部实现数据结构。

## 5. SSTable文件

LevelDb不同层级有很多SSTable文件（以后缀.sst为特征），所有.sst文件内部布局都是一样的。上节介绍Log文件是物理分块的，SSTable也一样会将文件划分为固定大小的物理存储块，但是两者逻辑布局大不相同，根本原因是：Log文件中的记录是Key无序的，即先后记录的key大小没有明确大小关系，而.sst文件内部则是根据记录的Key由小到大排列的，从下面介绍的SSTable布局可以体会到Key有序是为何如此设计.sst文件结构的关键。

![img](https://img-blog.csdnimg.cn/20190314203728756.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

​                                        图6-1 .sst文件的分块结构

​    图6-1展示了一个.sst文件的物理划分结构，同Log文件一样，也是划分为固定大小的存储块，每个Block分为三个部分，红色部分是数据存储区， 蓝色的Type区用于标识数据存储区是否采用了数据压缩算法（Snappy压缩或者无压缩两种），CRC部分则是数据校验码，用于判别数据是否在生成和传输中出错。

![img](https://img-blog.csdnimg.cn/20190314203728795.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

​                                       图6-2 .sst逻辑布局

　  从图6-2可以看出，从大的方面，可以将.sst文件划分为数据存储区和数据管理区，数据存储区存放实际的Key:Value数据，数据管理区则提供一些索引指针等管理数据，目的是更快速便捷的查找相应的记录。两个区域都是在上述的分块基础上的，就是说文-件的前面若干块实际存储KV数据，后面数据管理区存储管理数据。管理数据又分为四种不同类型：紫色的Meta Block，红色的MetaBlock 索引和蓝色的数据索引块以及一个文件尾部块。

​    LevelDb 1.2版对于Meta Block尚无实际使用，只是保留了一个接口，估计会在后续版本中加入内容，下面我们看看数据索引区和文件尾部Footer的内部结构。

![img](https://img-blog.csdnimg.cn/20190314203728786.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

​                                         图6-3 数据索引

​    图6-3是数据索引的内部结构示意图。再次强调一下，Data Block内的KV记录是按照Key由小到大排列的，数据索引区的每条记录是对某个Data Block建立的索引信息，每条索引信息包含三个内容，以图4.3所示的数据块i的索引Index i来说：红色部分的第一个字段记载大于等于数据块i中最大的Key值的那个Key，第二个字段指出数据块i在.sst文件中的起始位置，第三个字段指出Data Block i的大小（有时候是有数据压缩的）。

​    后面两个字段好理解，是用于定位数据块在文件中的位置的，第一个字段需要详细解释一下，在索引里保存的这个Key值未必一定是某条记录的Key,以图4.3的例子来说，假设数据块i 的最小Key=“samecity”，最大Key=“the best”;数据块i+1的最小Key=“the fox”,最大Key=“zoo”,那么对于数据块i的索引Index i来说，其第一个字段记载大于等于数据块i的最大Key(“the best”)同时要小于数据块i+1的最小Key(“the fox”)，所以例子中Index i的第一个字段是：“the best”，这个是满足要求的；而Index i+1的第一个字段则是“zoo”，即数据块i+1的最大Key。

​    文件末尾Footer块的内部结构见图6-4，metaindex_handle指出了metaindex block的起始位置和大小；inex_handle指出了index Block的起始地址和大小；这两个字段可以理解为索引的索引，是为了正确读出索引值而设立的，后面跟着一个填充区和魔数。

![img](https://img-blog.csdnimg.cn/20190314203727895.png)

​                                       图6-4 Footer结构图

​    上面主要介绍的是数据管理区的内部结构，下面我们看看数据区的一个Block的数据部分内部是如何布局的（图4.1中的红色部分），图6-5是其内部布局示意图。

![img](https://img-blog.csdnimg.cn/20190314203728921.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

​                                       图6-5 数据Block内部结构

​    图6-5中可以看出，其内部也分为两个部分，前面是一个个KV记录，其顺序是根据Key值由小到大排列的，在Block尾部则是一些“重启点”（Restart Point）,其实是一些指针，指出Block内容中的一些记录位置。

   “重启点”是干什么的呢？我们一再强调，Block内容里的KV记录是按照Key大小有序的，这样的话，相邻的两条记录很可能Key部分存在重叠，比如key i=“the Car”，Key i+1=“the color”,那么两者存在重叠部分“the c”，为了减少Key的存储量，Key i+1可以只存储和上一条Key不同的部分“olor”，两者的共同部分从Key i中可以获得。记录的Key在Block内容部分就是这么存储的，主要目的是减少存储开销。“重启点”的意思是：在这条记录开始，不再采取只记载不同的Key部分，而是重新记录所有的Key值，假设Key i+1是一个重启点，那么Key里面会完整存储“the color”，而不是采用简略的“olor”方式。Block尾部就是指出哪些记录是这些重启点。

 

![img](https://img-blog.csdnimg.cn/20190314203728806.png)

​                                         图6-6 记录格式

​    在Block内容区，每个KV记录的内部结构是怎样的？图4.6给出了其详细结构，每个记录包含5个字段：key共享长度，比如上面的“olor”记录， 其key和上一条记录共享的Key部分长度是“the c”的长度，即5；key非共享长度，对于“olor”来说，是4；value长度指出Key:Value中Value的长度，在后面的Value内容字段中存储实际的Value值；而key非共享内容则实际存储“olor”这个Key字符串。

**SSTable文件细节**

​    1、每个SST文件大小上限为2MB，所以，LevelDB通常存储了大量的SST文件；

​    2、SST文件由若干个4K大小的blocks组成，block也是读/写操作的最小单元；

​    3、SST文件的最后一个block是一个index，指向每个data block的起始位置，以及每个block第一个entry的key值（block内的key有序存储）；

​    4、使用Bloom filter加速查找，只要扫描index，就可以快速找出所有可能包含指定entry的block。

​    5、同一个block内的key可以共享前缀（只存储一次），这样每个key只要存储自己唯一的后缀就行了。如果block中只有部分key需要共享前缀，在这部分key与其它key之间插入"reset"标识。

由log直接读取的entry会写到Level 0的SST中（最多4个文件）；

***\*Level0\**** ***\*注意事项：\****

​    当Level 0的4个文件都存储满了，会选择其中一个文件Compact到Level 1的SST中；

***\*注意：\****Level 0的SSTable文件和其它Level的文件相比有特殊性：这个层级内的.sst文件，两个文件可能存在key重叠，比如有两个level 0的sst文件，文件A和文件B，文件A的key范围是：{bar, car}，文件B的Key范围是{blue,samecity}，那么很可能两个文件都存在key=”blood”的记录。对于其它Level的SSTable文件来说，则不会出现同一层级内.sst文件的key重叠现象，就是说Level L中任意两个.sst文件，那么可以保证它们的key值是不会重叠的。

**level层次**

   Log：最大4MB (可配置), 会写入Level 0；

  Level 0：最多4个SST文件,；

  Level 1：总大小不超过10MB；

  Level 2：总大小不超过100MB；

  Level 3+：总大小不超过上一个Level ×10的大小。

  比如：0 ↠ 4 SST, 1 ↠ 10M, 2 ↠ 100M, 3 ↠ 1G, 4 ↠ 10G, 5 ↠ 100G, 6 ↠ 1T, 7 ↠ 10T

**读写操作流程：**

​    在读操作中，要查找一条entry，先查找log，如果没有找到，然后在Level 0中查找，如果还是没有找到，再依次往更底层的Level顺序查找；如果查找了一条不存在的entry，则要遍历一遍所有的Level才能返回"Not Found"的结果。

​    在写操作中，新数据总是先插入开头的几个Level中，开头的这几个Level存储量也比较小，因此，对某条entry的修改或删除操作带来的性能影响就比较可控。

​    可见，SST采取分层结构是为了最大限度减小插入新entry时的开销；

## 6. Compaction

​    对于LevelDb来说，写入记录操作很简单，删除记录仅仅写入一个删除标记就算完事，但是读取记录比较复杂，需要在内存以及各个层级文件中依照新鲜程度依次查找，代价很高。为了加快读取速度，levelDb采取了compaction的方式来对已有的记录进行整理压缩，通过这种方式，来删除掉一些不再有效的KV数据，减小数据规模，减少文件数量等。

​    LevelDb的compaction机制和过程与Bigtable所讲述的是基本一致的，Bigtable中讲到三种类型的compaction: minor 、major和full：

1. minor Compaction，就是把memtable中的数据导出到SSTable文件中；
2. major compaction就是合并不同层级的SSTable文件；
3. full compaction就是将所有SSTable进行合并；

  LevelDb包含其中两种，minor和major。

  Minor compaction 的目的是当内存中的memtable大小到了一定值时，将内容保存到磁盘文件中，如下图：

![img](https://img-blog.csdnimg.cn/20190314203727830.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

​                                           图7-1 compaction方式

   immutable memtable其实是一个SkipList，其中的记录是根据key有序排列的，遍历key并依次写入一个level 0 的新建SSTable文件中，写完后建立文件的index 数据，这样就完成了一次minor compaction。从图中也可以看出，对于被删除的记录，在minor compaction过程中并不真正删除这个记录，原因也很简单，这里只知道要删掉key记录，但是这个KV数据在哪里？那需要复杂的查找，所以在minor compaction的时候并不做删除，只是将这个key作为一个记录写入文件中，至于真正的删除操作，在以后更高层级的compaction中会去做。

​    当某个level下的SSTable文件数目超过一定设置值后，levelDb会从这个level的SSTable中选择一个文件（level>0），将其和高一层级的level+1的SSTable文件合并，这就是major compaction。

​    由于大于0的层级中，每个SSTable文件内的Key都是由小到大有序存储的，而且不同文件之间的key范围（文件内最小key和最大key之间）不会有任何重叠。Level 0的SSTable文件有些特殊，尽管每个文件也是根据Key由小到大排列，但是因为level 0的文件是通过minor compaction直接生成的，所以任意两个level 0下的两个sstable文件可能再key范围上有重叠。所以在做major compaction的时候，对于大于level 0的层级，选择其中一个文件就行，但是对于level 0来说，指定某个文件后，本level中很可能有其他SSTable文件的key范围和这个文件有重叠，这种情况下，要找出所有有重叠的文件和level 1的文件进行合并，即level 0在进行文件选择的时候，可能会有多个文件参与major compaction。

​    LevelDb在选定某个level进行compaction后，还要选择是具体哪个文件要进行compaction，比如这次是文件A进行compaction，那么下次就是在key range上紧挨着文件A的文件B进行compaction，这样每个文件都会有机会轮流和高层的level 文件进行合并。

​    如果选好了level L的文件A和level L+1层的文件进行合并，那么问题又来了，应该选择level L+1哪些文件进行合并？levelDb选择L+1层中和文件A在key range上有重叠的所有文件来和文件A进行合并。也就是说，选定了level L的文件A，之后在level L+1中找到了所有需要合并的文件B,C,D…..等等。剩下的问题就是具体是如何进行major 合并的？就是说给定了一系列文件，每个文件内部是key有序的，如何对这些文件进行合并，使得新生成的文件仍然Key有序，同时抛掉哪些不再有价值的KV 数据。

![img](https://img-blog.csdnimg.cn/20190314203727440.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

​                                           图7-2  level合并过程

Major compaction过程：对多个文件采用多路归并排序的方式，依次找出其中最小的Key记录，也就是对多个文件中的所有记录重新进行排序。之后采取一定的标准判断这个Key是否还需要保存，如果判断没有保存价值，那么直接抛掉，如果觉得还需要继续保存，那么就将其写入level L+1层中新生成的一个SSTable文件中。就这样对KV数据一一处理，形成了一系列新的L+1层数据文件，之前的L层文件和L+1层参与compaction 的文件数据此时已经没有意义了，所以全部删除。这样就完成了L层和L+1层文件记录的合并过程。

   那么在major compaction过程中，**判断一个KV记录是否抛弃的标准是什么呢？**其中一个标准是：对于某个key来说，如果在小于L层中存在这个Key，那么这个KV在major compaction过程中可以抛掉。因为我们前面分析过，对于层级低于L的文件中如果存在同一Key的记录，那么说明对于Key来说，有更新鲜的Value存在，那么过去的Value就等于没有意义了，所以可以删除。

## 7. Version

**Version** 保存了当前磁盘以及内存中所有的文件信息，一般只有一个Version叫做"current" version（当前版本）。Leveldb还保存了一系列的历史版本，这些历史版本有什么作用呢？

​    当一个Iterator创建后，Iterator就引用到了current version(当前版本)，只要这个Iterator不被delete那么被Iterator引用的版本就会一直存活。这就意味着当你用完一个Iterator后，需要及时删除它。

​    当一次Compaction结束后（会生成新的文件，合并前的文件需要删除），Leveldb会创建一个新的版本作为当前版本，原先的当前版本就会变为历史版本。

**VersionSet** 是所有Version的集合，管理着所有存活的Version。

**VersionEdit** 表示Version之间的变化，相当于delta 增量，表示有增加了多少文件，删除了文件。下图表示他们之间的关系。

   Version0 +VersionEdit-->Version1

   VersionEdit会保存到MANIFEST文件中，当做数据恢复时就会从MANIFEST文件中读出来重建数据。

   leveldb的这种版本的控制，让我想到了双buffer切换，双buffer切换来自于图形学中，用于解决屏幕绘制时的闪屏问题，在服务器编程中也有用处。

   比如我们的服务器上有一个字典库，每天我们需要更新这个字典库，我们可以新开一个buffer，将新的字典库加载到这个新buffer中，等到加载完毕，将字典的指针指向新的字典库。

   leveldb的version管理和双buffer切换类似，但是如果原version被某个iterator引用，那么这个version会一直保持，直到没有被任何一个iterator引用，此时就可以删除这个version。

## 8. Cache

​    前面讲过对于levelDb来说，读取操作如果没有在内存的memtable中找到记录，要多次进行磁盘访问操作。假设最优情况，即第一次就在level 0中最新的文件中找到了这个key，那么也需要读取2次磁盘，一次是将SSTable的文件中的index部分读入内存，这样根据这个index可以确定key是在哪个block中存储；第二次是读入这个block的内容，然后在内存中查找key对应的value。

​    LevelDb中引入了两个不同的Cache:Table Cache和Block Cache。其中Block Cache是配置可选的，即在配置文件中指定是否打开这个功能。

![img](https://img-blog.csdnimg.cn/20190314203728954.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

​                                        图8-1 cache结构图

   如图8-1，在Table Cache中，key值是SSTable的文件名称，Value部分包含两部分，一个是指向磁盘打开的SSTable文件的文件指针，这是为了方便读取内容；另外一个是指向内存中这个SSTable文件对应的Table结构指针，table结构在内存中，保存了SSTable的index内容以及用来指示block cache用的cache_id ,当然除此外还有其它一些内容。

​    比如在get(key)读取操作中，如果levelDb确定了key在某个level下某个文件A的key range范围内，那么需要判断是不是文件A真的包含这个KV。此时，levelDb会首先查找Table Cache，看这个文件是否在缓存里，如果找到了，那么根据index部分就可以查找是哪个block包含这个key。如果没有在缓存中找到文件，那么打开SSTable文件，将其index部分读入内存，然后插入Cache里面，去index里面定位哪个block包含这个Key 。如果确定了文件哪个block包含这个key，那么需要读入block内容，这是第二次读取。

![img](https://img-blog.csdnimg.cn/20190314203728625.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3R1d2VucWkyMDEz,size_16,color_FFFFFF,t_70)

​                                        图8-2 Block Cache结构图

Block Cache是为了加快这个过程的，其中的key是文件的cache_id加上这个block在文件中的起始位置block_offset。而value则是这个Block的内容。

   如果levelDb发现这个block在block cache中，那么可以避免读取数据，直接在cache里的block内容里面查找key的value就行，如果没找到呢？那么读入block内容并把它插入block cache中。levelDb就是这样通过两个cache来加快读取速度的。从这里可以看出，如果读取的数据局部性比较好，也就是说要读的数据大部分在cache里面都能读到，那么读取效率应该还是很高的，而如果是对key进行顺序读取效率也应该不错，因为一次读入后可以多次被复用。但是如果是随机读取，您可以推断下其效率如何。