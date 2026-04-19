#pragma once

#include <cstdint>
#include <string>

#include "web/web.h"

namespace synapse {
namespace web {

struct ConnectorAuditWriteResult {
    bool ok = false;
    std::string contentHashHex;
    std::string extractHashHex;
    std::string reason;
};

std::string defaultConnectorAuditDir();

ConnectorAuditWriteResult writeConnectorAuditArtifact(const std::string& auditDir,
                                                      const std::string& sourceUrl,
                                                      const std::string& fetchedBody,
                                                      const ExtractedContent& extracted,
                                                      uint64_t atTimestamp);

}
}
