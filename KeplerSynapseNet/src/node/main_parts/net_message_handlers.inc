	    void handlePoeInvMessage(const std::string& peerId, const network::Message& msg) {
	        if (!network_ || !poeV1_) return;
	        if (msg.payload.size() < 1 + crypto::SHA256_SIZE + 4) return;

	        PoeInvKind kind = static_cast<PoeInvKind>(msg.payload[0]);
	        crypto::Hash256 after{};
	        std::memcpy(after.data(), msg.payload.data() + 1, after.size());
	        uint32_t count = readU32LE(msg.payload, 1 + crypto::SHA256_SIZE);
	        size_t expected = 1 + crypto::SHA256_SIZE + 4 + static_cast<size_t>(count) * crypto::SHA256_SIZE;
	        if (expected > msg.payload.size()) return;

	        uint32_t limit = 0;
	        {
	            std::lock_guard<std::mutex> lock(poeSyncMtx_);
	            auto it = poeSync_.find(peerId);
	            if (it == poeSync_.end()) return;
	            PoeSyncState* st = nullptr;
	            if (kind == PoeInvKind::ENTRY) st = &it->second.entries;
	            else if (kind == PoeInvKind::VOTE) st = &it->second.votes;
	            else if (kind == PoeInvKind::EPOCH) st = &it->second.epochs;
	            if (!st) return;
	            if (!st->active || st->done) return;
	            if (!st->inFlight) return;
	            if (st->after != after) return;
	            limit = st->limit;
	        }

	        synapse::InvMessage inv;
	        inv.items.reserve(count);
	        const uint8_t* ptr = msg.payload.data() + 1 + crypto::SHA256_SIZE + 4;
	        for (uint32_t i = 0; i < count; ++i) {
	            synapse::InvItem item;
	            if (kind == PoeInvKind::ENTRY) item.type = synapse::InvType::POE_ENTRY;
	            else if (kind == PoeInvKind::VOTE) item.type = synapse::InvType::POE_VOTE;
	            else if (kind == PoeInvKind::EPOCH) item.type = synapse::InvType::POE_EPOCH;
	            else return;
	            std::memcpy(item.hash.data(), ptr, crypto::SHA256_SIZE);
	            ptr += crypto::SHA256_SIZE;
	            inv.items.push_back(item);
	        }

	        network::Message fakeInv = makeMessage("inv", inv.serialize());
	        handleInvMessage(peerId, fakeInv);

	        crypto::Hash256 nextAfter = after;
	        if (!inv.items.empty()) {
	            std::memcpy(nextAfter.data(), inv.items.back().hash.data(), nextAfter.size());
	        }

	        bool shouldContinue = (count == limit) && (nextAfter != after);
	        bool done = !shouldContinue;
	        uint64_t now = std::time(nullptr);

	        {
	            std::lock_guard<std::mutex> lock(poeSyncMtx_);
	            auto it = poeSync_.find(peerId);
	            if (it != poeSync_.end()) {
	                PoeSyncState* st = nullptr;
	                if (kind == PoeInvKind::ENTRY) st = &it->second.entries;
	                else if (kind == PoeInvKind::VOTE) st = &it->second.votes;
	                else if (kind == PoeInvKind::EPOCH) st = &it->second.epochs;
	                if (st) {
	                    st->inFlight = false;
	                    if (done) {
	                        st->done = true;
	                    } else {
	                        st->after = nextAfter;
	                        st->inFlight = true;
	                        st->lastRequestAt = now;
	                        st->pages += 1;
	                    }
	                }
	            }
	        }

	        if (shouldContinue) {
	            sendPoeGetInv(peerId, kind, nextAfter, limit);
	        }
	    }
	    
	    void broadcastInv(synapse::InvType type, const crypto::Hash256& hash) {
	        if (!network_) return;
            const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
            const std::string key = std::to_string(static_cast<int>(type)) + ":" + crypto::toHex(hash);
            {
                std::lock_guard<std::mutex> lock(invMtx_);
                auto it = invGossipSeenAt_.find(key);
                if (it != invGossipSeenAt_.end() &&
                    now <= it->second + static_cast<uint64_t>(config_.networkGossipDedupWindowSeconds)) {
                    gossipSuppressed_.fetch_add(1);
                    return;
                }
                invGossipSeenAt_[key] = now;
                if (invGossipSeenAt_.size() > 65536) {
                    const uint64_t ttl = std::max<uint32_t>(1, config_.networkGossipDedupWindowSeconds) * 3ULL;
                    for (auto itr = invGossipSeenAt_.begin(); itr != invGossipSeenAt_.end();) {
                        if (now > itr->second + ttl) itr = invGossipSeenAt_.erase(itr);
                        else ++itr;
                    }
                }
            }

	        synapse::InvMessage inv;
            synapse::InvItem item;
            item.type = type;
            std::memcpy(item.hash.data(), hash.data(), hash.size());
            inv.items.push_back(item);
            auto msg = makeMessage("inv", inv.serialize());

            auto peers = network_->getPeers();
            if (peers.empty()) return;
            uint32_t fanout = std::max<uint32_t>(1, config_.networkGossipFanoutLimit);
            auto netStats = network_->getStats();
            if (netStats.overloadMode) {
                fanout = std::max<uint32_t>(1, fanout / 2);
            }

            if (peers.size() <= fanout) {
                network_->broadcast(msg);
                return;
            }

            std::sort(peers.begin(), peers.end(), [](const network::Peer& a, const network::Peer& b) {
                return a.id < b.id;
            });
            uint64_t startSeed = 0;
            std::memcpy(&startSeed, hash.data(), std::min<size_t>(sizeof(startSeed), hash.size()));
            size_t start = peers.empty() ? 0 : static_cast<size_t>(startSeed % peers.size());
            uint64_t sent = 0;
            for (size_t step = 0; step < peers.size() && sent < fanout; ++step) {
                size_t idx = (start + step) % peers.size();
                if (peers[idx].state != network::PeerState::CONNECTED) continue;
                if (network_->send(peers[idx].id, msg)) {
                    sent += 1;
                }
            }
            if (sent == 0) {
                network_->broadcast(msg);
            } else {
                gossipSubsetRouted_.fetch_add(sent);
            }
    }
    
	    void handleMessage(const std::string& peerId, const network::Message& msg) {
	        if (msg.command == "ping") {
            network::Message pong;
            pong.command = "pong";
            pong.payload = msg.payload;
            network_->send(peerId, pong);
        } else if (msg.command == "pong") {
            handlePongMessage(peerId, msg);
        } else if (msg.command == "version") {
            handleVersionMessage(peerId, msg);
        } else if (msg.command == "verack") {
            handleVerackMessage(peerId, msg);
        } else if (msg.command == "peer_hello") {
            handlePeerHelloMessage(peerId, msg);
        } else if (msg.command == "getaddr") {
            handleGetAddrMessage(peerId, msg);
        } else if (msg.command == "addr") {
            handleAddrMessage(peerId, msg);
        } else if (msg.command == "getpeers") {
            handleGetPeersMessage(peerId, msg);
        } else if (msg.command == "peers") {
            handlePeersMessage(peerId, msg);
        } else if (msg.command == "inv") {
            handleInvMessage(peerId, msg);
        } else if (msg.command == "getdata") {
            handleGetDataMessage(peerId, msg);
        } else if (msg.command == "getblock") {
            handleGetBlockMessage(peerId, msg);
        } else if (msg.command == "block") {
            handleBlockMessage(peerId, msg);
	        } else if (msg.command == "knowledge") {
	            handleKnowledgeMessage(peerId, msg);
	        } else if (msg.command == "tx") {
	            handleTxMessage(peerId, msg);
	        } else if (msg.command == "mempool") {
	            handleMempoolMessage(peerId, msg);
	        } else if (msg.command == "poe_getinv") {
	            handlePoeGetInvMessage(peerId, msg);
	        } else if (msg.command == "poe_inv") {
	            handlePoeInvMessage(peerId, msg);
	        } else if (msg.command == "poe_entry") {
	            handlePoeEntryMessage(peerId, msg);
	        } else if (msg.command == "poe_vote") {
	            handlePoeVoteMessage(peerId, msg);
	        } else if (msg.command == "poe_epoch") {
	            handlePoeEpochMessage(peerId, msg);
	        } else if (msg.command == "upd_manifest") {
	            handleUpdateManifestMessage(peerId, msg);
	        } else if (msg.command == "m_offer") {
	            handleRemoteOfferMessage(peerId, msg);
	        } else if (msg.command == "m_rent") {
	            handleRemoteRentMessage(peerId, msg);
	        } else if (msg.command == "m_rentok") {
	            handleRemoteRentOkMessage(peerId, msg);
	        } else if (msg.command == "m_infer") {
	            handleRemoteInferMessage(peerId, msg);
	        } else if (msg.command == "m_out") {
	            handleRemoteOutMessage(peerId, msg);
	        }
	    }

    static std::string pubKeyHex33(const std::array<uint8_t, 33>& pk) {
        return crypto::toHex(pk.data(), pk.size());
    }

    static std::string randomHex16() {
        std::array<uint8_t, 16> b{};
        std::random_device rd;
        for (auto& v : b) v = static_cast<uint8_t>(rd());
        return crypto::toHex(b.data(), b.size());
    }

    synapse::RemoteModelOfferMessage buildLocalOffer(uint64_t now) {
        synapse::RemoteModelOfferMessage offer;
        {
            std::lock_guard<std::mutex> lock(remoteProvMtx_);
            offer.offerId = localOfferId_;
        }
        offer.providerAddress = address_;
        offer.pricePerRequestAtoms = remotePricePerRequestAtoms_;
        offer.maxSlots = modelAccess_ ? modelAccess_->getMaxSlots() : 0;
        offer.usedSlots = modelAccess_ ? modelAccess_->getActiveSlots() : 0;
        offer.expiresAt = now + 120;

        if (modelLoader_) {
            std::lock_guard<std::mutex> lock(modelMtx_);
            auto info = modelLoader_->getInfo();
            offer.modelId = info.name.empty() ? "active" : info.name;
        }
        if (offer.modelId.empty()) offer.modelId = "active";
        return offer;
    }

    bool verifyPaymentToSelf(const std::string& paymentTxidHex, uint64_t minAtoms, uint64_t& paidOut) const {
        paidOut = 0;
        if (!transfer_) return false;
        auto bytes = crypto::fromHex(paymentTxidHex);
        if (bytes.size() != 32) return false;
        crypto::Hash256 txid{};
        std::memcpy(txid.data(), bytes.data(), txid.size());
        if (!transfer_->hasTransaction(txid)) return false;
        core::Transaction tx = transfer_->getTransaction(txid);
        if (tx.outputs.empty()) return false;
        for (const auto& outp : tx.outputs) {
            if (outp.address == address_) {
                if (UINT64_MAX - paidOut < outp.amount) return false;
                paidOut += outp.amount;
            }
        }
        return paidOut >= minAtoms;
    }

    void handleRemoteOfferMessage(const std::string& peerId, const network::Message& msg) {
        if (msg.payload.empty()) return;
        synapse::RemoteModelOfferMessage offer;
        try {
            offer = synapse::RemoteModelOfferMessage::deserialize(msg.payload);
        } catch (...) {
            return;
        }
        if (offer.offerId.empty() || offer.modelId.empty() || offer.providerAddress.empty()) return;
        uint64_t now = std::time(nullptr);
        if (offer.expiresAt != 0 && offer.expiresAt < now) return;
        std::lock_guard<std::mutex> lock(remoteMtx_);
        RemoteOfferCache c;
        c.offer = offer;
        c.peerId = peerId;
        c.receivedAt = now;
        remoteOffers_[offer.offerId] = std::move(c);
    }

    void handleRemoteRentMessage(const std::string& peerId, const network::Message& msg) {
        if (msg.payload.empty()) return;
        if (!modelAccess_) return;
        synapse::RemoteModelRentMessage rent;
        try {
            rent = synapse::RemoteModelRentMessage::deserialize(msg.payload);
        } catch (...) {
            return;
        }
        if (rent.offerId.empty()) return;

        std::string expectedOffer;
        {
            std::lock_guard<std::mutex> lock(remoteProvMtx_);
            expectedOffer = localOfferId_;
        }
        if (expectedOffer.empty() || rent.offerId != expectedOffer) return;

        const std::string renterId = pubKeyHex33(rent.renterPubKey);
        if (!modelAccess_->canAccess(renterId)) return;
        if (!modelAccess_->hasAvailableSlot()) return;

        const uint64_t now = std::time(nullptr);
        const uint64_t sessionTtl = config_.dev ? 900 : 3600;
        const uint64_t expiresAt = now + sessionTtl;

        if (!modelAccess_->startSession(renterId)) return;

        synapse::RemoteModelRentOkMessage ok;
        ok.offerId = rent.offerId;
        if (!modelMarketplace_) return;
        ok.sessionId = modelMarketplace_->rentModel(ok.offerId, renterId);
        if (ok.sessionId.empty()) {
            (void)modelAccess_->endSession(renterId);
            return;
        }
        ok.providerAddress = address_;
        ok.pricePerRequestAtoms = remotePricePerRequestAtoms_;
        ok.expiresAt = expiresAt;

        {
            std::lock_guard<std::mutex> lock(remoteProvMtx_);
            ProviderSession ps;
            ps.renterId = renterId;
            ps.expiresAt = expiresAt;
            ps.pricePerRequestAtoms = remotePricePerRequestAtoms_;
            providerSessions_[ok.sessionId] = std::move(ps);
        }

        if (network_) {
            auto reply = makeMessage("m_rentok", ok.serialize());
            network_->send(peerId, reply);
        }
    }

    void handleRemoteRentOkMessage(const std::string& peerId, const network::Message& msg) {
        if (msg.payload.empty()) return;
        synapse::RemoteModelRentOkMessage ok;
        try {
            ok = synapse::RemoteModelRentOkMessage::deserialize(msg.payload);
        } catch (...) {
            return;
        }
        if (ok.offerId.empty() || ok.sessionId.empty() || ok.providerAddress.empty()) return;
        RemoteSessionInfo s;
        s.peerId = peerId;
        s.sessionId = ok.sessionId;
        s.providerAddress = ok.providerAddress;
        s.pricePerRequestAtoms = ok.pricePerRequestAtoms;
        s.expiresAt = ok.expiresAt;
        {
            std::lock_guard<std::mutex> lock(remoteMtx_);
            remoteSessions_[s.sessionId] = s;
            remoteRentOkByOffer_[ok.offerId] = ok;
        }
        remoteCv_.notify_all();
    }

    void handleRemoteInferMessage(const std::string& peerId, const network::Message& msg) {
        if (msg.payload.empty()) return;
        if (!modelLoader_ || !modelAccess_) return;
        synapse::RemoteModelInferMessage req;
        try {
            req = synapse::RemoteModelInferMessage::deserialize(msg.payload);
        } catch (...) {
            return;
        }
        if (req.sessionId.empty() || req.requestId.empty() || req.prompt.empty()) return;

        ProviderSession sess;
        {
            std::lock_guard<std::mutex> lock(remoteProvMtx_);
            auto it = providerSessions_.find(req.sessionId);
            if (it == providerSessions_.end()) return;
            sess = it->second;
        }
        const uint64_t now = std::time(nullptr);
        if (sess.expiresAt != 0 && sess.expiresAt < now) return;

        const std::string renterId = pubKeyHex33(req.renterPubKey);
        if (!renterId.empty() && renterId != sess.renterId) return;
        if (!modelAccess_->canAccess(sess.renterId)) return;
        if (modelAccess_->isRateLimited(sess.renterId)) return;

        // Enforce payment (mempool/confirmed). Provider address is our local address_.
        uint64_t paid = 0;
        bool paidOk = false;
        if (sess.pricePerRequestAtoms > 0) {
            paidOk = verifyPaymentToSelf(req.paymentTxidHex, sess.pricePerRequestAtoms, paid);
        } else {
            paidOk = true;
        }

        auto sendErr = [&](const std::string& errText) {
            synapse::RemoteModelOutMessage out;
            out.requestId = req.requestId;
            out.text = errText;
            out.tokensUsed = 0;
            out.latencyMs = 0;
            if (network_) {
                network_->send(peerId, makeMessage("m_out", out.serialize()));
            }
        };

        if (!paidOk) {
            sendErr("ERROR: payment_invalid_or_missing");
            return;
        }

        std::string resultText;
        uint64_t startMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

        {
            std::lock_guard<std::mutex> lock(modelMtx_);
            if (!modelLoader_->isLoaded()) {
                sendErr("ERROR: model_not_loaded");
                return;
            }
            if (modelLoader_->isGenerating()) {
                sendErr("ERROR: model_busy");
                return;
            }
            model::GenerationParams gp;
            gp.maxTokens = std::max<uint32_t>(1, req.maxTokens);
            gp.temperature = std::max(0.0f, req.temperature);
            gp.topP = std::max(0.0f, req.topP);
            gp.topK = req.topK;
            resultText = modelLoader_->generate(req.prompt, gp);
        }

        uint64_t endMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        uint64_t latency = (endMs >= startMs) ? (endMs - startMs) : 0;

        if (paid > 0) {
            modelAccess_->recordPayment(sess.renterId, paid);
            if (modelMarketplace_) {
                (void)modelMarketplace_->recordPayment(req.sessionId, paid);
            }
        }
        modelAccess_->recordRequest(sess.renterId, 0, static_cast<double>(latency));
        if (modelMarketplace_) {
            (void)modelMarketplace_->recordRequest(req.sessionId, 0, latency);
        }

        synapse::RemoteModelOutMessage out;
        out.requestId = req.requestId;
        out.text = resultText;
        out.tokensUsed = 0;
        out.latencyMs = latency;
        if (network_) {
            network_->send(peerId, makeMessage("m_out", out.serialize()));
        }
    }

    void handleRemoteOutMessage(const std::string& peerId, const network::Message& msg) {
        (void)peerId;
        if (msg.payload.empty()) return;
        synapse::RemoteModelOutMessage out;
        try {
            out = synapse::RemoteModelOutMessage::deserialize(msg.payload);
        } catch (...) {
            return;
        }
        if (out.requestId.empty()) return;
        {
            std::lock_guard<std::mutex> lock(remoteMtx_);
            auto it = remotePending_.find(out.requestId);
            if (it != remotePending_.end()) {
                it->second.done = true;
                it->second.text = out.text;
                it->second.tokensUsed = out.tokensUsed;
                it->second.latencyMs = out.latencyMs;
            }
        }
        remoteCv_.notify_all();
    }
    
    void handleVersionMessage(const std::string& peerId, const network::Message& msg) {
        if (msg.payload.size() < 40) return;
        synapse::VersionMessage v = synapse::VersionMessage::deserialize(msg.payload);
        peerHeights_[peerId] = v.startHeight;
        if (v.startHeight > networkHeight_) {
            networkHeight_ = v.startHeight;
        }
        
        if (discovery_ && v.addrRecv[10] == 0xff && v.addrRecv[11] == 0xff) {
            char ipStr[INET_ADDRSTRLEN];
            in_addr ipv4{};
            std::memcpy(&ipv4, v.addrRecv.data() + 12, 4);
            if (inet_ntop(AF_INET, &ipv4, ipStr, sizeof(ipStr))) {
                std::string ourIP = ipStr;
                if (ourIP != "127.0.0.1" && ourIP != "0.0.0.0" && ourIP.find("127.") != 0) {
                    discovery_->setExternalAddress(ourIP);
                }
            }
        }
        
        sendVerack(peerId);
        sendPeerHello(peerId);
        sendGetAddr(peerId);
        sendMempoolRequest(peerId);
        sendPoeInventory(peerId);
        sendUpdateBundleInventory(peerId);
        startPoeSync(peerId);
    }

    void handlePeerHelloMessage(const std::string& peerId, const network::Message& msg) {
        const auto parsed = core::parsePeerHelloPayload(msg.payload);
        if (!parsed) {
            utils::Logger::warn("Ignored malformed peer_hello from " + peerId);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(peerHelloMtx_);
            peerHelloById_[peerId] = *parsed;
        }
    }
    
    void handleVerackMessage(const std::string& peerId, const network::Message& msg) {
        (void)peerId;
        (void)msg;
    }
    
    void handlePongMessage(const std::string& peerId, const network::Message& msg) {
        (void)peerId;
        (void)msg;
    }
    
    void handleGetAddrMessage(const std::string& peerId, const network::Message& msg) {
        (void)msg;
        if (!discovery_) return;
        auto peers = discovery_->getKnownPeers(100);
        synapse::PeersMessage peersMsg;
        for (const auto& peer : peers) {
            synapse::PeerAddress addr{};
            addr.timestamp = peer.timestamp;
            addr.services = peer.services;
            addr.port = peer.port;
            addr.addr.fill(0);
            in_addr ipv4{};
            if (inet_pton(AF_INET, peer.address.c_str(), &ipv4) == 1) {
                addr.addr[10] = 0xff;
                addr.addr[11] = 0xff;
                std::memcpy(addr.addr.data() + 12, &ipv4, 4);
                peersMsg.peers.push_back(addr);
            }
        }
        auto reply = makeMessage("addr", peersMsg.serialize());
        network_->send(peerId, reply);
    }
    
    void handleAddrMessage(const std::string& peerId, const network::Message& msg) {
        (void)peerId;
        if (msg.payload.empty()) return;
        if (!discovery_) return;
        synapse::PeersMessage peersMsg = synapse::PeersMessage::deserialize(msg.payload);
        std::vector<network::PeerInfo> incoming;
        for (const auto& addr : peersMsg.peers) {
            if (addr.addr[10] == 0xff && addr.addr[11] == 0xff) {
                char ipStr[INET_ADDRSTRLEN];
                in_addr ipv4{};
                std::memcpy(&ipv4, addr.addr.data() + 12, 4);
                if (inet_ntop(AF_INET, &ipv4, ipStr, sizeof(ipStr))) {
                    network::PeerInfo info;
                    info.address = ipStr;
                    info.port = addr.port;
                    info.services = addr.services;
                    info.timestamp = addr.timestamp;
                    info.lastSeen = addr.timestamp;
                    incoming.push_back(info);
                }
            }
        }
        if (!incoming.empty()) {
            discovery_->processIncoming(incoming);
        }
    }
    
    void handleGetPeersMessage(const std::string& peerId, const network::Message& msg) {
        (void)msg;
        if (!discovery_ || !network_) return;
        
        static std::unordered_map<std::string, uint64_t> lastSent;
        uint64_t now = std::time(nullptr);
        auto it = lastSent.find(peerId);
        if (it != lastSent.end() && now - it->second < 60) {
            return;
        }
        lastSent[peerId] = now;
        
        auto allPeers = discovery_->getRandomPeers(50);
        synapse::PeersMessage peersMsg;
        
        std::string requestingPeerAddr;
        auto networkPeers = network_->getPeers();
        for (const auto& p : networkPeers) {
            if (p.id == peerId) {
                requestingPeerAddr = p.address;
                break;
            }
        }
        
        for (const auto& peer : allPeers) {
            if (peer.address == requestingPeerAddr && peer.port == network_->getPort()) {
                continue;
            }
            
            if (discovery_->isBanned(peer.address)) {
                continue;
            }
            
            synapse::PeerAddress addr{};
            addr.timestamp = peer.timestamp;
            addr.services = peer.services;
            addr.port = peer.port;
            addr.addr.fill(0);
            
            in_addr ipv4{};
            if (inet_pton(AF_INET, peer.address.c_str(), &ipv4) == 1) {
                addr.addr[10] = 0xff;
                addr.addr[11] = 0xff;
                std::memcpy(addr.addr.data() + 12, &ipv4, 4);
                peersMsg.peers.push_back(addr);
            }
            
            if (peersMsg.peers.size() >= 50) break;
        }
        
        if (!peersMsg.peers.empty()) {
            auto reply = makeMessage("peers", peersMsg.serialize());
            network_->send(peerId, reply);
        }
    }
    
    void handlePeersMessage(const std::string& peerId, const network::Message& msg) {
        (void)peerId;
        if (msg.payload.empty()) return;
        if (!discovery_) return;
        
        synapse::PeersMessage peersMsg = synapse::PeersMessage::deserialize(msg.payload);
        std::vector<network::PeerInfo> incoming;
        
        for (const auto& addr : peersMsg.peers) {
            if (addr.addr[10] == 0xff && addr.addr[11] == 0xff) {
                char ipStr[INET_ADDRSTRLEN];
                in_addr ipv4{};
                std::memcpy(&ipv4, addr.addr.data() + 12, 4);
                if (inet_ntop(AF_INET, &ipv4, ipStr, sizeof(ipStr))) {
                    std::string ipString = ipStr;
                    
                    if (ipString == "127.0.0.1" || ipString == "::1" || ipString.find("127.") == 0) {
                        continue;
                    }
                    
                    if (discovery_->isBanned(ipString)) {
                        continue;
                    }
                    
                    if (network_) {
                        bool isSelf = false;
                        auto localAddr = network_->getLocalAddress();
                        if (localAddr == ipString && addr.port == network_->getPort()) {
                            isSelf = true;
                        }
                        if (!isSelf && !address_.empty()) {
                        }
                        if (isSelf) {
                            continue;
                        }
                    }
                    
                    network::PeerInfo info;
                    info.address = ipString;
                    info.port = addr.port;
                    info.services = addr.services;
                    info.timestamp = addr.timestamp;
                    info.lastSeen = addr.timestamp;
                    info.state = network::DiscoveryPeerState::UNKNOWN;
                    info.attempts = 0;
                    incoming.push_back(info);
                }
            }
        }
        
        if (!incoming.empty()) {
            discovery_->processIncoming(incoming);
        }
    }
    
	    void handleInvMessage(const std::string& peerId, const network::Message& msg) {
	        if (msg.payload.empty()) return;
	        synapse::InvMessage inv = synapse::InvMessage::deserialize(msg.payload);
	        synapse::GetDataMessage req;
            const bool overloaded = network_ ? network_->getStats().overloadMode : false;
            const uint32_t maxItems = overloaded ? config_.networkInvOverloadItems : config_.networkInvMaxItems;
            std::unordered_set<std::string> seenItems;
            seenItems.reserve(inv.items.size());
            uint64_t dropped = 0;
        
        for (const auto& item : inv.items) {
            if (req.items.size() >= maxItems) {
                dropped += 1;
                continue;
            }

            std::string itemKey = std::to_string(static_cast<int>(item.type)) + ":" +
                crypto::toHex(item.hash.data(), item.hash.size());
            if (!seenItems.insert(itemKey).second) {
                dropped += 1;
                continue;
            }

            bool known = false;
            std::string h = crypto::toHex(item.hash.data(), item.hash.size());
            
            if (item.type == synapse::InvType::TX) {
                {
                    std::lock_guard<std::mutex> lock(invMtx_);
                    if (knownTxs_.count(h) > 0) known = true;
                }
                if (!known && transfer_) {
                    crypto::Hash256 txid{};
                    std::memcpy(txid.data(), item.hash.data(), txid.size());
                    known = transfer_->hasTransaction(txid);
                }
            } else if (item.type == synapse::InvType::KNOWLEDGE) {
                std::lock_guard<std::mutex> lock(invMtx_);
                known = (knownKnowledge_.count(h) > 0) || (knowledgeByHash_.count(h) > 0);
            } else if (item.type == synapse::InvType::BLOCK) {
                {
                    std::lock_guard<std::mutex> lock(invMtx_);
                    if (knownBlocks_.count(h) > 0) known = true;
                }
                if (!known && ledger_) {
                    crypto::Hash256 bh{};
                    std::memcpy(bh.data(), item.hash.data(), bh.size());
                    known = ledger_->getBlockByHash(bh).hash != crypto::Hash256{};
                }
            } else if (item.type == synapse::InvType::POE_ENTRY) {
                {
                    std::lock_guard<std::mutex> lock(invMtx_);
                    if (knownPoeEntries_.count(h) > 0) known = true;
                }
                if (!known && poeV1_) {
                    crypto::Hash256 sid{};
                    std::memcpy(sid.data(), item.hash.data(), sid.size());
                    known = poeV1_->getEntry(sid).has_value();
                }
	            } else if (item.type == synapse::InvType::POE_VOTE) {
	                {
	                    std::lock_guard<std::mutex> lock(invMtx_);
	                    if (knownPoeVotes_.count(h) > 0) known = true;
	                }
	                if (!known && poeV1_) {
	                    crypto::Hash256 vid{};
	                    std::memcpy(vid.data(), item.hash.data(), vid.size());
	                    known = poeV1_->getVoteById(vid).has_value();
	                }
	            } else if (item.type == synapse::InvType::POE_EPOCH) {
	                {
	                    std::lock_guard<std::mutex> lock(invMtx_);
	                    if (knownPoeEpochs_.count(h) > 0) known = true;
	                }
	                if (!known && poeV1_) {
	                    crypto::Hash256 hid{};
	                    std::memcpy(hid.data(), item.hash.data(), hid.size());
	                    auto eid = epochIdFromPoeInvHash(hid);
	                    if (!eid) {
	                        known = true;
	                    } else {
	                        known = poeV1_->getEpoch(*eid).has_value();
	                    }
	                }
	            } else if (item.type == synapse::InvType::UPDATE_BUNDLE) {
	                std::lock_guard<std::mutex> lock(invMtx_);
	                known = (knownUpdateBundles_.count(h) > 0) || (updateManifestsById_.count(h) > 0);
	            }
            
            if (!known) {
                req.items.push_back(item);
            }
        }
        if (dropped > 0) {
            invBackpressureDrops_.fetch_add(dropped);
        }
        
        if (!req.items.empty()) {
            auto request = makeMessage("getdata", req.serialize());
            network_->send(peerId, request);
        }
    }
    
	    void handleGetDataMessage(const std::string& peerId, const network::Message& msg) {
	        if (msg.payload.empty()) return;
	        synapse::GetDataMessage req = synapse::GetDataMessage::deserialize(msg.payload);
            const bool overloaded = network_ ? network_->getStats().overloadMode : false;
            const uint32_t maxItems = overloaded ? config_.networkGetDataOverloadItems : config_.networkGetDataMaxItems;
            uint64_t processed = 0;
            uint64_t dropped = 0;
        
        for (const auto& item : req.items) {
            if (processed >= maxItems) {
                dropped += 1;
                continue;
            }
            processed += 1;
            if (item.type == synapse::InvType::TX && transfer_) {
                crypto::Hash256 txid{};
                std::memcpy(txid.data(), item.hash.data(), txid.size());
                core::Transaction tx = transfer_->getTransaction(txid);
                if (tx.txid != crypto::Hash256{}) {
                    auto reply = makeMessage("tx", tx.serialize());
                    network_->send(peerId, reply);
                }
            } else if (item.type == synapse::InvType::KNOWLEDGE && knowledge_) {
                std::string h = crypto::toHex(item.hash.data(), item.hash.size());
                uint64_t id = 0;
                {
                    std::lock_guard<std::mutex> lock(invMtx_);
                    auto it = knowledgeByHash_.find(h);
                    if (it != knowledgeByHash_.end()) id = it->second;
                }
                if (id != 0) {
                    core::KnowledgeEntry entry = knowledge_->get(id);
                    if (entry.id != 0) {
                        auto reply = makeMessage("knowledge", entry.serialize());
                        network_->send(peerId, reply);
                    }
                }
            } else if (item.type == synapse::InvType::BLOCK && ledger_) {
                crypto::Hash256 bh{};
                std::memcpy(bh.data(), item.hash.data(), bh.size());
                core::Block block = ledger_->getBlockByHash(bh);
                if (block.hash != crypto::Hash256{}) {
                    auto reply = makeMessage("block", block.serialize());
                    network_->send(peerId, reply);
                }
            } else if (item.type == synapse::InvType::POE_ENTRY && poeV1_) {
                crypto::Hash256 sid{};
                std::memcpy(sid.data(), item.hash.data(), sid.size());
                auto entry = poeV1_->getEntry(sid);
                if (entry) {
                    auto reply = makeMessage("poe_entry", entry->serialize());
                    network_->send(peerId, reply);
                }
	            } else if (item.type == synapse::InvType::POE_VOTE && poeV1_) {
	                crypto::Hash256 vid{};
	                std::memcpy(vid.data(), item.hash.data(), vid.size());
	                auto vote = poeV1_->getVoteById(vid);
	                if (vote) {
	                    auto reply = makeMessage("poe_vote", vote->serialize());
	                    network_->send(peerId, reply);
	                }
	            } else if (item.type == synapse::InvType::POE_EPOCH && poeV1_) {
	                crypto::Hash256 hid{};
	                std::memcpy(hid.data(), item.hash.data(), hid.size());
	                auto eid = epochIdFromPoeInvHash(hid);
	                if (!eid) continue;
	                auto epoch = poeV1_->getEpoch(*eid);
	                if (epoch) {
	                    auto reply = makeMessage("poe_epoch", serializePoeEpoch(*epoch));
	                    network_->send(peerId, reply);
	                }
	            } else if (item.type == synapse::InvType::UPDATE_BUNDLE) {
	                core::UpdateManifest manifest;
	                bool found = false;
	                std::string idHex = crypto::toHex(item.hash.data(), item.hash.size());
	                {
	                    std::lock_guard<std::mutex> lock(invMtx_);
	                    auto it = updateManifestsById_.find(idHex);
	                    if (it != updateManifestsById_.end()) {
	                        manifest = it->second;
	                        found = true;
	                    }
	                }
	                if (found) {
	                    auto reply = makeMessage("upd_manifest", manifest.serialize());
	                    network_->send(peerId, reply);
	                }
	            }
	        }
            if (dropped > 0) {
                getDataBackpressureDrops_.fetch_add(dropped);
            }
	    }
    
    void handleGetBlockMessage(const std::string& peerId, const network::Message& msg) {
        if (!ledger_) return;
        if (msg.payload.size() < 8) return;
        uint64_t height = deserializeU64(msg.payload);
        core::Block block = ledger_->getBlock(height);
        if (block.hash != crypto::Hash256{}) {
            auto reply = makeMessage("block", block.serialize());
            network_->send(peerId, reply);
        }
    }
    
    void handleBlockMessage(const std::string& peerId, const network::Message& msg) {
        (void)peerId;
        if (!ledger_) return;
        core::Block block = core::Block::deserialize(msg.payload);
        if (block.hash == crypto::Hash256{}) return;

        std::vector<core::Transaction> blockTxs;
        if (transfer_) {
            for (const auto& ev : block.events) {
                if (ev.type != core::EventType::TRANSFER) continue;
                core::Transaction tx = core::Transaction::deserialize(ev.data);
                if (tx.txid == crypto::Hash256{}) return;
                blockTxs.push_back(tx);
            }
            if (!blockTxs.empty()) {
                if (!transfer_->verifyTransactionsInBlockOrder(blockTxs)) return;
            }
        }

        if (!ledger_->appendBlockWithValidation(block)) return;
        
        {
            std::lock_guard<std::mutex> lock(syncMtx_);
            requestedBlocks_.erase(block.height);
        }
        {
            std::lock_guard<std::mutex> lock(invMtx_);
            knownBlocks_.insert(crypto::toHex(block.hash));
        }
        uint64_t impliedHeight = block.height + 1;
        if (impliedHeight > networkHeight_) {
            networkHeight_ = impliedHeight;
        }
        
        suppressCallbacks_ = true;
        for (const auto& ev : block.events) {
            if (ev.type == core::EventType::KNOWLEDGE && knowledge_) {
                core::KnowledgeEntry entry = core::KnowledgeEntry::deserialize(ev.data);
                knowledge_->importEntry(entry);
            } else if (ev.type == core::EventType::POE_ENTRY && poeV1_) {
                auto entry = core::poe_v1::KnowledgeEntryV1::deserialize(ev.data);
                if (entry) {
                    poeV1_->importEntry(*entry, nullptr);
                }
            } else if (ev.type == core::EventType::POE_VOTE && poeV1_) {
                auto vote = core::poe_v1::ValidationVoteV1::deserialize(ev.data);
                if (vote) {
                    poeV1_->addVote(*vote);
                }
            }
        }
        suppressCallbacks_ = false;

        if (transfer_ && !blockTxs.empty()) {
            if (!transfer_->applyBlockTransactionsFromBlock(blockTxs, block.height, block.hash)) {
                utils::Logger::error("Failed to apply block transfer events (received block)");
            }
        }
    }
    
    void handleKnowledgeMessage(const std::string& peerId, const network::Message& msg) {
        (void)peerId;
        if (!knowledge_) return;
        if (msg.payload.empty()) return;
        core::KnowledgeEntry entry = core::KnowledgeEntry::deserialize(msg.payload);
        knowledge_->importEntry(entry);
    }
    
    void handleTxMessage(const std::string& peerId, const network::Message& msg) {
        (void)peerId;
        if (!transfer_) return;
        if (msg.payload.empty()) return;
        core::Transaction tx = core::Transaction::deserialize(msg.payload);
        transfer_->submitTransaction(tx);
    }

    void handleMempoolMessage(const std::string& peerId, const network::Message& msg) {
        (void)msg;
        if (!network_ || !transfer_) return;

        auto pending = transfer_->getPending();
        if (pending.empty()) return;

        synapse::InvMessage inv;
        size_t limit = 5000;
        size_t count = 0;
        for (const auto& tx : pending) {
            if (count++ >= limit) break;
            synapse::InvItem item;
            item.type = synapse::InvType::TX;
            std::memcpy(item.hash.data(), tx.txid.data(), tx.txid.size());
            inv.items.push_back(item);
        }

        if (inv.items.empty()) return;
        auto reply = makeMessage("inv", inv.serialize());
        network_->send(peerId, reply);
    }

    uint64_t maybeCreditAcceptanceReward(const crypto::Hash256& submitId) {
        if (!poeV1_ || !transfer_) return 0;

        auto fin = poeV1_->finalize(submitId);
        if (!fin) return 0;

        auto entry = poeV1_->getEntry(submitId);
        if (!entry) return 0;

        std::string addr = addressFromPubKey(entry->authorPubKey);
        if (addr.empty()) return 0;

        uint64_t amount = poeV1_->calculateAcceptanceReward(*entry);
        if (amount == 0) return 0;

        crypto::Hash256 rewardId = node::rewardIdForAcceptance(submitId);
        if (transfer_->creditRewardDeterministic(addr, rewardId, amount)) {
            return amount;
        }
        return 0;
    }

    void autoVoteSweepAllPending() {
        if (!poeV1_ || !keys_ || !keys_->isValid()) return;
        auto allIds = poeV1_->listEntryIds(0);
        for (const auto& sid : allIds) {
            if (!poeV1_->isFinalized(sid)) {
                maybeAutoVote(sid);
            }
        }
    }

    void maybeRunAutoPoeEpoch(uint64_t now) {
        if (!poeV1_ || !transfer_) return;

        auto& runtimeCfg = utils::Config::instance();
        if (!runtimeCfg.getBool("poe.epoch.auto_enabled", true)) return;

        int64_t intervalRaw = runtimeCfg.getInt64("poe.epoch.auto_interval_seconds", 30);
        if (intervalRaw < 5) intervalRaw = 5;
        if (intervalRaw > 86400) intervalRaw = 86400;
        const uint64_t intervalSec = static_cast<uint64_t>(intervalRaw);

        const uint64_t lastRun = autoPoeEpochLastRunAt_.load();
        if (lastRun != 0 && now < (lastRun + intervalSec)) return;

        const bool requireNewFinalized = runtimeCfg.getBool("poe.epoch.auto_require_new_finalized", true);
        const uint64_t finalizedCount = poeV1_->totalFinalized();
        if (finalizedCount == 0) return;
        if (requireNewFinalized && finalizedCount <= autoPoeEpochLastFinalizedCount_.load()) return;

        int64_t cfgBudget = runtimeCfg.getInt64(
            "poe.epoch_budget",
            config_.dev ? 100000000LL : 1000000000LL);
        const uint64_t budget = cfgBudget > 0 ? static_cast<uint64_t>(cfgBudget) : 0ULL;
        if (budget == 0) {
            autoPoeEpochLastRunAt_.store(now);
            return;
        }
        const uint32_t iters = static_cast<uint32_t>(std::max(1, runtimeCfg.getInt(
            "poe.epoch_iterations",
            config_.dev ? 10 : 20)));

        auto epochRes = poeV1_->runEpoch(budget, iters);
        autoPoeEpochLastRunAt_.store(now);
        if (!epochRes.ok) {
            if (epochRes.error != "no_finalized_entries") {
                utils::Logger::warn("Auto PoE epoch skipped: " + epochRes.error);
            }
            return;
        }

        {
            crypto::Hash256 hid = poeEpochInvHash(epochRes.epochId);
            std::lock_guard<std::mutex> lock(invMtx_);
            knownPoeEpochs_.insert(crypto::toHex(hid));
        }
        broadcastInv(synapse::InvType::POE_EPOCH, poeEpochInvHash(epochRes.epochId));

        uint64_t mintedTotal = 0;
        uint64_t mintedMine = 0;
        uint64_t mintedCount = 0;
        for (const auto& a : epochRes.allocations) {
            std::string addr = addressFromPubKey(a.authorPubKey);
            if (addr.empty()) continue;
            crypto::Hash256 rid = node::rewardIdForEpoch(epochRes.epochId, a.contentId);
            if (transfer_->creditRewardDeterministic(addr, rid, a.amount)) {
                mintedTotal += a.amount;
                mintedCount += 1;
                if (!address_.empty() && addr == address_) mintedMine += a.amount;
            }
        }

        if (mintedMine > 0) {
            autoPoeLastRewardEpochId_.store(epochRes.epochId);
            autoPoeLastRewardMineAtoms_.store(mintedMine);
            autoPoeLastRewardEntries_.store(mintedCount);
            autoPoeLastRewardAt_.store(now);
        }

        autoPoeEpochLastFinalizedCount_.store(finalizedCount);

        std::ostringstream oss;
        oss << "Auto epoch #" << epochRes.epochId << " distributed "
            << std::fixed << std::setprecision(8)
            << (static_cast<double>(mintedTotal) / 100000000.0) << " NGT";
        if (mintedMine > 0) {
            oss << " (you: " << std::fixed << std::setprecision(8)
                << (static_cast<double>(mintedMine) / 100000000.0) << " NGT)";
        }
        oss << " to " << mintedCount << " entries";
        utils::Logger::info(oss.str());
    }

    void maybeAutoVote(const crypto::Hash256& submitId) {
        if (!poeV1_ || !keys_ || !keys_->isValid()) return;

        auto entry = poeV1_->getEntry(submitId);
        if (!entry) return;
        if (poeV1_->isFinalized(submitId)) return;

        updatePoeValidatorsFromStake();
        core::PoeV1Config cfg = poeV1_->getConfig();
        auto validators = poeV1_->getDeterministicValidators();
        if (validators.empty()) return;

        auto pubV = keys_->getPublicKey();
        if (pubV.size() < crypto::PUBLIC_KEY_SIZE) return;
        crypto::PublicKey selfPub{};
        std::memcpy(selfPub.data(), pubV.data(), selfPub.size());

        uint32_t selectedCount = poeV1_->effectiveSelectedValidators();
        if (selectedCount == 0) return;

        auto selected = core::poe_v1::selectValidators(poeV1_->chainSeed(), submitId, validators, selectedCount);
        if (std::find(selected.begin(), selected.end(), selfPub) == selected.end()) return;

        auto votes = poeV1_->getVotesForSubmit(submitId);
        for (const auto& v : votes) {
            if (v.validatorPubKey == selfPub) return;
        }

        auto privV = keys_->getPrivateKey();
        if (privV.size() < crypto::PRIVATE_KEY_SIZE) return;
        crypto::PrivateKey priv{};
        std::memcpy(priv.data(), privV.data(), priv.size());

        core::poe_v1::ValidationVoteV1 vote;
        vote.version = 1;
        vote.submitId = submitId;
        vote.prevBlockHash = poeV1_->chainSeed();
        vote.flags = 0;
        vote.scores = {100, 100, 100};
        core::poe_v1::signValidationVoteV1(vote, priv);

        crypto::Hash256 vid = vote.payloadHash();
        std::string vidHex = crypto::toHex(vid);

        bool added = poeV1_->addVote(vote);
        if (!added) return;

        {
            std::lock_guard<std::mutex> lock(invMtx_);
            knownPoeVotes_.insert(vidHex);
        }
        broadcastInv(synapse::InvType::POE_VOTE, vid);
        maybeCreditAcceptanceReward(submitId);
    }

    void handlePoeEntryMessage(const std::string& peerId, const network::Message& msg) {
        (void)peerId;
        if (!poeV1_) return;
        if (msg.payload.empty()) return;

        auto entry = core::poe_v1::KnowledgeEntryV1::deserialize(msg.payload);
        if (!entry) return;

        std::string reason;
        bool added = poeV1_->importEntry(*entry, &reason);
        crypto::Hash256 sid = entry->submitId();
        std::string sidHex = crypto::toHex(sid);

        if (added || reason == "duplicate_submit") {
            std::lock_guard<std::mutex> lock(invMtx_);
            knownPoeEntries_.insert(sidHex);
        }

        if (added) {
            broadcastInv(synapse::InvType::POE_ENTRY, sid);
        }

        maybeAutoVote(sid);
        maybeCreditAcceptanceReward(sid);
    }

	    void handlePoeVoteMessage(const std::string& peerId, const network::Message& msg) {
	        (void)peerId;
	        if (!poeV1_) return;
	        if (msg.payload.empty()) return;

        auto vote = core::poe_v1::ValidationVoteV1::deserialize(msg.payload);
        if (!vote) return;

        crypto::Hash256 vid = vote->payloadHash();
        std::string vidHex = crypto::toHex(vid);
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        {
            std::lock_guard<std::mutex> lock(invMtx_);
            auto it = recentPoeVotes_.find(vidHex);
            if (it != recentPoeVotes_.end() &&
                now <= it->second + static_cast<uint64_t>(config_.networkVoteDedupWindowSeconds)) {
                return;
            }
            recentPoeVotes_[vidHex] = now;
            if (recentPoeVotes_.size() > config_.networkVoteDedupMaxEntries) {
                const uint64_t ttl = std::max<uint32_t>(30, config_.networkVoteDedupWindowSeconds);
                for (auto itr = recentPoeVotes_.begin(); itr != recentPoeVotes_.end();) {
                    if (now > itr->second + ttl) itr = recentPoeVotes_.erase(itr);
                    else ++itr;
                }
                if (recentPoeVotes_.size() > config_.networkVoteDedupMaxEntries) {
                    std::vector<std::pair<std::string, uint64_t>> items;
                    items.reserve(recentPoeVotes_.size());
                    for (const auto& kv : recentPoeVotes_) items.push_back(kv);
                    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
                        if (a.second != b.second) return a.second > b.second;
                        return a.first < b.first;
                    });
                    recentPoeVotes_.clear();
                    const size_t keep = std::min<size_t>(config_.networkVoteDedupMaxEntries, items.size());
                    for (size_t idx = 0; idx < keep; ++idx) {
                        recentPoeVotes_[items[idx].first] = items[idx].second;
                    }
                }
            }
        }

        bool added = poeV1_->addVote(*vote);
        if (added) {
            {
                std::lock_guard<std::mutex> lock(invMtx_);
                knownPoeVotes_.insert(vidHex);
            }
            broadcastInv(synapse::InvType::POE_VOTE, vid);
        }

	        maybeCreditAcceptanceReward(vote->submitId);
	    }

	    void handlePoeEpochMessage(const std::string& peerId, const network::Message& msg) {
	        (void)peerId;
	        if (!poeV1_ || !transfer_) return;
	        if (msg.payload.empty()) return;

	        auto epoch = deserializePoeEpoch(msg.payload);
	        if (!epoch) return;

	        crypto::Hash256 hid = poeEpochInvHash(epoch->epochId);
	        std::string hidHex = crypto::toHex(hid);

	        if (!poeV1_->importEpoch(*epoch)) return;

	        {
	            std::lock_guard<std::mutex> lock(invMtx_);
	            knownPoeEpochs_.insert(hidHex);
	        }
	        broadcastInv(synapse::InvType::POE_EPOCH, hid);

        auto stored = poeV1_->getEpoch(epoch->epochId);
        if (!stored) return;

        uint64_t mintedMine = 0;
        uint64_t mintedCount = 0;
        for (const auto& a : stored->allocations) {
            std::string addr = addressFromPubKey(a.authorPubKey);
            if (addr.empty()) continue;
            crypto::Hash256 rid = node::rewardIdForEpoch(stored->epochId, a.contentId);
            if (transfer_->creditRewardDeterministic(addr, rid, a.amount)) {
                mintedCount += 1;
                if (!address_.empty() && addr == address_) {
                    mintedMine += a.amount;
                }
            }
        }

        if (mintedMine > 0) {
            const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
            autoPoeLastRewardEpochId_.store(stored->epochId);
            autoPoeLastRewardMineAtoms_.store(mintedMine);
            autoPoeLastRewardEntries_.store(mintedCount);
            autoPoeLastRewardAt_.store(now);
        }
    }

	    void handleUpdateManifestMessage(const std::string& peerId, const network::Message& msg) {
	        (void)peerId;
	        if (msg.payload.empty()) return;

	        auto manifest = core::UpdateManifest::deserialize(msg.payload);
	        if (!manifest) return;

	        std::string reason;
	        auto status = acceptUpdateManifest(*manifest, true, &reason);
	        if (status == UpdateManifestAccept::REJECTED) {
	            utils::Logger::warn("Rejected update manifest from peer: " + reason);
	        }
	    }
