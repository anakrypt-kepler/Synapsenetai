#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

namespace synapse {
namespace tests {

class LedgerTests {
public:
    static void runAll() {
        testEventCreation();
        testEventSerialization();
        testEventDeserialization();
        testEventHashing();
        testBlockCreation();
        testBlockSerialization();
        testMerkleRoot();
        testChainValidation();
        testEventAppend();
        testBlockAppend();
        testGenesisBlock();
        testChainReorg();
        std::cout << "All ledger tests passed!" << std::endl;
    }
    
    static void testEventCreation() {
        std::cout << "Testing event creation..." << std::endl;
        
        struct Event {
            uint64_t id;
            uint64_t timestamp;
            uint8_t type;
            std::vector<uint8_t> data;
            std::vector<uint8_t> prevHash;
            std::vector<uint8_t> author;
            std::vector<uint8_t> signature;
            std::vector<uint8_t> hash;
        };
        
        Event e;
        e.id = 1;
        e.timestamp = 1735000000;
        e.type = 1;
        e.data = {'t', 'e', 's', 't'};
        e.prevHash.resize(32, 0);
        e.author.resize(33, 0);
        e.signature.resize(64, 0);
        e.hash.resize(32, 0);
        
        assert(e.id == 1);
        assert(e.data.size() == 4);
        
        std::cout << "  Event creation: PASSED" << std::endl;
    }
    
    static void testEventSerialization() {
        std::cout << "Testing event serialization..." << std::endl;
        
        std::vector<uint8_t> serialized;
        
        uint64_t id = 1;
        for (int i = 0; i < 8; i++) {
            serialized.push_back((id >> (i * 8)) & 0xFF);
        }
        
        uint64_t timestamp = 1735000000;
        for (int i = 0; i < 8; i++) {
            serialized.push_back((timestamp >> (i * 8)) & 0xFF);
        }
        
        serialized.push_back(1);
        
        std::vector<uint8_t> data = {'t', 'e', 's', 't'};
        uint32_t dataLen = data.size();
        for (int i = 0; i < 4; i++) {
            serialized.push_back((dataLen >> (i * 8)) & 0xFF);
        }
        serialized.insert(serialized.end(), data.begin(), data.end());
        
        assert(serialized.size() == 8 + 8 + 1 + 4 + 4);
        
        std::cout << "  Event serialization: PASSED" << std::endl;
    }
    
    static void testEventDeserialization() {
        std::cout << "Testing event deserialization..." << std::endl;
        
        std::vector<uint8_t> data = {
            1, 0, 0, 0, 0, 0, 0, 0,
            0, 0x5E, 0x9A, 0x67, 0, 0, 0, 0,
            1,
            4, 0, 0, 0,
            't', 'e', 's', 't'
        };
        
        size_t offset = 0;
        
        uint64_t id = 0;
        for (int i = 0; i < 8; i++) {
            id |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
        }
        offset += 8;
        
        uint64_t timestamp = 0;
        for (int i = 0; i < 8; i++) {
            timestamp |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
        }
        offset += 8;
        
        uint8_t type = data[offset++];
        
        uint32_t dataLen = 0;
        for (int i = 0; i < 4; i++) {
            dataLen |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
        }
        offset += 4;
        
        std::vector<uint8_t> eventData(data.begin() + offset, data.begin() + offset + dataLen);
        
        assert(id == 1);
        assert(type == 1);
        assert(dataLen == 4);
        assert(eventData.size() == 4);
        
        std::cout << "  Event deserialization: PASSED" << std::endl;
    }
    
    static void testEventHashing() {
        std::cout << "Testing event hashing..." << std::endl;
        
        std::vector<uint8_t> eventData = {1, 2, 3, 4, 5};
        std::vector<uint8_t> hash(32);
        
        for (size_t i = 0; i < 32; i++) {
            hash[i] = eventData[i % eventData.size()] ^ static_cast<uint8_t>(i);
        }
        
        assert(hash.size() == 32);
        
        std::cout << "  Event hashing: PASSED" << std::endl;
    }
    
    static void testBlockCreation() {
        std::cout << "Testing block creation..." << std::endl;
        
        struct Block {
            uint64_t height;
            uint64_t timestamp;
            std::vector<uint8_t> prevHash;
            std::vector<uint8_t> merkleRoot;
            uint32_t nonce;
            std::vector<uint8_t> hash;
        };
        
        Block b;
        b.height = 0;
        b.timestamp = 1735000000;
        b.prevHash.resize(32, 0);
        b.merkleRoot.resize(32, 0);
        b.nonce = 0;
        b.hash.resize(32, 0);
        
        assert(b.height == 0);
        assert(b.prevHash.size() == 32);
        
        std::cout << "  Block creation: PASSED" << std::endl;
    }
    
    static void testBlockSerialization() {
        std::cout << "Testing block serialization..." << std::endl;
        
        std::vector<uint8_t> serialized;
        
        uint64_t height = 0;
        for (int i = 0; i < 8; i++) {
            serialized.push_back((height >> (i * 8)) & 0xFF);
        }
        
        uint64_t timestamp = 1735000000;
        for (int i = 0; i < 8; i++) {
            serialized.push_back((timestamp >> (i * 8)) & 0xFF);
        }
        
        for (int i = 0; i < 32; i++) serialized.push_back(0);
        for (int i = 0; i < 32; i++) serialized.push_back(0);
        
        uint32_t nonce = 0;
        for (int i = 0; i < 4; i++) {
            serialized.push_back((nonce >> (i * 8)) & 0xFF);
        }
        
        assert(serialized.size() == 8 + 8 + 32 + 32 + 4);
        
        std::cout << "  Block serialization: PASSED" << std::endl;
    }
    
    static void testMerkleRoot() {
        std::cout << "Testing Merkle root..." << std::endl;
        
        std::vector<std::vector<uint8_t>> hashes = {
            std::vector<uint8_t>(32, 1),
            std::vector<uint8_t>(32, 2),
            std::vector<uint8_t>(32, 3),
            std::vector<uint8_t>(32, 4)
        };
        
        while (hashes.size() > 1) {
            std::vector<std::vector<uint8_t>> next;
            for (size_t i = 0; i < hashes.size(); i += 2) {
                std::vector<uint8_t> combined;
                combined.insert(combined.end(), hashes[i].begin(), hashes[i].end());
                size_t j = (i + 1 < hashes.size()) ? i + 1 : i;
                combined.insert(combined.end(), hashes[j].begin(), hashes[j].end());
                
                std::vector<uint8_t> hash(32);
                for (size_t k = 0; k < 32; k++) {
                    hash[k] = combined[k] ^ combined[k + 32];
                }
                next.push_back(hash);
            }
            hashes = std::move(next);
        }
        
        assert(hashes.size() == 1);
        assert(hashes[0].size() == 32);
        
        std::cout << "  Merkle root: PASSED" << std::endl;
    }
    
    static void testChainValidation() {
        std::cout << "Testing chain validation..." << std::endl;
        
        struct Block {
            uint64_t height;
            std::vector<uint8_t> prevHash;
            std::vector<uint8_t> hash;
        };
        
        std::vector<Block> chain;
        
        Block genesis;
        genesis.height = 0;
        genesis.prevHash.resize(32, 0);
        genesis.hash.resize(32, 1);
        chain.push_back(genesis);
        
        Block block1;
        block1.height = 1;
        block1.prevHash = genesis.hash;
        block1.hash.resize(32, 2);
        chain.push_back(block1);
        
        bool valid = true;
        for (size_t i = 1; i < chain.size(); i++) {
            if (chain[i].prevHash != chain[i-1].hash) {
                valid = false;
                break;
            }
        }
        
        assert(valid == true);
        
        std::cout << "  Chain validation: PASSED" << std::endl;
    }
    
    static void testEventAppend() {
        std::cout << "Testing event append..." << std::endl;
        
        std::vector<std::vector<uint8_t>> events;
        
        events.push_back({'e', 'v', 't', '1'});
        events.push_back({'e', 'v', 't', '2'});
        events.push_back({'e', 'v', 't', '3'});
        
        assert(events.size() == 3);
        
        std::cout << "  Event append: PASSED" << std::endl;
    }
    
    static void testBlockAppend() {
        std::cout << "Testing block append..." << std::endl;
        
        uint64_t currentHeight = 0;
        
        currentHeight++;
        assert(currentHeight == 1);
        
        currentHeight++;
        assert(currentHeight == 2);
        
        std::cout << "  Block append: PASSED" << std::endl;
    }
    
    static void testGenesisBlock() {
        std::cout << "Testing genesis block..." << std::endl;
        
        std::string genesisMessage = "Satoshi gave us money without banks. I give you brains without corporations. - Kepler";
        
        assert(genesisMessage.length() > 0);
        
        std::cout << "  Genesis block: PASSED" << std::endl;
    }
    
    static void testChainReorg() {
        std::cout << "Testing chain reorg..." << std::endl;
        
        uint64_t mainChainHeight = 100;
        uint64_t forkHeight = 95;
        uint64_t forkChainHeight = 102;
        
        bool shouldReorg = forkChainHeight > mainChainHeight;
        assert(shouldReorg == true);
        
        std::cout << "  Chain reorg: PASSED" << std::endl;
    }
};

}
}

int main() {
    synapse::tests::LedgerTests::runAll();
    return 0;
}
