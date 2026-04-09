#include "model/model_loader.h"
#include <cassert>
#include <string>

static void testUpsertAndStats() {
    synapse::model::ModelMarketplace m;

    // Stable listing id (matches offer id concept).
    assert(m.upsertModel("offer123", "ownerA", "ModelA", "desc", 123, "GGUF", 10, 5, 3, true));

    auto listings = m.getAllListings(false);
    assert(listings.size() == 1);
    assert(listings[0].modelId == "offer123");
    assert(listings[0].pricePerRequestAtoms == 5);
    assert(listings[0].maxSlots == 3);

    auto st = m.getStats();
    assert(st.totalListings == 1);
    assert(st.activeListings == 1);
    assert(st.avgPricePerRequestAtoms == 5);
}

static void testRentPaymentRequestFlow() {
    synapse::model::ModelMarketplace m;
    assert(m.upsertModel("offerX", "ownerX", "ModelX", "", 0, "GGUF", 0, 7, 2, true));

    std::string session1 = m.rentModel("offerX", "renter1");
    assert(!session1.empty());

    // record payment then request
    assert(m.recordPayment(session1, 7));
    assert(m.recordRequest(session1, 0, 12));

    auto st = m.getStats();
    assert(st.totalRequests == 1);
    assert(st.totalEarningsAtoms == 7);
    assert(st.totalVolumeAtoms == 7);

    // end rental
    assert(m.endRental(session1));
}

static void testRejectsInvalidSession() {
    synapse::model::ModelMarketplace m;
    assert(!m.recordPayment("missing", 1));
    assert(!m.recordRequest("missing", 0, 1));
    assert(!m.endRental("missing"));
}

int main() {
    testUpsertAndStats();
    testRentPaymentRequestFlow();
    testRejectsInvalidSession();
    return 0;
}

