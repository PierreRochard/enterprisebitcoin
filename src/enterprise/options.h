#ifndef BITCOIN_ENTERPRISE_OPTIONS_H
#define BITCOIN_ENTERPRISE_OPTIONS_H

#pragma once

#include <cstdint>

static constexpr bool DEFAULT_ENTERPRISEINDEX{false};
static constexpr bool DEFAULT_ENTERPRISE_BOOTSTRAP_FROM_POSTGRES{false};
static constexpr bool DEFAULT_ENTERPRISE_MEMPOOL_EXPORT{false};
static constexpr int64_t DEFAULT_ENTERPRISE_PRUNE_TARGET_MIB{20000};
static constexpr int64_t DEFAULT_ENTERPRISE_SPOOL_MAX_MIB{1024};
static constexpr int64_t DEFAULT_ENTERPRISE_BACKFILL_HEIGHT{-1};
static constexpr int DEFAULT_ENTERPRISE_GAP_BATCH_SIZE{256};
static constexpr int DEFAULT_ENTERPRISE_PRICE_FINALIZATION_BATCH_SIZE{256};
// Keep the scan bounded so the daily finalizer never performs an unbounded
// pass over the wide blocks table. 2,016 blocks is roughly two weeks at the
// target ten-minute interval and remains comfortably inside the configured
// 20 GiB pruned block store for the expected one-day finalization delay.
static constexpr int DEFAULT_ENTERPRISE_PRICE_FINALIZATION_LOOKBACK{2016};
//! Snapshot the live UTXO set into Postgres this often. 4032 blocks is four
//! weeks at 1008 blocks/week and matches the historical utxo_age series.
static constexpr int64_t UTXO_EXPORT_INTERVAL{4032};

#endif // BITCOIN_ENTERPRISE_OPTIONS_H
