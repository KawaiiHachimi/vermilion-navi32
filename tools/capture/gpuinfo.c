// SPDX-License-Identifier: MIT
/*
 * gpuinfo: print what Linux amdgpu reports about an AMD GPU (identity, shader layout, memory, firmware versions)
 * through libdrm's read-only AMDGPU_INFO queries. No root needed.
 *
 * Used to compare a card with RADV's canned profiles (patches/mesa/0016) and to document reference machines
 * (docs/REFERENCE-MACHINES.md).
 *
 * Build and run on Linux:
 *   cc -Wall -I/usr/include/libdrm -o gpuinfo tools/capture/gpuinfo.c -ldrm_amdgpu
 *   ./gpuinfo /dev/dri/renderD128
 * Which render node is which GPU: readlink -f /sys/class/drm/renderD128/device (and the other nodes).
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <amdgpu.h>
#include <amdgpu_drm.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s /dev/dri/renderDN\n", argv[0]);
        return 2;
    }
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }
    uint32_t major, minor;
    amdgpu_device_handle dev;
    if (amdgpu_device_initialize(fd, &major, &minor, &dev)) {
        fprintf(stderr, "%s: not an amdgpu device\n", argv[1]);
        return 1;
    }

    struct amdgpu_gpu_info g;
    struct drm_amdgpu_info_device di;
    struct drm_amdgpu_info_vram_gtt vg;
    if (amdgpu_query_gpu_info(dev, &g) || amdgpu_query_info(dev, AMDGPU_INFO_DEV_INFO, sizeof di, &di) ||
        amdgpu_query_info(dev, AMDGPU_INFO_VRAM_GTT, sizeof vg, &vg)) {
        fprintf(stderr, "query failed\n");
        return 1;
    }

    printf("asic_id=%#x chip_rev=%#x ext_rev=%#x pci_rev=%#x family=%u\n", g.asic_id, g.chip_rev,
           g.chip_external_rev, g.pci_rev_id, g.family_id);
    printf("gb_addr_cfg=%#x num_se=%u sa_per_se=%u cu_active=%u rb_pipes=%u rb_mask=%#x num_tcc=%u "
           "tcc_disabled=%#llx vram_bit_width=%u vram_type=%u\n",
           g.gb_addr_cfg, g.num_shader_engines, g.num_shader_arrays_per_engine, di.cu_active_number,
           di.num_rb_pipes, g.enabled_rb_pipes_mask, di.num_tcc_blocks, (unsigned long long)di.tcc_disabled_mask,
           di.vram_bit_width, di.vram_type);
    printf("max_sclk_khz=%llu max_mclk_khz=%llu gl2c_kb=%u mall_bytes=%llu pcie_gen=%u pcie_lanes=%u\n",
           (unsigned long long)di.max_engine_clock, (unsigned long long)di.max_memory_clock, di.gl2c_cache_size,
           (unsigned long long)di.mall_size, di.pcie_gen, di.pcie_num_lanes);
    printf("cu_bitmap:");
    for (int se = 0; se < 4; se++)
        for (int sa = 0; sa < 4; sa++)
            printf(" %#x", di.cu_bitmap[se][sa]);
    printf("\n");
    printf("vram_bytes=%llu vram_cpu_visible=%llu gtt_bytes=%llu\n", (unsigned long long)vg.vram_size,
           (unsigned long long)vg.vram_cpu_accessible_size, (unsigned long long)vg.gtt_size);

    static const struct { const char *name; unsigned type; } fw[] = {
        {"me", AMDGPU_INFO_FW_GFX_ME},   {"pfp", AMDGPU_INFO_FW_GFX_PFP}, {"mec", AMDGPU_INFO_FW_GFX_MEC},
        {"rlc", AMDGPU_INFO_FW_GFX_RLC}, {"mes", AMDGPU_INFO_FW_MES},     {"smc", AMDGPU_INFO_FW_SMC},
        {"sos", AMDGPU_INFO_FW_SOS},     {"dmcub", AMDGPU_INFO_FW_DMCUB}, {"vcn", AMDGPU_INFO_FW_VCN},
        {"sdma", AMDGPU_INFO_FW_SDMA},
    };
    for (unsigned i = 0; i < sizeof fw / sizeof fw[0]; i++) {
        uint32_t ver, feature;
        if (!amdgpu_query_firmware_version(dev, fw[i].type, 0, 0, &ver, &feature))
            printf("fw %-5s ver=%u (%#x) feature=%u\n", fw[i].name, ver, ver, feature);
    }

    amdgpu_device_deinitialize(dev);
    close(fd);
    return 0;
}
