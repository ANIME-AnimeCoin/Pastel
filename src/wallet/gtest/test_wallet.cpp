#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sodium.h>

#include "base58.h"
#include "chainparams.h"
#include "main.h"
#include "random.h"
#include "wallet/wallet.h"
#include "zcash/JoinSplit.hpp"
#include "zcash/Note.hpp"
#include "zcash/NoteEncryption.hpp"

using ::testing::_;
using ::testing::Return;

ZCJoinSplit* params = ZCJoinSplit::Unopened();

class TestWallet : public CWallet {
public:
    TestWallet() : CWallet() { }

    void IncrementNoteWitnesses(const CBlockIndex* pindex,
                                const CBlock* pblock,
                                ZCIncrementalMerkleTree tree) {
        CWallet::IncrementNoteWitnesses(pindex, pblock, tree);
    }
    void DecrementNoteWitnesses() {
        CWallet::DecrementNoteWitnesses();
    }
};

CWalletTx GetValidReceive(const libzcash::SpendingKey& sk, CAmount value, bool randomInputs) {
    CMutableTransaction mtx;
    mtx.nVersion = 2; // Enable JoinSplits
    mtx.vin.resize(2);
    if (randomInputs) {
        mtx.vin[0].prevout.hash = GetRandHash();
        mtx.vin[1].prevout.hash = GetRandHash();
    } else {
        mtx.vin[0].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000001");
        mtx.vin[1].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000002");
    }
    mtx.vin[0].prevout.n = 0;
    mtx.vin[1].prevout.n = 0;

    // Generate an ephemeral keypair.
    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);
    mtx.joinSplitPubKey = joinSplitPubKey;

    boost::array<libzcash::JSInput, 2> inputs = {
        libzcash::JSInput(), // dummy input
        libzcash::JSInput() // dummy input
    };

    boost::array<libzcash::JSOutput, 2> outputs = {
        libzcash::JSOutput(), // dummy output
        libzcash::JSOutput(sk.address(), value)
    };

    boost::array<libzcash::Note, 2> output_notes;

    // Prepare JoinSplits
    uint256 rt;
    JSDescription jsdesc {*params, mtx.joinSplitPubKey, rt,
                          inputs, outputs, value, 0, false};
    mtx.vjoinsplit.push_back(jsdesc);

    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);

    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL,
                         dataToBeSigned.begin(), 32,
                         joinSplitPrivKey
                        ) == 0);

    CTransaction tx {mtx};
    CWalletTx wtx {NULL, tx};
    return wtx;
}

libzcash::Note GetNote(const libzcash::SpendingKey& sk,
                       const CTransaction& tx, size_t js, size_t n) {
    ZCNoteDecryption decryptor {sk.viewing_key()};
    auto hSig = tx.vjoinsplit[js].h_sig(*params, tx.joinSplitPubKey);
    auto note_pt = libzcash::NotePlaintext::decrypt(
        decryptor,
        tx.vjoinsplit[js].ciphertexts[n],
        tx.vjoinsplit[js].ephemeralKey,
        hSig,
        (unsigned char) n);
    return note_pt.note(sk.address());
}

CWalletTx GetValidSpend(const libzcash::SpendingKey& sk,
                        const libzcash::Note& note, CAmount value) {
    CMutableTransaction mtx;
    mtx.vout.resize(2);
    mtx.vout[0].nValue = value;
    mtx.vout[1].nValue = 0;

    // Generate an ephemeral keypair.
    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);
    mtx.joinSplitPubKey = joinSplitPubKey;

    // Fake tree for the unused witness
    ZCIncrementalMerkleTree tree;

    boost::array<libzcash::JSInput, 2> inputs = {
        libzcash::JSInput(tree.witness(), note, sk),
        libzcash::JSInput() // dummy input
    };

    boost::array<libzcash::JSOutput, 2> outputs = {
        libzcash::JSOutput(), // dummy output
        libzcash::JSOutput() // dummy output
    };

    boost::array<libzcash::Note, 2> output_notes;

    // Prepare JoinSplits
    uint256 rt;
    JSDescription jsdesc {*params, mtx.joinSplitPubKey, rt,
                          inputs, outputs, 0, value, false};
    mtx.vjoinsplit.push_back(jsdesc);

    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);

    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL,
                         dataToBeSigned.begin(), 32,
                         joinSplitPrivKey
                        ) == 0);
    CTransaction tx {mtx};
    CWalletTx wtx {NULL, tx};
    return wtx;
}

TEST(wallet_tests, set_note_addrs_in_cwallettx) {
    auto sk = libzcash::SpendingKey::random();
    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx, 0, 1);
    auto nullifier = note.nullifier(sk);
    EXPECT_EQ(0, wtx.mapNoteData.size());

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.GetTxid(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;

    wtx.SetNoteData(noteData);
    EXPECT_EQ(noteData, wtx.mapNoteData);
}

TEST(wallet_tests, set_invalid_note_addrs_in_cwallettx) {
    CWalletTx wtx;
    EXPECT_EQ(0, wtx.mapNoteData.size());

    mapNoteData_t noteData;
    auto sk = libzcash::SpendingKey::random();
    JSOutPoint jsoutpt {wtx.GetTxid(), 0, 1};
    CNoteData nd {sk.address(), uint256()};
    noteData[jsoutpt] = nd;

    EXPECT_THROW(wtx.SetNoteData(noteData), std::runtime_error);
}

TEST(wallet_tests, find_note_in_tx) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx, 0, 1);
    auto nullifier = note.nullifier(sk);

    auto noteMap = wallet.FindMyNotes(wtx);
    EXPECT_EQ(1, noteMap.size());

    JSOutPoint jsoutpt {wtx.GetTxid(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    EXPECT_EQ(1, noteMap.count(jsoutpt));
    EXPECT_EQ(nd, noteMap[jsoutpt]);
}

TEST(wallet_tests, get_conflicted_notes) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx, 0, 1);
    auto nullifier = note.nullifier(sk);

    auto wtx2 = GetValidSpend(sk, note, 5);
    auto wtx3 = GetValidSpend(sk, note, 10);
    auto hash2 = wtx2.GetTxid();
    auto hash3 = wtx3.GetTxid();

    // No conflicts for no spends
    EXPECT_EQ(0, wallet.GetConflicts(hash2).size());
    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_EQ(0, wallet.GetConflicts(hash2).size());

    // No conflicts for one spend
    wallet.AddToWallet(wtx2, true, NULL);
    EXPECT_EQ(0, wallet.GetConflicts(hash2).size());

    // Conflicts for two spends
    wallet.AddToWallet(wtx3, true, NULL);
    auto c3 = wallet.GetConflicts(hash2);
    EXPECT_EQ(2, c3.size());
    EXPECT_EQ(std::set<uint256>({hash2, hash3}), c3);
}

TEST(wallet_tests, nullifier_is_spent) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx, 0, 1);
    auto nullifier = note.nullifier(sk);

    EXPECT_FALSE(wallet.IsSpent(nullifier));

    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_FALSE(wallet.IsSpent(nullifier));

    auto wtx2 = GetValidSpend(sk, note, 5);
    wallet.AddToWallet(wtx2, true, NULL);
    EXPECT_FALSE(wallet.IsSpent(nullifier));

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.vtx.push_back(wtx2);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    wtx2.SetMerkleBranch(block);
    wallet.AddToWallet(wtx2, true, NULL);
    EXPECT_TRUE(wallet.IsSpent(nullifier));

    // Tear down
    chainActive.SetTip(NULL);
    mapBlockIndex.erase(blockHash);
}

TEST(wallet_tests, navigate_from_nullifier_to_note) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx, 0, 1);
    auto nullifier = note.nullifier(sk);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.GetTxid(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;

    wtx.SetNoteData(noteData);

    EXPECT_EQ(0, wallet.mapNullifiersToNotes.count(nullifier));

    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_EQ(1, wallet.mapNullifiersToNotes.count(nullifier));
    EXPECT_EQ(wtx.GetTxid(), wallet.mapNullifiersToNotes[nullifier].hash);
    EXPECT_EQ(0, wallet.mapNullifiersToNotes[nullifier].js);
    EXPECT_EQ(1, wallet.mapNullifiersToNotes[nullifier].n);
}

TEST(wallet_tests, spent_note_is_from_me) {
    CWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx, 0, 1);
    auto nullifier = note.nullifier(sk);
    auto wtx2 = GetValidSpend(sk, note, 5);

    EXPECT_FALSE(wallet.IsFromMe(wtx));
    EXPECT_FALSE(wallet.IsFromMe(wtx2));

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.GetTxid(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;

    wtx.SetNoteData(noteData);
    EXPECT_FALSE(wallet.IsFromMe(wtx));
    EXPECT_FALSE(wallet.IsFromMe(wtx2));

    wallet.AddToWallet(wtx, true, NULL);
    EXPECT_FALSE(wallet.IsFromMe(wtx));
    EXPECT_TRUE(wallet.IsFromMe(wtx2));
}

TEST(wallet_tests, cached_witnesses_empty_chain) {
    TestWallet wallet;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    auto wtx = GetValidReceive(sk, 10, true);
    auto note = GetNote(sk, wtx, 0, 1);
    auto nullifier = note.nullifier(sk);

    mapNoteData_t noteData;
    JSOutPoint jsoutpt {wtx.GetTxid(), 0, 1};
    CNoteData nd {sk.address(), nullifier};
    noteData[jsoutpt] = nd;
    wtx.SetNoteData(noteData);

    std::vector<JSOutPoint> notes {jsoutpt};
    std::vector<boost::optional<ZCIncrementalWitness>> witnesses;
    uint256 anchor;

    wallet.GetNoteWitnesses(notes, witnesses, anchor);
    EXPECT_FALSE((bool) witnesses[0]);

    wallet.AddToWallet(wtx, true, NULL);
    witnesses.clear();
    wallet.GetNoteWitnesses(notes, witnesses, anchor);
    EXPECT_FALSE((bool) witnesses[0]);

    CBlock block;
    block.vtx.push_back(wtx);
    ZCIncrementalMerkleTree tree;
    wallet.IncrementNoteWitnesses(NULL, &block, tree);
    witnesses.clear();
    wallet.GetNoteWitnesses(notes, witnesses, anchor);
    EXPECT_TRUE((bool) witnesses[0]);

    wallet.DecrementNoteWitnesses();
    witnesses.clear();
    wallet.GetNoteWitnesses(notes, witnesses, anchor);
    EXPECT_FALSE((bool) witnesses[0]);
}

TEST(wallet_tests, cached_witnesses_chain_tip) {
    TestWallet wallet;
    uint256 anchor1;
    CBlock block1;
    ZCIncrementalMerkleTree tree;

    auto sk = libzcash::SpendingKey::random();
    wallet.AddSpendingKey(sk);

    {
        // First transaction (case tested in _empty_chain)
        auto wtx = GetValidReceive(sk, 10, true);
        auto note = GetNote(sk, wtx, 0, 1);
        auto nullifier = note.nullifier(sk);

        mapNoteData_t noteData;
        JSOutPoint jsoutpt {wtx.GetTxid(), 0, 1};
        CNoteData nd {sk.address(), nullifier};
        noteData[jsoutpt] = nd;
        wtx.SetNoteData(noteData);
        wallet.AddToWallet(wtx, true, NULL);

        std::vector<JSOutPoint> notes {jsoutpt};
        std::vector<boost::optional<ZCIncrementalWitness>> witnesses;

        // First block (case tested in _empty_chain)
        block1.vtx.push_back(wtx);
        wallet.IncrementNoteWitnesses(NULL, &block1, tree);
        // Called to fetch anchor
        wallet.GetNoteWitnesses(notes, witnesses, anchor1);
    }

    {
        // Second transaction
        auto wtx = GetValidReceive(sk, 50, true);
        auto note = GetNote(sk, wtx, 0, 1);
        auto nullifier = note.nullifier(sk);

        mapNoteData_t noteData;
        JSOutPoint jsoutpt {wtx.GetTxid(), 0, 1};
        CNoteData nd {sk.address(), nullifier};
        noteData[jsoutpt] = nd;
        wtx.SetNoteData(noteData);
        wallet.AddToWallet(wtx, true, NULL);

        std::vector<JSOutPoint> notes {jsoutpt};
        std::vector<boost::optional<ZCIncrementalWitness>> witnesses;
        uint256 anchor2;

        wallet.GetNoteWitnesses(notes, witnesses, anchor2);
        EXPECT_FALSE((bool) witnesses[0]);

        // Second block
        CBlock block2;
        block2.hashPrevBlock = block1.GetHash();
        block2.vtx.push_back(wtx);
        ZCIncrementalMerkleTree tree2 {tree};
        wallet.IncrementNoteWitnesses(NULL, &block2, tree2);
        witnesses.clear();
        wallet.GetNoteWitnesses(notes, witnesses, anchor2);
        EXPECT_TRUE((bool) witnesses[0]);
        EXPECT_NE(anchor1, anchor2);

        // Decrementing should give us the previous anchor
        uint256 anchor3;
        wallet.DecrementNoteWitnesses();
        witnesses.clear();
        wallet.GetNoteWitnesses(notes, witnesses, anchor3);
        EXPECT_FALSE((bool) witnesses[0]);
        // Should not equal first anchor because none of these notes had witnesses
        EXPECT_NE(anchor1, anchor3);

        // Re-incrementing with the same block should give the same result
        uint256 anchor4;
        wallet.IncrementNoteWitnesses(NULL, &block2, tree);
        witnesses.clear();
        wallet.GetNoteWitnesses(notes, witnesses, anchor4);
        EXPECT_TRUE((bool) witnesses[0]);
        EXPECT_EQ(anchor2, anchor4);
    }
}
