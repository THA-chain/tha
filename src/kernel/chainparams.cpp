// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/consensus.h>
#include <hash.h>
#include <kernel/messagestartchars.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <arith_uint256.h>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */

static bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    //std::cout << " bnTarget = " << bnTarget.ToString() << std::endl;

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    //const char* pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";
    const char* pszTimestamp = "Decentralized and fair";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        m_chain_type = ChainType::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0; // segwit activation height + miner confirmation window
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.posLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing

        consensus.nLastPOWBlock = 10;
        consensus.nEnableHeaderSignatureHeight = 0;
        consensus.nCheckpointSpan = COINBASE_MATURITY;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = 1619222400; // April 24th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = 1776261600; // April 15th, 2026
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // Approximately May 2024

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0x54;
        pchMessageStart[2] = 0x48;
        pchMessageStart[3] = 0xd9;
        nDefaultPort = 7222;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;


/*

// MINING GENESIS - MAINNET
        uint32_t nTime=1713823241;
        uint32_t nNonce=177259754;
       
        // @"consensus.hashGenesisBlock: 000000004c2738ff52ee6dc039d4fde2f3292fed9afa9d712f895d7094f8d350\r\n"
        // @"genesis.hashMerkleRoot: c36c4216baf256beb34d939e7aa158a54b7488be996e8bdab8d83ff9c73f1f4d\r\n"
        // @"genesis.nNonce: 771851678\r\n"
        // Difficulty bits:
        // Using following formula target can be obtained from any block. For example if a target packed in a block appears as 0x1b0404cb its hexadecimal version will look as following:
        // 0x0404cb * 2**(8*(0x1b - 3)) = 0x00000000000404CB000000000000000000000000000000000000000000000000
        bool proof_of_work_valid = false;
        uint256 lowest_hash = uint256S("ffffffffff19d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
        for (nTime=1713823241; ;nTime++)
        {
            genesis = CreateGenesisBlock(nTime, nNonce, 0x1d00ffff, 1, 50 * COIN);
            //genesis = CreateGenesisBlock(nTime, nNonce, 0x1e00ffff, 1, 50 * COIN);

            for (genesis.nNonce = 177259754; genesis.nNonce <= 0xfffffff0;genesis.nNonce++)
            {
                uint256 genhash = genesis.GetHash();
                proof_of_work_valid = CheckProofOfWork(genhash, genesis.nBits, consensus);
                if (proof_of_work_valid) {
                    break;
                }

                if (UintToArith256(genhash) < UintToArith256(lowest_hash))
                    lowest_hash = genhash;

                if (genesis.nNonce % 2000000 == 0)
                    std::cout << "STILL NOTHING time: " << nTime << " nonce: " << genesis.nNonce << " lowest hash: " << lowest_hash.ToString() << " pow limit: " << consensus.powLimit.ToString() << std::endl;

            }
            if (proof_of_work_valid) {
                break;
            }

            std::cout << "time: " << nTime << std::endl;
        }

        
        consensus.hashGenesisBlock = genesis.GetHash();
        // std::cout << "consensus.hashGenesisBlock: " << consensus.hashGenesisBlock.ToString() << std::endl;
        // std::cout << "genesis.hashMerkleRoot: " << genesis.hashMerkleRoot.ToString() << std::endl;   
        // std::cout << "genesis.nNonce: " << genesis.nNonce << std::endl;
        // std::cout << "genesis.nTime: " << nTime << std::endl;


        std::cout << genesis.ToString() << std::endl;

        exit(1);

*/

        genesis = CreateGenesisBlock(1713823241, 177259754, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000000c80a80efa43810b7e7c77571071a5ff05626e9e6d7d26f3b6c9fac69"));
        assert(genesis.hashMerkleRoot == uint256S("0xe8c8cc27e8be80d8d5d0b681881edfce4736a9c036d0f939d6802af3ae8102ac"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "bc";

        consensus.premine_address_1 = "13FoL25J8fvTzYRa3x2v3sk8SV4W1o21ty";
        consensus.premine_amount_1 = 84000 * COIN;
        consensus.premine_address_2 = "1MkwuMMbEqVxdPf4aq4qLxcsasAZiLKMVT";
        consensus.premine_amount_2 = 126000 * COIN;

        // fixed seeds disabled; to re-enable, populate chainparams_seed_main array in chainparamsseeds.h, uncomment line bellow and rebuild
        // vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                { 0, uint256S("0x00000000c80a80efa43810b7e7c77571071a5ff05626e9e6d7d26f3b6c9fac69")},
            }
        };

        m_assumeutxo_data = {
            // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            .nTime    = 0,
            .nTxCount = 0,
            .dTxRate  = 0,
        };
    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        m_chain_type = ChainType::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1; 
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = 1619222400; // April 24th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = 1628640000; // August 11th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0x0b;
        pchMessageStart[1] = 0x74;
        pchMessageStart[2] = 0x68;
        pchMessageStart[3] = 0x07;
        nDefaultPort = 17222;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

/*

// MINING GENESIS - TESTNET
        uint32_t nTime=1713817130;
        uint32_t nNonce=575844624;
       
        // @"consensus.hashGenesisBlock: 000000004c2738ff52ee6dc039d4fde2f3292fed9afa9d712f895d7094f8d350\r\n"
        // @"genesis.hashMerkleRoot: c36c4216baf256beb34d939e7aa158a54b7488be996e8bdab8d83ff9c73f1f4d\r\n"
        // @"genesis.nNonce: 771851678\r\n"
        // Difficulty bits:
        // Using following formula target can be obtained from any block. For example if a target packed in a block appears as 0x1b0404cb its hexadecimal version will look as following:
        // 0x0404cb * 2**(8*(0x1b - 3)) = 0x00000000000404CB000000000000000000000000000000000000000000000000
        bool proof_of_work_valid = false;
        uint256 lowest_hash = uint256S("ffffffffff19d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
        for (nTime=1713817130; ;nTime++)
        {
            genesis = CreateGenesisBlock(nTime, nNonce, 0x1d00ffff, 1, 50 * COIN);
            //genesis = CreateGenesisBlock(nTime, nNonce, 0x1e00ffff, 1, 50 * COIN);

            for (genesis.nNonce = 575844624; genesis.nNonce <= 0xfffffff0;genesis.nNonce++)
            {
                uint256 genhash = genesis.GetHash();
                proof_of_work_valid = CheckProofOfWork(genhash, genesis.nBits, consensus);
                if (proof_of_work_valid) {
                    break;
                }

                if (UintToArith256(genhash) < UintToArith256(lowest_hash))
                    lowest_hash = genhash;

                if (genesis.nNonce % 2000000 == 0)
                    std::cout << "STILL NOTHING time: " << nTime << " nonce: " << genesis.nNonce << " lowest hash: " << lowest_hash.ToString() << " pow limit: " << consensus.powLimit.ToString() << std::endl;

            }
            if (proof_of_work_valid) {
                break;
            }

            std::cout << "time: " << nTime << std::endl;
        }

        
        consensus.hashGenesisBlock = genesis.GetHash();
        // std::cout << "consensus.hashGenesisBlock: " << consensus.hashGenesisBlock.ToString() << std::endl;
        // std::cout << "genesis.hashMerkleRoot: " << genesis.hashMerkleRoot.ToString() << std::endl;   
        // std::cout << "genesis.nNonce: " << genesis.nNonce << std::endl;
        // std::cout << "genesis.nTime: " << nTime << std::endl;


        std::cout << std::endl << "TESTNET " << genesis.ToString() << std::endl;

        exit(1);



*/


        genesis = CreateGenesisBlock(1713817130, 575844624, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000000efc68a6dd7a0f1786c829d79dce44f3cd0385787121a4d60b255b0fc"));
        assert(genesis.hashMerkleRoot == uint256S("0xe8c8cc27e8be80d8d5d0b681881edfce4736a9c036d0f939d6802af3ae8102ac"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        consensus.premine_address_1 = "mzyewKRoWUwNCEo1QktB8MoKAzY2LUQH1w";
        consensus.premine_amount_1 = 84000 * COIN;
        consensus.premine_address_2 = "n3VHMJ6zHFGRduFToRE9FTa5t5xB1RB6Gv";
        consensus.premine_amount_2 = 126000 * COIN;

        // fixed seeds disabled; to re-enable, populate chainparams_seed_test array in chainparamsseeds.h, uncomment line bellow and rebuild
        // vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_test), std::end(chainparams_seed_test));

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                { 0, uint256S("0x000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943")},
            }
        };

        m_assumeutxo_data = {};

        chainTxData = ChainTxData{
            .nTime    = 0,
            .nTxCount = 0,
            .dTxRate  = 0
        };
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vSeeds.clear();

        if (!options.challenge) {
            bin = ParseHex("512103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae");
            vSeeds.emplace_back("seed.signet.bitcoin.sprovoost.nl.");

            // Hardcoded nodes can be removed once there are more DNS seeds
            vSeeds.emplace_back("178.128.221.177");
            vSeeds.emplace_back("v7ajjeirttkbnt32wpy3c6w3emwnfr3fkla7hpxcfokr3ysd3kqtzmqd.onion:38333");

            consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000001ad46be4862");
            consensus.defaultAssumeValid = uint256S("0x0000013d778ba3f914530f11f6b69869c9fab54acff85acd7b8201d111f19b7f"); // 150000
            m_assumed_blockchain_size = 1;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                // Data from RPC: getchaintxstats 4096 0000013d778ba3f914530f11f6b69869c9fab54acff85acd7b8201d111f19b7f
                .nTime    = 1688366339,
                .nTxCount = 2262750,
                .dTxRate  = 0.003414084572046456,
            };
        } else {
            bin = *options.challenge;
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogPrintf("Signet with challenge %s\n", HexStr(bin));
        }

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        m_chain_type = ChainType::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("00000377ae000000000000000000000000000000000000000000000000000000");
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Activation of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        std::copy_n(hash.begin(), 4, pchMessageStart.begin());

        nDefaultPort = 37222;
        nPruneAfterHeight = 1000;



/*

// MINING GENESIS - SIGNET
        uint32_t nTime=1713875204;
        uint32_t nNonce=185020;
       
        // @"consensus.hashGenesisBlock: 000000004c2738ff52ee6dc039d4fde2f3292fed9afa9d712f895d7094f8d350\r\n"
        // @"genesis.hashMerkleRoot: c36c4216baf256beb34d939e7aa158a54b7488be996e8bdab8d83ff9c73f1f4d\r\n"
        // @"genesis.nNonce: 771851678\r\n"
        // Difficulty bits:
        // Using following formula target can be obtained from any block. For example if a target packed in a block appears as 0x1b0404cb its hexadecimal version will look as following:
        // 0x0404cb * 2**(8*(0x1b - 3)) = 0x00000000000404CB000000000000000000000000000000000000000000000000
        bool proof_of_work_valid = false;
        uint256 lowest_hash = uint256S("ffffffffff19d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
        for (nTime=1713875204; ;nTime++)
        {
            genesis = CreateGenesisBlock(nTime, nNonce, 0x1e0377ae, 1, 50 * COIN);
            //genesis = CreateGenesisBlock(nTime, nNonce, 0x1e00ffff, 1, 50 * COIN);

            for (genesis.nNonce = 185020; genesis.nNonce <= 0xfffffff0;genesis.nNonce++)
            {
                uint256 genhash = genesis.GetHash();
                proof_of_work_valid = CheckProofOfWork(genhash, genesis.nBits, consensus);
                if (proof_of_work_valid) {
                    break;
                }

                if (UintToArith256(genhash) < UintToArith256(lowest_hash))
                    lowest_hash = genhash;

                if (genesis.nNonce % 2000000 == 0)
                    std::cout << "STILL NOTHING time: " << nTime << " nonce: " << genesis.nNonce << " lowest hash: " << lowest_hash.ToString() << " pow limit: " << consensus.powLimit.ToString() << std::endl;

            }
            if (proof_of_work_valid) {
                break;
            }

            std::cout << "time: " << nTime << std::endl;
        }

        
        consensus.hashGenesisBlock = genesis.GetHash();
        // std::cout << "consensus.hashGenesisBlock: " << consensus.hashGenesisBlock.ToString() << std::endl;
        // std::cout << "genesis.hashMerkleRoot: " << genesis.hashMerkleRoot.ToString() << std::endl;   
        // std::cout << "genesis.nNonce: " << genesis.nNonce << std::endl;
        // std::cout << "genesis.nTime: " << nTime << std::endl;


        std::cout << std::endl << "SIGNET " << genesis.ToString() << std::endl;

        exit(1);

*/


        genesis = CreateGenesisBlock(1713875204, 185020, 0x1e0377ae, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000083975bda6ff7ee5def1fb5d904526bae8aea1e848fae4e32986f17e3ea"));
        assert(genesis.hashMerkleRoot == uint256S("0xe8c8cc27e8be80d8d5d0b681881edfce4736a9c036d0f939d6802af3ae8102ac"));

        vFixedSeeds.clear();

        m_assumeutxo_data = {};

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        consensus.premine_address_1 = "mzyewKRoWUwNCEo1QktB8MoKAzY2LUQH1w";
        consensus.premine_amount_1 = 84000 * COIN;
        consensus.premine_address_2 = "n3VHMJ6zHFGRduFToRE9FTa5t5xB1RB6Gv";
        consensus.premine_amount_2 = 126000 * COIN;

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        m_chain_type = ChainType::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        consensus.SegwitHeight = 0; // Always active unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0x54;
        pchMessageStart[2] = 0x68;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 17333;
        nPruneAfterHeight = opts.fastprune ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        for (const auto& [dep, height] : opts.activation_heights) {
            switch (dep) {
            case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
                consensus.SegwitHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
                consensus.BIP34Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
                consensus.BIP66Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
                consensus.BIP65Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
                consensus.CSVHeight = int{height};
                break;
            }
        }

        for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
            consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
            consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
            consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
        }


/*
        // MINING GENESIS - REGTEST
        uint32_t nTime = 1713875204;
        uint32_t nNonce = 1;

        genesis = CreateGenesisBlock(nTime, nNonce, 0x207fffff, 1, 50 * COIN);

        for (genesis.nNonce = 1; genesis.nNonce <= 0xfffffff0;genesis.nNonce++)
        {
            if (CheckProofOfWork(genesis.GetHash(), genesis.nBits, consensus)) {
                break;
            }
        }

        consensus.hashGenesisBlock = genesis.GetHash();
        std::cout << std::endl << "REGTEST GENESIS" << genesis.ToString() << std::endl;

        exit(1);
*/



        genesis = CreateGenesisBlock(1713875204, 1, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x11b0cbc77cbd4c6afd8d8006a2981d524024f7aec6c2538b5e35cc5f2df30562"));
        assert(genesis.hashMerkleRoot == uint256S("0xe8c8cc27e8be80d8d5d0b681881edfce4736a9c036d0f939d6802af3ae8102ac"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        m_is_mockable_chain = true;

        checkpointData = {
            {
                {0, uint256S("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")},
            }
        };

        m_assumeutxo_data = {};

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "bcrt";

        consensus.premine_address_1 = "mzyewKRoWUwNCEo1QktB8MoKAzY2LUQH1w";
        consensus.premine_amount_1 = 84000 * COIN;
        consensus.premine_address_2 = "n3VHMJ6zHFGRduFToRE9FTa5t5xB1RB6Gv";
        consensus.premine_amount_2 = 126000 * COIN;

    }
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main()
{
    return std::make_unique<const CMainParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet()
{
    return std::make_unique<const CTestNetParams>();
}
