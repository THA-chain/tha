// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/miner.h>

#include <common/args.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <common/args.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <deploymentstatus.h>
#include <common/system.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <util/threadnames.h>
#include <primitives/transaction.h>
#include <logging.h>
#include <net.h>
#include <netbase.h>
#include <pow.h>
#include <key_io.h>
//#include <primitives/transaction.h>
#include <timedata.h>
#include <util/time.h>
#include <util/moneystr.h>
#include <validation.h>

#include <common/system.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif

#include <algorithm>
#include <utility>

using namespace wallet;

namespace node {
int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime{std::max<int64_t>(pindexPrev->GetMedianTimePast() + 1, TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime()))};

    if (nOldTime < nNewTime) {
        pblock->nTime = nNewTime;
    }

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks) {
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);
    }

    return nNewTime - nOldTime;
}

void RegenerateCommitments(CBlock& block, ChainstateManager& chainman)
{
    CMutableTransaction tx{*block.vtx.at(0)};
    tx.vout.erase(tx.vout.begin() + GetWitnessCommitmentIndex(block));
    block.vtx.at(0) = MakeTransactionRef(tx);

    const CBlockIndex* prev_block = WITH_LOCK(::cs_main, return chainman.m_blockman.LookupBlockIndex(block.hashPrevBlock));
    chainman.GenerateCoinbaseCommitment(block, prev_block);

    block.hashMerkleRoot = BlockMerkleRoot(block);
}

static BlockAssembler::Options ClampOptions(BlockAssembler::Options options)
{
    // Limit weight to between 4K and DEFAULT_BLOCK_MAX_WEIGHT for sanity:
    options.nBlockMaxWeight = std::clamp<size_t>(options.nBlockMaxWeight, 4000, DEFAULT_BLOCK_MAX_WEIGHT);
    return options;
}

BlockAssembler::BlockAssembler(Chainstate& chainstate, const CTxMemPool* mempool, const Options& options)
    : chainparams{chainstate.m_chainman.GetParams()},
      m_mempool{mempool},
      m_chainstate{chainstate},
      m_options{ClampOptions(options)}
{
}

void ApplyArgsManOptions(const ArgsManager& args, BlockAssembler::Options& options)
{
    // Block resource limits
    options.nBlockMaxWeight = args.GetIntArg("-blockmaxweight", options.nBlockMaxWeight);
    if (const auto blockmintxfee{args.GetArg("-blockmintxfee")}) {
        if (const auto parsed{ParseMoney(*blockmintxfee)}) options.blockMinFeeRate = CFeeRate{*parsed};
    }
}
static BlockAssembler::Options ConfiguredOptions()
{
    BlockAssembler::Options options;
    ApplyArgsManOptions(gArgs, options);
    return options;
}

BlockAssembler::BlockAssembler(Chainstate& chainstate, const CTxMemPool* mempool)
    : BlockAssembler(chainstate, mempool, ConfiguredOptions()) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn)
{
    const auto time_start{SteadyClock::now()};

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if (!pblocktemplate.get()) {
        return nullptr;
    }
    CBlock* const pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK(::cs_main);
    CBlockIndex* pindexPrev = m_chainstate.m_chain.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = m_chainstate.m_chainman.m_versionbitscache.ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand()) {
        pblock->nVersion = gArgs.GetIntArg("-blockversion", pblock->nVersion);
    }

    pblock->nTime = TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime());
    m_lock_time_cutoff = pindexPrev->GetMedianTimePast();

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    if (m_mempool) {
        LOCK(m_mempool->cs);
        addPackageTxs(*m_mempool, nPackagesSelected, nDescendantsUpdated);
    }

    const auto time_1{SteadyClock::now()};

    m_last_block_num_txs = nBlockTx;
    m_last_block_weight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);

    // force coinbase destination to premine at block heights 1 and 2
    if (nHeight == 1) {
        coinbaseTx.vout[0].scriptPubKey = GetScriptForDestination(DecodeDestination(chainparams.GetConsensus().premine_address_1));
    } else if (nHeight == 2) {
        coinbaseTx.vout[0].scriptPubKey = GetScriptForDestination(DecodeDestination(chainparams.GetConsensus().premine_address_2));
    } else {
        coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
    }

    coinbaseTx.vout[0].nValue = nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus());
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    pblocktemplate->vchCoinbaseCommitment = m_chainstate.m_chainman.GenerateCoinbaseCommitment(*pblock, pindexPrev);
    pblocktemplate->vTxFees[0] = -nFees;

    LogPrintf("CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    BlockValidationState state;
    if (m_options.test_block_validity && !TestBlockValidity(state, chainparams, m_chainstate, *pblock, pindexPrev,
                                                  GetAdjustedTime, /*fCheckPOW=*/false, /*fCheckMerkleRoot=*/false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, state.ToString()));
    }

    const auto time_2{SteadyClock::now()};

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n",
             Ticks<MillisecondsDouble>(time_1 - time_start), nPackagesSelected, nDescendantsUpdated,
             Ticks<MillisecondsDouble>(time_2 - time_1),
             Ticks<MillisecondsDouble>(time_2 - time_start));

    return std::move(pblocktemplate);
}


std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake, int64_t* pTotalFees, int32_t nTime, bool fAddTxs)
{
    const auto time_start{SteadyClock::now()};

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if (!pblocktemplate.get()) {
        return nullptr;
    }
    CBlock* const pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    // Add dummy coinstake tx as second transaction
    if(fProofOfStake)
        pblock->vtx.emplace_back();    
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK(::cs_main);
    CBlockIndex* pindexPrev = m_chainstate.m_chain.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = m_chainstate.m_chainman.m_versionbitscache.ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand()) {
        pblock->nVersion = gArgs.GetIntArg("-blockversion", pblock->nVersion);
    }


    uint32_t txProofTime = nTime == 0 ? GetAdjustedTime64() : nTime;
    if(fProofOfStake)
        txProofTime &= ~STAKE_TIMESTAMP_MASK;
    pblock->nTime = txProofTime;
    m_lock_time_cutoff = pindexPrev->GetMedianTimePast();

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    if (fAddTxs) {
        if (m_mempool) {
            LOCK(m_mempool->cs);
            addPackageTxs(*m_mempool, nPackagesSelected, nDescendantsUpdated);
        }
    }

    const auto time_1{SteadyClock::now()};

    m_last_block_num_txs = nBlockTx;
    m_last_block_weight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    if (fProofOfStake)
    {
        // Make the coinbase tx empty in case of proof of stake
        coinbaseTx.vout[0].SetEmpty();
    }
    else
    {
        coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
        coinbaseTx.vout[0].nValue = nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus());
    }
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));

    // Create coinstake transaction.
    if(fProofOfStake)
    {
        CMutableTransaction coinstakeTx;
        coinstakeTx.vout.resize(2);
        coinstakeTx.vout[0].SetEmpty();
        coinstakeTx.vout[1].scriptPubKey = scriptPubKeyIn;
        pblock->vtx[1] = MakeTransactionRef(std::move(coinstakeTx));

        //this just makes CBlock::IsProofOfStake to return true
        //real prevoutstake info is filled in later in SignBlock
        pblock->prevoutStake.n = 0;

    }

    pblocktemplate->vchCoinbaseCommitment = m_chainstate.m_chainman.GenerateCoinbaseCommitment(*pblock, pindexPrev, fProofOfStake);
    pblocktemplate->vTxFees[0] = -nFees;

    if (pTotalFees)
        *pTotalFees = nFees;
    LogPrintf("CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    if (!fProofOfStake)
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    BlockValidationState state;
    if (!fProofOfStake && m_options.test_block_validity && !TestBlockValidity(state, chainparams, m_chainstate, *pblock, pindexPrev,
                                                  GetAdjustedTime, /*fCheckPOW=*/false, /*fCheckMerkleRoot=*/false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, state.ToString()));
    }
    const auto time_2{SteadyClock::now()};

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n",
             Ticks<MillisecondsDouble>(time_1 - time_start), nPackagesSelected, nDescendantsUpdated,
             Ticks<MillisecondsDouble>(time_2 - time_1),
             Ticks<MillisecondsDouble>(time_2 - time_start));

    return std::move(pblocktemplate);
}


void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        } else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= m_options.nBlockMaxWeight) {
        return false;
    }
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST) {
        return false;
    }
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package) const
{
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, m_lock_time_cutoff)) {
            return false;
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblocktemplate->block.vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee rate %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

/** Add descendants of given transactions to mapModifiedTx with ancestor
 * state updated assuming given transactions are inBlock. Returns number
 * of updated descendants. */
static int UpdatePackagesForAdded(const CTxMemPool& mempool,
                                  const CTxMemPool::setEntries& alreadyAdded,
                                  indexed_modified_transaction_set& mapModifiedTx) EXCLUSIVE_LOCKS_REQUIRED(mempool.cs)
{
    AssertLockHeld(mempool.cs);

    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc)) {
                continue;
            }
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                mit = mapModifiedTx.insert(modEntry).first;
            }
            mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
        }
    }
    return nDescendantsUpdated;
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(const CTxMemPool& mempool, int& nPackagesSelected, int& nDescendantsUpdated)
{
    AssertLockHeld(mempool.cs);

    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        //
        // Skip entries in mapTx that are already in a block or are present
        // in mapModifiedTx (which implies that the mapTx ancestor state is
        // stale due to ancestor inclusion in the block)
        // Also skip transactions that we've already failed to add. This can happen if
        // we consider a transaction in mapModifiedTx and it fails: we can then
        // potentially consider it again while walking mapTx.  It's currently
        // guaranteed to fail again, but as a belt-and-suspenders check we put it in
        // failedTx and avoid re-evaluation, since the re-evaluation would be using
        // cached size/sigops/fee values that are not actually correct.
        /** Return true if given transaction from mapTx has already been evaluated,
         * or if the transaction's cached data in mapTx is incorrect. */
        if (mi != mempool.mapTx.get<ancestor_score>().end()) {
            auto it = mempool.mapTx.project<0>(mi);
            assert(it != mempool.mapTx.end());
            if (mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it)) {
                ++mi;
                continue;
            }
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (packageFees < m_options.blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                    m_options.nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        auto ancestors{mempool.AssumeCalculateMemPoolAncestors(__func__, *iter, CTxMemPool::Limits::NoLimits(), /*fSearchForParents=*/false)};

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        for (size_t i = 0; i < sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(mempool, ancestors, mapModifiedTx);
    }
}



#ifdef ENABLE_WALLET
//////////////////////////////////////////////////////////////////////////////
//
// Proof of Work/Transaction miner
//

//
// Looking for suitable coins for creating new block.
//

bool CheckStake(ChainstateManager& chainman, const std::shared_ptr<const CBlock> pblock, wallet::CWallet& wallet)
{
    uint256 proofHash, hashTarget;
    uint256 hashBlock = pblock->GetHash();

    if(!pblock->IsProofOfStake())
        return error("CheckStake() : %s is not a proof-of-stake block", hashBlock.GetHex());

    // verify hash target and signature of coinstake tx
    BlockValidationState state;
    if (!CheckProofOfStake( &(chainman.BlockIndex()[pblock->hashPrevBlock]), state, *pblock->vtx[1], pblock->nBits, pblock->nTime, pblock->nNonce, proofHash, hashTarget, chainman.ActiveChainstate().CoinsTip()))
        return error("CheckStake() : proof-of-stake checking failed");

    //// debug print
    LogPrint(BCLog::COINSTAKE, "CheckStake() : new proof-of-stake block found  \n  hash: %s \nproofhash: %s  \ntarget: %s\n", hashBlock.GetHex(), proofHash.GetHex(), hashTarget.GetHex());
    LogPrint(BCLog::COINSTAKE, "%s\n", pblock->ToString());
    LogPrint(BCLog::COINSTAKE, "out %s\n", FormatMoney(pblock->vtx[1]->GetValueOut()));

    // Found a solution
    {
        if (pblock->hashPrevBlock != chainman.ActiveChain().Tip()->GetBlockHash())
            return error("CheckStake() : generated block is stale");

        for(const CTxIn& vin : pblock->vtx[1]->vin) {
            if (wallet.IsSpent(COutPoint{vin.prevout.hash, vin.prevout.n})) {
                return error("CheckStake() : generated block became invalid due to stake UTXO being spent");
            }
        }
    }

    // Process this block the same as if we had received it from another node
    bool fNewBlock = false;
    if (!chainman.ProcessNewBlock(pblock, /*force_processing=*/true, /*min_pow_checked=*/false, /*new_block=*/&fNewBlock))
        return error("CheckStake() : ProcessBlock, block not accepted");

    return true;
}

void ThreadStakeMiner(wallet::CWallet& wallet, CConnman& connman, ChainstateManager& chainman, const CTxMemPool& mempool)
{
    s_mining_thread_exiting.store(false, std::memory_order_relaxed);
    s_mining_allowed.store(true, std::memory_order_relaxed);

    bool fTryToSync = true;
    
    std::set<std::pair<const wallet::CWalletTx*,unsigned int> > setCoins;
    uint256 chainTipForCoins;

    uint32_t beginningTime=0;
    int profiler = 0;
    int64_t stop_time, start_time = 0;

    while (true)
    {     
        if ( s_mining_thread_exiting.load(std::memory_order_relaxed) )
        {
            break;
        }

        if (!wallet::GetMiningAllowedStatus())
        {
            break;
        }        

        while (wallet.IsLocked())
        {
            wallet.m_last_coin_stake_search_interval = 0;
            s_hashes_per_second = 0;
            s_cpu_loading = 0;
            UninterruptibleSleep(std::chrono::milliseconds{1000});      
            continue;    
        }
        // Check if the last PoW block has been mined yet
        if (chainman.ActiveChain().Tip()->nHeight < Params().GetConsensus().nLastPOWBlock) {
            UninterruptibleSleep(std::chrono::milliseconds{Params().GetConsensus().nPowTargetSpacing * 1000});
            wallet.m_last_coin_stake_search_interval = 0;
            s_hashes_per_second = 0;
            s_cpu_loading = 0;
            continue;
        }
        // Don't disable PoS mining for no connections if in regtest mode
        if (!gArgs.GetBoolArg("-emergencymining", false)) {
            while (chainman.IsInitialBlockDownload()) {
                wallet.m_last_coin_stake_search_interval = 0;
                fTryToSync = true;
                UninterruptibleSleep(std::chrono::milliseconds{1000});
                if ( s_mining_thread_exiting.load(std::memory_order_relaxed) || (!wallet::GetMiningAllowedStatus()) )
                {
                    goto DONE_MINING;
                }
            }
            if (fTryToSync) {
                fTryToSync = false;
                if (connman.GetNodeCount(ConnectionDirection::Both) < 3 ||
                    chainman.ActiveChain().Tip()->GetBlockTime() < GetTime() - Params().GetConsensus().nPowTargetSpacing ||
                    !chainman.ActiveChain().Tip()->HaveTxsDownloaded() ||
                    !chainman.ActiveChain().Tip()->IsValid(BLOCK_VALID_TRANSACTIONS)) {
                    UninterruptibleSleep(std::chrono::milliseconds{1000});
                    wallet.m_last_coin_stake_search_interval = 0;
                    s_hashes_per_second = 0;
                    s_cpu_loading = 0;
                    continue;
                }
            }
        }

        // Cannot mine with 0 connections, except in regtest mode.
        if (chainman.GetParams().GetChainTypeString() != "regtest" && connman.GetNodeCount(ConnectionDirection::Both) == 0 ) {
            UninterruptibleSleep(std::chrono::milliseconds{1000});
            wallet.m_last_coin_stake_search_interval = 0;
            s_hashes_per_second = 0;
            s_cpu_loading = 0;
            continue;
        }

        //
        // Select the suitable coins
        //
        if (chainTipForCoins != chainman.ActiveChain().Tip()->GetBlockHash()) {
            const auto start_time{SteadyClock::now()};
            LogPrint(BCLog::COINSTAKE, "Chain tip changed since previous coin selection, selecting new coins for staking...\n");
            LOCK(wallet.cs_wallet);
            setCoins.clear();
            chainTipForCoins = chainman.ActiveChain().Tip()->GetBlockHash();
            wallet.SelectCoinsForStaking(setCoins);
            LogPrint(BCLog::COINSTAKE, "Selecting coins for staking completed in %15dms\n", Ticks<std::chrono::milliseconds>(SteadyClock::now() - start_time));
        } else {
            LogPrint(BCLog::COINSTAKE, "Chain tip unchanged since previous coin selection, using previously selected coins...\n");
        }

        //
        // Create new block
        //
        if (setCoins.size() > 0)
        {
            int64_t nTotalFees = 0;  
            // First just create an empty block. No need to process transactions until we know we can create a block
            std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler{chainman.ActiveChainstate(), &mempool}.CreateNewBlock(CScript(), true, &nTotalFees, 0, false));

            if (!pblocktemplate.get()) {
                LogPrintf("ThreadStakeMiner(): Failed to create block template; thread exiting...\n");
                goto DONE_MINING;
            }

            CBlockIndex* pindexPrev = chainman.ActiveChain().Tip();
            stop_time = GetTime<std::chrono::milliseconds>().count();            
            while (true)
            {
                UninterruptibleSleep(std::chrono::milliseconds{10});
                // Sliding window results in solving the same soluions many times for the sake of sending
                // a mined block at a different time, this could result in a higher chance of the network
                // accepting a mined block, however the time aligment in BTC is very good and this may
                // just be overkill. Simplifying the mining by removing the sliding window and mining
                // as many hashes as possible in each second internal.
                uint32_t newTime=GetAdjustedTime64();

                // Hashes/sec is equal to the number of coins if the delta was less than 1 second..
                // If more than 1 second, then the hashrate is still the the number of coins in the set,
                // but the loading is over 
                int64_t delta = stop_time - start_time;
                if ( delta <= 1000 )
                {
                    s_hashes_per_second = setCoins.size() / 1.0;
                    s_cpu_loading = delta/10.0;
                }
                else
                {
                    s_hashes_per_second = 100 * setCoins.size() / delta;
                    s_cpu_loading = 100.0;
                }

                if ( newTime > beginningTime )
                {
                    beginningTime = newTime;
                    start_time = GetTime<std::chrono::milliseconds>().count();
                    break;
                }
            }

            uint32_t i=beginningTime;

            if ( profiler < 200 )
            {
                LogPrintf("ThreadStakeMiner(): BEGIN===================\n");
                LogPrintf("ThreadStakeMiner(): nTime: %d  SIZE: %d\n", i, setCoins.size());                 
                profiler++;
            }

            // The information is needed for status bar to determine if the staker is trying to create block and when it will be created approximately,
            if (wallet.m_last_coin_stake_search_time == 0) wallet.m_last_coin_stake_search_time = GetAdjustedTime64(); // startup timestamp
            // nLastCoinStakeSearchInterval > 0 mean that the staker is running
            wallet.m_last_coin_stake_search_interval = i - wallet.m_last_coin_stake_search_time;

            // Try to sign a block (this also checks for a PoS stake)
            pblocktemplate->block.nTime = i;
            pblocktemplate->block.nNonce = 0xD0D0FACE; // Proof of Transaction Work
            std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>(pblocktemplate->block);

            if (SignBlock(chainman, pblock, wallet, nTotalFees, i, 0xD0D0FACE, setCoins)) {

                if (chainman.ActiveChain().Tip()->GetBlockHash() != pblock->hashPrevBlock) {
                    //another block was received while building ours, scrap progress
                    LogPrintf("ThreadStakeMiner(): Valid future PoS block was orphaned before becoming valid\n");
                    break;
                }
                // Create a block that's properly populated with transactions
                std::unique_ptr<CBlockTemplate> pblocktemplatefilled(
                        BlockAssembler{chainman.ActiveChainstate(), &mempool}.CreateNewBlock(pblock->vtx[1]->vout[1].scriptPubKey, true, &nTotalFees, i));
                pblocktemplatefilled->block.nNonce = 0xD0D0FACE; // Proof of Transaction Work
                if (!pblocktemplatefilled.get()) {
                    LogPrintf("ThreadStakeMiner(): Failed to create block template; thread exiting...\n");
                    goto DONE_MINING;
                }
                if (chainman.ActiveChain().Tip()->GetBlockHash() != pblock->hashPrevBlock) {
                    //another block was received while building ours, scrap progress
                    LogPrintf("ThreadStakeMiner(): Valid future PoS block was orphaned before becoming valid\n");
                    break;
                }
                // Sign the full block and use the timestamp from earlier for a valid stake
                std::shared_ptr<CBlock> pblockfilled = std::make_shared<CBlock>(pblocktemplatefilled->block);
                if (SignBlock(chainman, pblockfilled, wallet, nTotalFees, i, 0xD0D0FACE, setCoins)) {
                    // Should always reach here unless we spent too much time processing transactions and the timestamp is now invalid
                    // CheckStake also does CheckBlock and AcceptBlock to propogate it to the network
                    bool validBlock = false;
                    while (!validBlock) {
                        if (chainman.ActiveChain().Tip()->GetBlockHash() != pblockfilled->hashPrevBlock) {
                            //another block was received while building ours, scrap progress
                            LogPrintf("ThreadStakeMiner(): Valid future PoS block was orphaned before becoming valid\n");
                            break;
                        }
                        //check timestamps
                        if (pblockfilled->GetBlockTime() <= pindexPrev->GetBlockTime() ||
                            FutureDrift(pblockfilled->GetBlockTime()) < pindexPrev->GetBlockTime()) {
                            LogPrintf("ThreadStakeMiner(): Valid PoS block took too long to create and has expired\n");
                            break; //timestamp too late, so ignore
                        }
                        if (pblockfilled->GetBlockTime() > FutureDrift(GetAdjustedTime64())) {
                                //too early, so wait and try again
                                UninterruptibleSleep(std::chrono::milliseconds{200});
                            continue;
                        }
                        validBlock=true;
                    }
                    if (validBlock) {
                        CheckStake(chainman, pblockfilled, wallet);
                        // Update the search time when new valid block is created, needed for status bar icon
                        wallet.m_last_coin_stake_search_time = pblockfilled->GetBlockTime();
                    }
                }
            }
        
        }
        else
        {
            wallet.m_last_coin_stake_search_interval = 0;
            UninterruptibleSleep(std::chrono::milliseconds{5000});
        }
    }

DONE_MINING:

    // Not mining anymore, show 0 hps.
    wallet.m_last_coin_stake_search_interval = 0;
    s_hashes_per_second = 0;
    s_cpu_loading = 0;
}

#endif




} // namespace node
