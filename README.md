# confZNS_
为了进一步探索confzns的地址映射功能，我将其中重要的函数抽取出来写成独立的可运行文件。在这个文件中，保留了confzns地址映射的原始逻辑，同时也支持不同映射配置，但是他相比于整个confzns的结构更加轻量。有助于我们专注于地址映射的研究。同时也避免了每次编译所占用的大量时间和空间，可以更为直接的对confzns的地址映射进行研究。

其中的log文件包含了不同类型的配置和输出信息，方便调试和更改映射逻辑。

在探索的过程中，我发现源代码中的位置映射不是十分合理
![img_v2_c6ee3b1e-84aa-46d3-8a2b-9f104fc31e1g](https://github.com/Weipp7/confZNS_/assets/73998546/c715fabd-1424-4471-b022-af689634d2f4)


显然不同的slba映射到了相同的ppa

整体的映射逻辑没有太大的问题，我在修改过程中尝试将 `zns_get_multichnlway_ppn_idx`函数中的 `uint64_t slpa = (slba >> 3) / (ZNS_PAGE_SIZE/MIN_DISCARD_GRANULARITY);`改为 `uint64_t slpa = (slba >> 2) / (ZNS_PAGE_SIZE/MIN_DISCARD_GRANULARITY);`得到了理想的映射结果。

这么改的原因是：

修改前的逻辑是将逻辑地址 `slba` 右移3位（除以8），然后再除以 `(ZNS_PAGE_SIZE/MIN_DISCARD_GRANULARITY)`。这里的 `(ZNS_PAGE_SIZE/MIN_DISCARD_GRANULARITY)` 表示逻辑页面大小除以物理页面大小，即 16k/4k = 4，得到的结果是逻辑页面的索引。

修改后的逻辑是将逻辑地址 `slba` 右移2位（除以4），然后再除以 `(ZNS_PAGE_SIZE/MIN_DISCARD_GRANULARITY)`。这里的 `(ZNS_PAGE_SIZE/MIN_DISCARD_GRANULARITY)` 表示逻辑页面大小除以物理页面大小，即 16k/4k = 4，得到的结果是逻辑页面的索引。

对比两种情况，我们可以发现：

修改前：`slba` 右移3位后得到的结果是 0、2、4、6，然后再除以4得到的结果是 0、0、1、1。

修改后：`slba` 右移2位后得到的结果是 0、4、8、12，然后再除以4得到的结果是 0、1、2、3。

从结果上看，修改后的逻辑在不同的 `slba` 值下得到了不同的逻辑页面索引，而修改前的逻辑在一定范围内的 `slba` 值下得到了相同的逻辑页面索引。

这部分仅仅对confzns的地址映射做些实际操作的探索，关于原理的解析可以访问[confZNS 分析](https://weipp7.github.io/posts/0.html)

[源码参考](https://github.com/DKU-StarLab/ConfZNS/tree/master)
