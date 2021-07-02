/*
 * author : Shuichi TAKANO
 * since  : Sun Jun 20 2021 13:24:36
 */

#include "dvi.h"

namespace dvi
{
    namespace
    {
        // Transition Maximized x2
        constexpr uint32_t __not_in_flash_func(TMDSControlSyms_)[4] = {
            0xd5354, // 1101010100
            0x2acab, // 0010101011
            0x55154, // 0101010100
            0xaaeab, // 1010101011
        };

        const uint32_t *getTMDSControlSymbol(bool vsync, bool hsync)
        {
            return &TMDSControlSyms_[(vsync << 1) | hsync];
        }

        // 赤
        static uint32_t __not_in_flash_func(TMDSRedSym_)[3] = {
            0x7fd00u, // 0x00, 0x00
            0xbfa01u, // 0xfc, 0xfc
            0x7fd00u, // 0x00, 0x00
        };
    }

    DMA::DMA(const Timing &timing, PIO pio)
    {
        for (int i = 0; i < N_TMDS_LANES; ++i)
        {
            auto sm = i;
            auto &cfg = cfgs_[i];
            cfg.chCtrl = dma_claim_unused_channel(true);
            cfg.chData = dma_claim_unused_channel(true);
            cfg.txFIFO = &pio->txf[sm];
            cfg.dreq = pio_get_dreq(pio, sm, true /* tx */);
        }

        listVBlankSync_.setupListForVBlank(timing, cfgs_, true);
        listVBlankNoSync_.setupListForVBlank(timing, cfgs_, false);
        listActive_.setupListForActive(timing, cfgs_, reinterpret_cast<uint32_t *>(SRAM_BASE)); // この時点ではなんでもいい
        listActiveError_.setupListForActive(timing, cfgs_, nullptr);

        // SYNC Lane のデータ転送からしか割り込みは出さない
        uint32_t maskSyncCh = 1u << cfgs_[TMDS_SYNC_LANE].chData;
        uint32_t maskAllCh = 0;
        for (auto &c : cfgs_)
        {
            maskAllCh |= 1u << c.chCtrl;
            maskAllCh |= 1u << c.chData;
        }

        // DMA_IRQ0 限定
        dma_hw->ints0 = maskSyncCh; // clear int req
        hw_write_masked(&dma_hw->inte0, maskSyncCh, maskAllCh);
    }

    void
    DMA::start()
    {
        listVBlankNoSync_.load(cfgs_);
        dma_start_channel_mask((1u << cfgs_[0].chCtrl) |
                               (1u << cfgs_[1].chCtrl) |
                               (1u << cfgs_[2].chCtrl));
    }

    void
    DMA::clearInterruptReq() const
    {
        dma_hw->ints0 = 1u << cfgs_[TMDS_SYNC_LANE].chData;
    }

    void
    DMA::waitForLastBlockTransferToStart(const Timing &timing) const
    {
        for (int i = 0; i < N_TMDS_LANES; ++i)
        {
            //            printf("wait %d, ch %d\n", i, cfgs_[i].chData);
            while (1)
            // while (dma_debug_hw->ch[cfgs_[i].chData].tcr != timing.hActivePixels / N_CHAR_PER_WORD)
            {
                auto chData = cfgs_[i].chData;
                auto p = &dma_debug_hw->ch[chData];
                auto ct = p->tcr;
                if (ct == timing.hActivePixels / N_CHAR_PER_WORD)
                    break;
                tight_loop_contents();
            }
        }
    }

    void
    DMA::update(LineState st, const uint32_t *tmdsBuf, const Timing &timing)
    {
        switch (st)
        {
        case LineState::ACTIVE:
        {
            if (tmdsBuf)
            {
                listActive_.updateScanLineData(timing, tmdsBuf);
                listActive_.load(cfgs_);
            }
            else
            {
                listActiveError_.load(cfgs_);
            }
        }
        break;

        case LineState::SYNC:
            listVBlankSync_.load(cfgs_);
            break;

        default:
            listVBlankNoSync_.load(cfgs_);
            break;
        }
    }

    /////

    void
    DMA::Reg::set(const DMA::Config &cfg, const void *readAddr, int count, int readRingSizeLog2, bool irq)
    {
        read_addr = readAddr;
        write_addr = cfg.txFIFO;
        transfer_count = count;
        ch_cfg = dma_channel_get_default_config(cfg.chData);

        printf("%p: ra:%p wa:%p ct:%d c:%x\n", this, read_addr, write_addr, transfer_count, ch_cfg.ctrl);

        channel_config_set_ring(&ch_cfg, false, readRingSizeLog2);
        channel_config_set_dreq(&ch_cfg, cfg.dreq);
        channel_config_set_chain_to(&ch_cfg, cfg.chCtrl);
        channel_config_set_irq_quiet(&ch_cfg, !irq);
    }

    void
    DMA::List::setupListForVBlank(const Timing &timing, const Configs &cfgs, bool vSyncAsserted)
    {
        bool vsync = timing.vSyncPolarity == vSyncAsserted;

        const auto *symHSyncOff = getTMDSControlSymbol(vsync, !timing.hSyncPolarity);
        const auto *symHSyncOn = getTMDSControlSymbol(vsync, timing.hSyncPolarity);
        const auto *symNoSync = getTMDSControlSymbol(false, false);

        for (int i = 0; i < N_TMDS_LANES; ++i)
        {
            auto *list = get(i);
            const auto &cfg = cfgs[i];
            if (i == TMDS_SYNC_LANE)
            {
                list[0].set(cfg, symHSyncOff, timing.hFrontPorch / N_CHAR_PER_WORD, 2, false);
                list[1].set(cfg, symHSyncOn, timing.hSyncWidth / N_CHAR_PER_WORD, 2, false);
                list[2].set(cfg, symHSyncOff, timing.hBackPorch / N_CHAR_PER_WORD, 2, true);
                list[3].set(cfg, symHSyncOff, timing.hActivePixels / N_CHAR_PER_WORD, 2, false);
            }
            else
            {
                list[0].set(cfg, symNoSync, (timing.hFrontPorch + timing.hSyncWidth + timing.hBackPorch) / N_CHAR_PER_WORD, 2, false);
                list[1].set(cfg, symNoSync, timing.hActivePixels / N_CHAR_PER_WORD, 2, false);
            }

            // 0番のレーンの active region の直前のブロックの終端で割り込みを入れ、3レーン分の次のラインのDMAリストの更新を行う
        }
    }

    void
    DMA::List::setupListForActive(const Timing &timing, const Configs &cfgs, const uint32_t *tmds)
    {
        bool vsync = !timing.vSyncPolarity;

        const auto *symHSyncOff = getTMDSControlSymbol(vsync, !timing.hSyncPolarity);
        const auto *symHSyncOn = getTMDSControlSymbol(vsync, timing.hSyncPolarity);
        const auto *symNoSync = getTMDSControlSymbol(false, false);

        for (int i = 0; i < N_TMDS_LANES; ++i)
        {
            auto *list = get(i);
            const auto &cfg = cfgs[i];

            auto setActiveChunk = [&](auto &r) {
                const void *src = &TMDSRedSym_[i];
                auto lineSize = timing.hActivePixels / N_CHAR_PER_WORD;
                if (tmds)
                {
                    src = tmds + lineSize * i;
                }
                r.set(cfg, src, lineSize, tmds ? 0 : 2, false);
            };

            if (i == TMDS_SYNC_LANE)
            {
                list[0].set(cfg, symHSyncOff, timing.hFrontPorch / N_CHAR_PER_WORD, 2, false);
                list[1].set(cfg, symHSyncOn, timing.hSyncWidth / N_CHAR_PER_WORD, 2, false);
                list[2].set(cfg, symHSyncOff, timing.hBackPorch / N_CHAR_PER_WORD, 2, true);
                setActiveChunk(list[3]);
            }
            else
            {
                list[0].set(cfg, symNoSync, (timing.hFrontPorch + timing.hSyncWidth + timing.hBackPorch) / N_CHAR_PER_WORD, 2, false);
                setActiveChunk(list[1]);
            }
        }
    }

    void
    DMA::List::updateScanLineData(const Timing &timing, const uint32_t *tmds)
    {
        auto lineSize = timing.hActivePixels / N_CHAR_PER_WORD;
        for (int i = 0; i < N_TMDS_LANES; ++i)
        {
            auto *list = get(i);
            auto *src = tmds + lineSize * i;
            if (i == TMDS_SYNC_LANE)
            {
                list[3].read_addr = src;
            }
            else
            {
                list[1].read_addr = src;
            }
        }
    }

    void
    DMA::List::load(const Configs &cfgs) const
    {
        for (int i = 0; i < N_TMDS_LANES; ++i)
        {
            const auto &cfg = cfgs[i];
            auto c = dma_channel_get_default_config(cfg.chCtrl);
            auto *dst = &dma_hw->ch[cfg.chData];
            auto *src = const_cast<List *>(this)->get(i);
            channel_config_set_ring(&c, true /* write */, 4); // 16-byte write wrap
            channel_config_set_read_increment(&c, true);
            channel_config_set_write_increment(&c, true);
            dma_channel_configure(cfg.chCtrl, &c, dst, src, 4, false);
            // 4 word 転送したら停止して、データ転送後の chain_to で再稼働する
        }
    }
}
