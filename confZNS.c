#include <stdio.h>
#include <stdint.h>

#define KiB (1024)
#define MiB (1024 * 1024)

#define NAND_READ_LATENCY 100 // 这里假设读取延迟为100纳秒
#define NAND_PROG_LATENCY 200 // 这里假设编程延迟为200纳秒
#define NAND_ERASE_LATENCY 1000 // 这里假设擦除延迟为1000纳秒
#define NAND_CHNL_PAGE_TRANSFER_LATENCY 50 // 这里假设通道页传输延迟为50纳秒

#define MIN_DISCARD_GRANULARITY (4 * KiB)
#define ZNS_PAGE_SIZE (16 * KiB)
#define NVME_DEFAULT_ZONE_SIZE (64 * MiB)

struct FemuCtrl {
    uint32_t num_zones; // 这里假设有一定数量的zones
    struct zns *zns; // 添加zns字段
    uint32_t zone_size_log2; // 添加zone_size_log2字段
};

struct NvmeNamespace {
    struct FemuCtrl *ctrl;
};

struct zns {
    struct zns_ssdparams *sp;
};

struct zns_ssdparams {
    uint32_t pg_rd_lat;
    uint32_t pg_wr_lat;
    uint32_t blk_er_lat;
    uint32_t ch_xfer_lat;

    uint32_t nchnls;
    uint32_t chnls_per_zone;
    uint32_t zones;
    uint32_t ways;
    uint32_t ways_per_zone;
    uint32_t dies_per_chip;
    uint32_t planes_per_die;
    uint32_t register_model;
};

typedef struct FemuCtrl FemuCtrl;
typedef struct NvmeNamespace NvmeNamespace;

// 函数声明
static inline uint32_t zns_zone_idx(NvmeNamespace *ns, uint64_t slba);
static inline uint64_t zns_get_multichnlway_ppn_idx(NvmeNamespace *ns, uint64_t slba);

// 请注意，这里的具体数值可能需要根据实际情况进行调整
void setup_params(struct zns_ssdparams *spp, FemuCtrl *ctrl) {
    spp->pg_rd_lat = NAND_READ_LATENCY;
    spp->pg_wr_lat = NAND_PROG_LATENCY;
    spp->blk_er_lat = NAND_ERASE_LATENCY;
    spp->ch_xfer_lat = NAND_CHNL_PAGE_TRANSFER_LATENCY;

    spp->nchnls = 4; // 假设有 个通道
    spp->chnls_per_zone = 1;
    spp->zones = ctrl->num_zones; // 假设zones数量等于num_zones
    spp->ways = 1; // 假设每个zone有 个way
    spp->ways_per_zone = 1;
    spp->dies_per_chip = 1; // 假设每个chip有 个die
    spp->planes_per_die = 2; // 假设每个die有 个plane
    spp->register_model = 1;

    ctrl->zone_size_log2 = 17;
}

static inline uint32_t zns_zone_idx(NvmeNamespace *ns, uint64_t slba) {
    FemuCtrl *n = ns->ctrl;
    return (n->zone_size_log2 > 0 ? slba >> n->zone_size_log2 : slba / NVME_DEFAULT_ZONE_SIZE);
}

uint64_t zns_get_multichnlway_ppn_idx(NvmeNamespace *ns, uint64_t slba) {
    FemuCtrl *n = ns->ctrl;
    struct zns *zns = n->zns;
    struct zns_ssdparams *spp = zns->sp;
    uint64_t zone_size = NVME_DEFAULT_ZONE_SIZE / ZNS_PAGE_SIZE;

    uint64_t zidx = zns_zone_idx(ns, slba);
    uint64_t slpa = (slba >> 2) / (ZNS_PAGE_SIZE / MIN_DISCARD_GRANULARITY);
    uint64_t slpa_origin = slpa;
    slpa = slpa / spp->planes_per_die;
    uint64_t num_of_concurrent_zones = (spp->nchnls / spp->chnls_per_zone) * (spp->ways / spp->ways_per_zone);
    uint64_t BIG_ITER = zidx / num_of_concurrent_zones;
    uint64_t BIG_ITER_VAL = zone_size * num_of_concurrent_zones * spp->planes_per_die;
    uint64_t small_iter = zidx % (spp->nchnls / spp->chnls_per_zone);
    uint64_t small_iter_val = ((spp->chnls_per_zone) % (spp->nchnls)) * spp->planes_per_die;
    uint64_t med_iter = (zidx / (spp->nchnls / spp->chnls_per_zone)) % ((spp->ways / spp->ways_per_zone));
    uint64_t med_iter_val = spp->nchnls * spp->ways_per_zone * spp->planes_per_die;

    uint64_t start = BIG_ITER * BIG_ITER_VAL + med_iter * med_iter_val + small_iter * small_iter_val;

    uint64_t iter_chnl_way = (slpa / spp->chnls_per_zone / spp->ways_per_zone) % (zone_size / spp->chnls_per_zone / spp->ways_per_zone);
    uint64_t iter_chnl_way_val = spp->nchnls * spp->ways * spp->planes_per_die;
    uint64_t iter_chnl = (slpa / spp->chnls_per_zone) % (spp->ways_per_zone);
    uint64_t iter_chnl_val = spp->nchnls * spp->planes_per_die;
    uint64_t incre = (slpa % spp->chnls_per_zone) * spp->planes_per_die;
    uint64_t increp = slpa_origin % spp->planes_per_die;
    // printf("[DEBUG] zidx:%llu slpa:%llu slpa_origin:%llu start:%llu iter_chnl_way:%llu iter_chnl_way_val:%llu iter_chnl:%llu iter_chnl_val:%llu incre:%llu increp:%llu\n", zidx,slpa,slpa_origin ,start, iter_chnl_way, iter_chnl_way_val,iter_chnl,iter_chnl_val,incre,increp);
    // printf("slba:%llu ppa:%llu\n",slba,(start + (iter_chnl_way * iter_chnl_way_val) + (iter_chnl * iter_chnl_val) + incre + increp));
    return ((start + (iter_chnl_way * iter_chnl_way_val) + (iter_chnl * iter_chnl_val) + incre + increp));
}

static inline uint64_t zns_advanced_plane_idx(NvmeNamespace *ns, uint64_t slba){
    FemuCtrl *n = ns->ctrl;
    struct zns * zns = n->zns;
    struct zns_ssdparams *spp = zns->sp;
    uint64_t ppn = zns_get_multichnlway_ppn_idx(ns, slba);
    return (ppn % (spp->nchnls * spp->ways * spp->dies_per_chip * spp->planes_per_die));
}
static inline uint64_t zns_get_multiway_chip_idx(NvmeNamespace *ns, uint64_t slba){
    FemuCtrl *n = ns->ctrl;
    struct zns * zns = n->zns;
    struct zns_ssdparams *spp = zns->sp;
    uint64_t zidx= zns_zone_idx(ns, slba);

    
    uint64_t ppn = zns_get_multichnlway_ppn_idx(ns,slba);
    // printf("ppn:%llu,f:%llu,b:%lu,an:%lu",ppn,(ppn/spp->planes_per_die),spp->nchnls * spp->ways,(ppn/spp->planes_per_die) % (spp->nchnls * spp->ways));
    return ((ppn/spp->planes_per_die) % (spp->nchnls * spp->ways));
    
}
static inline uint64_t zns_advanced_chnl_idx(NvmeNamespace *ns, uint64_t slba)
{    
    FemuCtrl *n = ns->ctrl;
    struct zns * zns = n->zns;
    struct zns_ssdparams *spp = zns->sp;
    return zns_get_multiway_chip_idx(ns,slba) % spp->nchnls;
}

int main() {
    FemuCtrl ctrl;
    ctrl.num_zones = 1024; // 假设有1024个zones
    struct zns zns;
    struct zns_ssdparams spp;
    zns.sp = &spp;
    ctrl.zns = &zns;
    ctrl.zone_size_log2 = 17; // 假设zone大小为2^17，即128KB

    setup_params(&spp, &ctrl);
    NvmeNamespace ns; // 创建NvmeNamespace类型的实例
    ns.ctrl = &ctrl;  // 将FemuCtrl类型的实例赋值给NvmeNamespace的ctrl字段
    // 计算逻辑地址对应的物理页号
    // uint64_t slba[] = {0, 16, 32, 48, 64, 80, 96};
    // for (int i = 0; i < sizeof(slba) / sizeof(slba[0]); i++) {
    //     uint64_t result_slpa = zns_get_multichnlway_ppn_idx(&ns, slba[i]);
    //     printf("slba:%lu  ppn: %lu\n", slba[i], result_slpa);
    // }
    for (uint64_t i =0; i < 1600; i+=16){
        uint16_t res = zns_get_multichnlway_ppn_idx(&ns,i);
        printf("[TEST] zns.c:1767 slba:%llu  ppa:%lu plane:%llu chipidx:%llu channel:%llu\n\n",i, res, zns_advanced_plane_idx(&ns,i),zns_get_multiway_chip_idx(&ns,i),zns_advanced_chnl_idx(&ns,i));
    }

    return 0;
}
