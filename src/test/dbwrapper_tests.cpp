// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dbwrapper.h"
#include "uint256.h"
#include "random.h"
#include "test/test_bitcoin.h"

#include <boost/assign/std/vector.hpp> // for 'operator+=()'
#include <boost/assert.hpp>
#include <boost/test/unit_test.hpp>
                    
using namespace std;
using namespace boost::assign; // bring 'operator+=()' into scope
using namespace boost::filesystem;
         
// Test if a string consists entirely of null characters
bool is_null_key(const vector<unsigned char>& key) {
    bool isnull = true;

    for (unsigned int i = 0; i < key.size(); i++)
        isnull &= (key[i] == '\x00');

    return isnull;
}
 
BOOST_FIXTURE_TEST_SUITE(dbwrapper_tests, BasicTestingSetup)
                       
BOOST_AUTO_TEST_CASE(dbwrapper)
{
    {
        path ph = temp_directory_path() / unique_path();
        CDBWrapper dbw(ph, (1 << 20), true, false);
        char key = 'k';
        uint256 in = GetRandHash();
        uint256 res;

        BOOST_CHECK(dbw.Write(key, in));
        BOOST_CHECK(dbw.Read(key, res));
        BOOST_CHECK_EQUAL(res.ToString(), in.ToString());
    }
}

// Test batch operations
BOOST_AUTO_TEST_CASE(dbwrapper_batch)
{
    {
        path ph = temp_directory_path() / unique_path();
        CDBWrapper dbw(ph, (1 << 20), true, false);

        char key = 'i';
        uint256 in = GetRandHash();
        char key2 = 'j';
        uint256 in2 = GetRandHash();
        char key3 = 'k';
        uint256 in3 = GetRandHash();

        uint256 res;
        CDBBatch batch(dbw);

        batch.Write(key, in);
        batch.Write(key2, in2);
        batch.Write(key3, in3);

        // Remove key3 before it's even been written
        batch.Erase(key3);

        dbw.WriteBatch(batch);

        BOOST_CHECK(dbw.Read(key, res));
        BOOST_CHECK_EQUAL(res.ToString(), in.ToString());
        BOOST_CHECK(dbw.Read(key2, res));
        BOOST_CHECK_EQUAL(res.ToString(), in2.ToString());

        // key3 never should've been written
        BOOST_CHECK(dbw.Read(key3, res) == false);
    }
}

BOOST_AUTO_TEST_CASE(dbwrapper_iterator)
{
    {
        path ph = temp_directory_path() / unique_path();
        CDBWrapper dbw(ph, (1 << 20), true, false);

        // The two keys are intentionally chosen for ordering
        char key = 'j';
        uint256 in = GetRandHash();
        BOOST_CHECK(dbw.Write(key, in));
        char key2 = 'k';
        uint256 in2 = GetRandHash();
        BOOST_CHECK(dbw.Write(key2, in2));

        boost::scoped_ptr<CDBIterator> it(const_cast<CDBWrapper*>(&dbw)->NewIterator());

        // Be sure to seek past any earlier key (if it exists)
        it->Seek(key);

        char key_res;
        uint256 val_res;

        it->GetKey(key_res);
        it->GetValue(val_res);
        BOOST_CHECK_EQUAL(key_res, key);
        BOOST_CHECK_EQUAL(val_res.ToString(), in.ToString());

        it->Next();

        it->GetKey(key_res);
        it->GetValue(val_res);
        BOOST_CHECK_EQUAL(key_res, key2);
        BOOST_CHECK_EQUAL(val_res.ToString(), in2.ToString());

        it->Next();
        BOOST_CHECK_EQUAL(it->Valid(), false);
    }
}

BOOST_AUTO_TEST_CASE(iterator_ordering)
{
    path ph = temp_directory_path() / unique_path();
    CDBWrapper dbw(ph, (1 << 20), true, false);
    for (int x=0x00; x<256; ++x) {
        uint8_t key = x;
        uint32_t value = x*x;
        BOOST_CHECK(dbw.Write(key, value));
    }

    boost::scoped_ptr<CDBIterator> it(const_cast<CDBWrapper*>(&dbw)->NewIterator());
    for (int c=0; c<2; ++c) {
        int seek_start;
        if (c == 0)
            seek_start = 0x00;
        else
            seek_start = 0x80;
        it->Seek((uint8_t)seek_start);
        for (int x=seek_start; x<256; ++x) {
            uint8_t key;
            uint32_t value;
            BOOST_CHECK(it->Valid());
            if (!it->Valid()) // Avoid spurious errors about invalid iterator's key and value in case of failure
                break;
            BOOST_CHECK(it->GetKey(key));
            BOOST_CHECK(it->GetValue(value));
            BOOST_CHECK_EQUAL(key, x);
            BOOST_CHECK_EQUAL(value, x*x);
            it->Next();
        }
        BOOST_CHECK(!it->Valid());
    }
}

BOOST_AUTO_TEST_SUITE_END()
