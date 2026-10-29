// Copyright (c) 2026 The THA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <pos.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pos_tests, BasicTestingSetup)

// The staker's parallel search uses ComputeStakeKernelHash. It must produce the
// exact hash consensus checks in CheckStakeKernelHash, or the staker would chase
// kernels the network rejects (or miss ones it accepts).
BOOST_AUTO_TEST_CASE(fast_kernel_hash_matches_consensus)
{
    CBlockIndex prev;
    for (int i = 0; i < 1000; ++i) {
        prev.nStakeModifier = InsecureRand256();
        const COutPoint prevout(InsecureRand256(), InsecureRand32());
        const uint32_t blockFromTime = InsecureRand32() >> 1;
        const uint32_t nTimeBlock = blockFromTime + (InsecureRand32() >> 2);

        uint256 hashProofOfStake, targetProofOfStake;
        CheckStakeKernelHash(&prev, 0x207fffff, blockFromTime, 0, prevout, nTimeBlock, 0xD0D0FACE,
                             hashProofOfStake, targetProofOfStake);

        BOOST_CHECK_EQUAL(hashProofOfStake, ComputeStakeKernelHash(prev.nStakeModifier, blockFromTime, prevout, nTimeBlock));
    }
}

BOOST_AUTO_TEST_SUITE_END()
